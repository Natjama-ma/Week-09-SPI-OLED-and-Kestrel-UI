#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "font5x7.h"

#define TAG "CLOSED_LOOP_IOT"

// ==============================================================================
// 1. PIN DEFINITIONS (ตามผังวงจรฮาร์ดแวร์ Week 09)
// ==============================================================================
#define OLED_PIN_SCK    (GPIO_NUM_18) // D0 (SPI Clock)
#define OLED_PIN_MOSI   (GPIO_NUM_23) // D1 (SPI MOSI Data)
#define OLED_PIN_RES    (GPIO_NUM_4)  // RES (Hardware Reset)
#define OLED_PIN_DC     (GPIO_NUM_2)  // DC (0 = Command, 1 = Data)
#define OLED_PIN_CS     (GPIO_NUM_5)  // CS (Chip Select - Active LOW)

// Potentiometer Input (ADC1 Channel 6 = GPIO 34)
#define POT_ADC_UNIT    (ADC_UNIT_1)
#define POT_ADC_CHANNEL (ADC_CHANNEL_6)

// Serial UART Configuration
#define UART_PORT       (UART_NUM_0)
#define UART_RX_BUF_SZ  (512)

// ==============================================================================
// 2. DISPLAY GLOBALS & BUFFERS
// ==============================================================================
static spi_device_handle_t s_spi_handle = NULL;
static uint8_t s_oled_buffer[1024]; // 128 cols x 8 pages = 1,024 bytes (1KB Framebuffer)

// ==============================================================================
// 3. LOW-LEVEL SPI & OLED DRIVER FUNCTIONS
// ==============================================================================

// กำหนดค่าบัสฮาร์ดแวร์ SPI2 และ GPIO
esp_err_t oled_spi_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RES),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,               // Write-only (จอไม่มี MISO)
        .mosi_io_num = OLED_PIN_MOSI,     // GPIO 23
        .sclk_io_num = OLED_PIN_SCK,      // GPIO 18
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024 + 16,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz SPI Clock
        .mode = 0,                          // SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = OLED_PIN_CS,        // GPIO 5
        .queue_size = 7,
    };

    return spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
}

// ส่งคำสั่ง 1 ไบต์ (Command: DC = 0)
void oled_send_cmd(uint8_t cmd)
{
    gpio_set_level(OLED_PIN_DC, 0); 
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8; 
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(s_spi_handle, &t);
}

// ส่งบล็อกข้อมูลพิกเซล (Data: DC = 1)
void oled_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(OLED_PIN_DC, 1); 
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; 
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi_handle, &t);
}

// ลำดับการตั้งค่าเริ่มต้นและเปิดวงจรทวีแรงดัน (Magic Sequence)
void oled_init_display(void)
{
    // Hardware Reset
    gpio_set_level(OLED_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(OLED_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    // SSD1306 Initialization Magic Sequence
    oled_send_cmd(0xAE); // Display OFF
    oled_send_cmd(0xD5); // Set Display Clock Divide Ratio
    oled_send_cmd(0x80); 
    oled_send_cmd(0xA8); // Set Multiplex Ratio
    oled_send_cmd(0x3F); // 64 lines (0x3F)
    oled_send_cmd(0xD3); // Set Display Offset
    oled_send_cmd(0x00); // 0
    oled_send_cmd(0x40); // Set Start Line (0)
    oled_send_cmd(0x8D); // Charge Pump Setting
    oled_send_cmd(0x14); // 0x14 = Enable Charge Pump (7.5V)
    oled_send_cmd(0x20); // Addressing Mode
    oled_send_cmd(0x00); // 0x00 = Horizontal Addressing Mode
    oled_send_cmd(0xA1); // Segment Re-map: 127 -> SEG0 (ป้องกันภาพกลับซ้ายขวา)
    oled_send_cmd(0xC8); // COM Output Scan: 63 -> COM0 (ป้องกันภาพกลับหัว)
    oled_send_cmd(0xDA); // COM Pins Configuration
    oled_send_cmd(0x12);
    oled_send_cmd(0x81); // Contrast Control
    oled_send_cmd(0xCF);
    oled_send_cmd(0xD9); // Pre-charge Period
    oled_send_cmd(0xF1);
    oled_send_cmd(0xDB); // VCOMH Deselect Level
    oled_send_cmd(0x40);
    oled_send_cmd(0xA4); // Output Follows RAM
    oled_send_cmd(0xA6); // Normal Display (1 = On, 0 = Off)
    oled_send_cmd(0xAF); // Display ON
}

// ==============================================================================
// 4. GRAPHIC RENDERING ENGINE & PRIMITIVES
// ==============================================================================

// ล้างหน้าจอในแรม (สีดำสนิท)
void oled_clear(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

// จุดหรือดับพิกเซลเดี่ยว
void oled_draw_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int byte_index = x + (y / 8) * 128;
    int bit_offset = y % 8;
    if (color) {
        s_oled_buffer[byte_index] |= (1 << bit_offset);
    } else {
        s_oled_buffer[byte_index] &= ~(1 << bit_offset);
    }
}

// ลากเส้นแนวนอน (Fast H-Line)
void oled_draw_line_h(int x, int y, int w, bool color)
{
    if (y < 0 || y >= 64) return;
    for (int i = 0; i < w; i++) {
        oled_draw_pixel(x + i, y, color);
    }
}

// ลากเส้นแนวตั้ง (Fast V-Line)
void oled_draw_line_v(int x, int y, int h, bool color)
{
    if (x < 0 || x >= 128) return;
    for (int i = 0; i < h; i++) {
        oled_draw_pixel(x, y + i, color);
    }
}

// วาดกรอบสี่เหลี่ยมโปร่ง (Hollow Rectangle)
void oled_draw_rect(int x, int y, int w, int h, bool color)
{
    oled_draw_line_h(x, y, w, color);
    oled_draw_line_h(x, y + h - 1, w, color);
    oled_draw_line_v(x, y, h, color);
    oled_draw_line_v(x + w - 1, y, h, color);
}

// วาดสี่เหลี่ยมทึบ (Filled Rectangle)
void oled_fill_rect(int x, int y, int w, int h, bool color)
{
    for (int i = 0; i < h; i++) {
        oled_draw_line_h(x, y + i, w, color);
    }
}

// ส่งถ่ายข้อมูลจากแรมขึ้นสู่หน้าจอจริง (Buffer Flush)
void oled_flush(void)
{
    oled_send_cmd(0x21); // Set Column Address
    oled_send_cmd(0x00); // 0
    oled_send_cmd(0x7F); // 127
    oled_send_cmd(0x22); // Set Page Address
    oled_send_cmd(0x00); // 0
    oled_send_cmd(0x07); // 7
    oled_send_data(s_oled_buffer, sizeof(s_oled_buffer));
}

// วาดตัวอักษร 1 ตัวจาก Font Matrix 5x7
void oled_draw_char(int x, int y, char c, bool color)
{
    if (c < 32 || c > 126) c = '?';
    int font_idx = c - 32;

    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[font_idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                oled_draw_pixel(x + col, y + row, color);
            } else {
                oled_draw_pixel(x + col, y + row, !color);
            }
        }
    }
    // เว้นวรรคช่องไฟ 1 พิกเซล
    for (int row = 0; row < 7; row++) {
        oled_draw_pixel(x + 5, y + row, !color);
    }
}

// พิมพ์สตริงข้อความ
void oled_draw_string(int x, int y, const char *str, bool color)
{
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 6; 
        if (x + 6 > 128) break; 
        str++;
    }
}

// ==============================================================================
// 5. MULTI-ZONE UI COMPOSITOR (ตามมาตรฐานข้อกำหนด Lab 9.3)
// ==============================================================================
void render_multizone_ui(int percent, int raw_val, const char *msg)
{
    oled_clear();

    // --------------------------------------------------------------------------
    // ZONE 1: Status Bar (Y = 0 ถึง 13) - โซนบนสีเหลือง
    // --------------------------------------------------------------------------
    oled_draw_string(2, 2, "ESP32 OK", true);
    oled_draw_string(62, 2, "ID:67030110", true);
    oled_draw_line_h(0, 13, 128, true); // เส้นแบ่งโซน 1

    // --------------------------------------------------------------------------
    // ZONE 2: Dynamic Telemetry & Bar Gauge (Y = 14 ถึง 47) - โซนสีฟ้า
    // --------------------------------------------------------------------------
    char str_val[16];
    char str_adc[16];
    snprintf(str_val, sizeof(str_val), "VAL:%3d%%", percent);
    snprintf(str_adc, sizeof(str_adc), "ADC:%4d", raw_val);

    oled_draw_string(4, 17, str_val, true);
    oled_draw_string(66, 17, str_adc, true);

    // วาด Bar Gauge Frame (กว้าง 120, สูง 10)
    int gauge_x = 4;
    int gauge_y = 31;
    int gauge_w = 120;
    int gauge_h = 10;
    oled_draw_rect(gauge_x, gauge_y, gauge_w, gauge_h, true);

    // เติมเนื้อแถบความคืบหน้าตามเปอร์เซ็นต์ (Clamp 0 - 100%)
    int clamped_percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    int inner_max_w = gauge_w - 4; // 116 pixels
    int fill_w = (clamped_percent * inner_max_w) / 100;
    if (fill_w > 0) {
        oled_fill_rect(gauge_x + 2, gauge_y + 2, fill_w, gauge_h - 4, true);
    }

    oled_draw_line_h(0, 48, 128, true); // เส้นแบ่งโซน 2

    // --------------------------------------------------------------------------
    // ZONE 3: Footer & Notification Action Bar (Y = 49 ถึง 63)
    // --------------------------------------------------------------------------
    // ถ้าข้อความเป็นคำเตือนวิกฤต ให้ Invert พื้นหลังเพื่อดึงดูดสายตา
    bool is_alert = (strstr(msg, "DANGER") != NULL || 
                     strstr(msg, "WARN") != NULL || 
                     strstr(msg, "STOP") != NULL);

    if (is_alert) {
        oled_fill_rect(0, 49, 128, 15, true);
        oled_draw_string(4, 53, msg, false); // ตัวหนังสือดำบนพื้นขาว
    } else {
        oled_draw_string(4, 53, msg, true);  // ตัวหนังสือขาวบนพื้นดำปกติ
    }

    // Flush Framebuffer ขึ้นสู่จอภาพทางกายภาพ
    oled_flush();
}

// ==============================================================================
// 6. PERIPHERAL INITIALIZATION (ADC1 & UART0)
// ==============================================================================

// กำหนดค่า ADC1 สำหรับ Potentiometer (GPIO 34)
adc_oneshot_unit_handle_t init_potentiometer_adc(void)
{
    adc_oneshot_unit_handle_t handle = NULL;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = POT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &handle));

    // กำหนด Attenuation 11/12 dB เพื่อรองรับแรงดัน 0 - 3.3V
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, POT_ADC_CHANNEL, &chan_config));
    return handle;
}

// กำหนดค่าพอร์ตสื่อสารอนุกรม UART0 สำหรับ Two-Way Serial Bridge
void init_serial_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_RX_BUF_SZ * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
}

// ฟังก์ชันอ่านข้อความทีละบรรทัดแบบ Non-Blocking
bool read_serial_line(char *buf, size_t max_len)
{
    static char rx_line[128];
    static size_t rx_idx = 0;
    uint8_t ch = 0;

    while (uart_read_bytes(UART_PORT, &ch, 1, 0) > 0) {
        if (ch == '\n' || ch == '\r') {
            if (rx_idx > 0) {
                rx_line[rx_idx] = '\0';
                strncpy(buf, rx_line, max_len - 1);
                buf[max_len - 1] = '\0';
                rx_idx = 0;
                return true;
            }
        } else if (rx_idx < sizeof(rx_line) - 1) {
            rx_line[rx_idx++] = (char)ch;
        }
    }
    return false;
}

// ==============================================================================
// 7. APPLICATION MAIN ENTRY POINT
// ==============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "=== Initializing Closed-Loop IoT System (Lab 9.3) ===");
    ESP_LOGI(TAG, "Student ID: 67030110");

    // 1. กำหนดค่าบัส SPI และปลุกหน้าจอ OLED SSD1306
    ESP_ERROR_CHECK(oled_spi_init());
    oled_init_display();

    // 2. กำหนดค่าฮาร์ดแวร์ ADC1 และ UART0
    adc_oneshot_unit_handle_t adc1_handle = init_potentiometer_adc();
    init_serial_uart();

    // แสดงหน้าจอ Splash เตรียมความพร้อม
    render_multizone_ui(0, 0, "SYSTEM READY");

    int raw_val = 0;
    int current_percent = 0;
    char oled_msg[32] = "SYSTEM READY";
    char rx_buffer[64];

    ESP_LOGI(TAG, "Two-Way Serial Bridge Running at 20 Hz (50ms interval)...");

    while (1) {
        // ----------------------------------------------------------------------
        // กิจกรรมที่ 3.1: ขั้นตอนที่ 1: อ่านค่า ADC จาก Potentiometer (GPIO 34)
        // ----------------------------------------------------------------------
        esp_err_t r = adc_oneshot_read(adc1_handle, POT_ADC_CHANNEL, &raw_val);
        if (r != ESP_OK) {
            raw_val = 0;
        }

        // ----------------------------------------------------------------------
        // กิจกรรมที่ 3.1: ขั้นตอนที่ 2: สตรีมค่าขึ้น Kestrel Server ทาง Serial
        // รูปแบบ: ADC:<raw_val>\n พร้อม Timestamp สำหรับ Latency Forensics
        // ----------------------------------------------------------------------
        int64_t t_now_ms = esp_timer_get_time() / 1000;
        printf("ADC:%d,T:%lld\n", raw_val, t_now_ms);
        fflush(stdout);

        // ----------------------------------------------------------------------
        // กิจกรรมที่ 3.1: ขั้นตอนที่ 3: ตรวจสอบข้อมูลตอบกลับจาก Kestrel Server
        // ----------------------------------------------------------------------
        if (read_serial_line(rx_buffer, sizeof(rx_buffer))) {
            int parsed_percent = 0;
            char parsed_msg[32] = {0};

            // รองรับรูปแบบคำสั่งทั้ง:
            // 1) "SET:50:CALIBRATED OK"
            // 2) "50,NORMAL" หรือ "50,CALIBRATED OK"
            // 3) "MSG:HELLO"
            if (sscanf(rx_buffer, "SET:%d:%31[^\r\n]", &parsed_percent, parsed_msg) == 2) {
                current_percent = parsed_percent;
                strncpy(oled_msg, parsed_msg, sizeof(oled_msg) - 1);
                oled_msg[sizeof(oled_msg) - 1] = '\0';
            } 
            else if (sscanf(rx_buffer, "%d,%31[^\r\n]", &parsed_percent, parsed_msg) == 2) {
                current_percent = parsed_percent;
                strncpy(oled_msg, parsed_msg, sizeof(oled_msg) - 1);
                oled_msg[sizeof(oled_msg) - 1] = '\0';
            }
            else if (sscanf(rx_buffer, "MSG:%31[^\r\n]", parsed_msg) == 1) {
                strncpy(oled_msg, parsed_msg, sizeof(oled_msg) - 1);
                oled_msg[sizeof(oled_msg) - 1] = '\0';
            }
        }

        // อัปเดตหน้าจอ Multi-Zone ทางกายภาพ
        render_multizone_ui(current_percent, raw_val, oled_msg);

        // วงรอบการทำงาน 20 Hz (ทุก 50 ms)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
