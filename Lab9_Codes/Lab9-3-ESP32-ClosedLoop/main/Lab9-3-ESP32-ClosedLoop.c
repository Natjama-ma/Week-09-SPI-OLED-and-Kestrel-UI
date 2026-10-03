#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "font5x7.h"

#define TAG "CLOSED_LOOP"

// ==========================================
// 1. การกำหนดขาสัญญาณฮาร์ดแวร์
// ==========================================
#define OLED_PIN_SCK        (GPIO_NUM_18) // D0
#define OLED_PIN_MOSI       (GPIO_NUM_23) // D1
#define OLED_PIN_RES        (GPIO_NUM_4)  // RES
#define OLED_PIN_DC         (GPIO_NUM_2)  // DC
#define OLED_PIN_CS         (GPIO_NUM_5)  // CS
#define POT_ADC1_CHAN       (ADC_CHANNEL_6) // GPIO 34 (ADC1)

// ==========================================
// 2. ตัวแปรและฟังก์ชันระบบ Framebuffer 1KB
// ==========================================
static spi_device_handle_t s_spi_oled = NULL;
static uint8_t s_oled_buffer[1024]; // 128x64 bits = 1024 bytes
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

// ฟังก์ชันส่งคำสั่งไปยัง SSD1306 (DC = 0)
void oled_send_cmd(uint8_t cmd) {
    gpio_set_level(OLED_PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_polling_transmit(s_spi_oled, &t);
}

// ฟังก์ชันส่งบล็อกข้อมูลพิกเซลไปยัง SSD1306 (DC = 1)
void oled_send_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(OLED_PIN_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_polling_transmit(s_spi_oled, &t);
}

// เคลียร์ Framebuffer ในแรม
void oled_clear(void) {
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

// วาดจุดพิกเซล (Bitwise Canvas)
void oled_draw_pixel(int x, int y, bool color) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    int idx = x + (y / 8) * 128;
    int bit = y % 8;
    if (color) s_oled_buffer[idx] |= (1 << bit);
    else       s_oled_buffer[idx] &= ~(1 << bit);
}

// วาดเส้นแนวนอน
void oled_draw_line_h(int x, int y, int w, bool color) {
    for (int i = 0; i < w; i++) oled_draw_pixel(x + i, y, color);
}

// วาดเส้นแนวตั้ง
void oled_draw_line_v(int x, int y, int h, bool color) {
    for (int i = 0; i < h; i++) oled_draw_pixel(x, y + i, color);
}

// วาดกรอบสี่เหลี่ยมโปร่ง
void oled_draw_rect(int x, int y, int w, int h, bool color) {
    oled_draw_line_h(x, y, w, color);
    oled_draw_line_h(x, y + h - 1, w, color);
    oled_draw_line_v(x, y, h, color);
    oled_draw_line_v(x + w - 1, y, h, color);
}

// วาดสี่เหลี่ยมทึบ (Filled Box สำหรับ Bar Gauge)
void oled_fill_rect(int x, int y, int w, int h, bool color) {
    for (int i = 0; i < h; i++) {
        oled_draw_line_h(x, y + i, w, color);
    }
}

// วาดตัวอักษรเดี่ยว 5x7 Font
void oled_draw_char(int x, int y, char c, bool color) {
    if (c < 32 || c > 126) c = '?';
    int font_idx = c - 32;
    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[font_idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) oled_draw_pixel(x + col, y + row, color);
        }
    }
}

// พิมพ์ข้อความสตริง
void oled_draw_string(int x, int y, const char *str, bool color) {
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 6; // กว้าง 5 + ช่องไฟ 1 พิกเซล
        if (x > 122) break; // เกินขอบจอ
        str++;
    }
}

// ยิงข้อมูลแรม 1KB ขึ้นจอผ่านบัส SPI2
void oled_flush(void) {
    oled_send_cmd(0x21); oled_send_cmd(0x00); oled_send_cmd(0x7F);
    oled_send_cmd(0x22); oled_send_cmd(0x00); oled_send_cmd(0x07);
    oled_send_data(s_oled_buffer, sizeof(s_oled_buffer));
}

// ==========================================
// 3. เริ่มต้นฮาร์ดแวร์ SPI, OLED, และ ADC1
// ==========================================
void init_hardware(void) {
    // 1. GPIO DC และ RES
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RES),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    // 2. SPI2 Bus
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = OLED_PIN_MOSI,
        .sclk_io_num = OLED_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024 + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,
        .spics_io_num = OLED_PIN_CS,
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_oled));

    // 3. Reset และ Magic Sequence สำหรับ SSD1306
    gpio_set_level(OLED_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(OLED_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    oled_send_cmd(0xAE); // Display OFF
    oled_send_cmd(0x8D); oled_send_cmd(0x14); // Enable Charge Pump (7.5V)
    oled_send_cmd(0x20); oled_send_cmd(0x00); // Horizontal Addressing Mode
    oled_send_cmd(0xAF); // Display ON

    // 4. ADC1 One-Shot บน GPIO 34
    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &s_adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12, // อ่านค่าได้เต็มสเกล 0 - 3.3V
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc1_handle, POT_ADC1_CHAN, &config));

    // 5. ติดตั้ง UART Driver บน UART0 (พอร์ตเดียวกับ Serial Monitor/USB)
    // ใช้เพื่อการอ่าน Serial แบบ Non-blocking
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
}

// ==========================================
// 4. เอนจินวาดหน้าจอ Multi-Zone (Renderer)
// ==========================================
void render_multizone_ui(int raw_val, int percent, bool is_cloud, const char *msg) {
    oled_clear();

    // --- Zone 1: Status Bar (Header: Y=0..12) ---
    // นักศึกษาต้องใส่รหัสนักศึกษาของตนเองลงในสตริงนี้
    char header_str[32];
    snprintf(header_str, sizeof(header_str), "ESP32 | 65012345"); // <-- แก้ไขเป็นรหัสนักศึกษาจริง
    oled_draw_string(2, 2, header_str, true);
    oled_draw_line_h(0, 13, 128, true); // เส้นกั้นโซน 1

    // --- Zone 2: Telemetry & Bar Gauge (Y=14..47) ---
    char val_str[32];
    snprintf(val_str, sizeof(val_str), "RAW:%04d  %3d%%", raw_val, percent);
    oled_draw_string(14, 18, val_str, true);

    // วาดกรอบ Bar Gauge (พิกัด X=10, Y=30, กว้าง=108, สูง=12)
    oled_draw_rect(10, 30, 108, 12, true);
    // คำนวณความกว้างของแถบถมด้านใน (สูงสุด 104 พิกเซล)
    int fill_w = (percent * 104) / 100;
    if (fill_w > 104) fill_w = 104;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > 0) {
        oled_fill_rect(12, 32, fill_w, 8, true);
    }
    oled_draw_line_h(0, 48, 128, true); // เส้นกั้นโซน 2

    // --- Zone 3: Mode & Notification (Footer: Y=49..63) ---
    if (is_cloud) {
        oled_draw_string(2, 52, "CLOUD:", true);
    } else {
        oled_draw_string(2, 52, "EDGE:", true);
    }
    oled_draw_string(42, 52, msg, true);

    // ยิงขึ้นหน้าจอจริง
    oled_flush();
}

// ==========================================
// 5. ฟังก์ชันหลัก (app_main)
// ==========================================
void app_main(void) {
    init_hardware();
    ESP_LOGI(TAG, "Hardware initialized successfully.");

    int raw_adc = 0;
    int calculated_percent = 0;
    char rx_line[128];
    int rx_pos = 0;

    int64_t last_cloud_rx_time = 0; // บันทึกเวลาล่าสุดที่ได้รับข้อมูลจาก Kestrel
    bool is_cloud_mode = false;
    char current_msg[32] = "STANDALONE";

    while (1) {
        int64_t now = esp_timer_get_time() / 1000; // เวลาปัจจุบัน (ms)

        // 1. อ่านค่าแอนะล็อกดิบจาก Potentiometer
        ESP_ERROR_CHECK(adc_oneshot_read(s_adc1_handle, POT_ADC1_CHAN, &raw_adc));

        // 2. ส่งค่า Raw ADC ขึ้น Kestrel ทาง Serial Stream: "ADC:<raw>,<uptime_ms>\n"
        printf("ADC:%d,%" PRId64 "\n", raw_adc, now);
        fflush(stdout);

        // 3. ตรวจสอบข้อมูลคำสั่งตอบกลับจาก Kestrel ผ่าน UART
        uint8_t byte_in = 0;
        while (uart_read_bytes(UART_NUM_0, &byte_in, 1, 0) > 0) {
            if (byte_in == '\n' || byte_in == '\r') {
                if (rx_pos > 0) {
                    rx_line[rx_pos] = '\0';

                    // ถอดรหัสคำสั่ง: รูปแบบ "SET:<percent>:<message>\n"
                    int incoming_percent = 0;
                    char incoming_msg[32] = {0};
                    if (sscanf(rx_line, "SET:%d:%31[^\r\n]", &incoming_percent, incoming_msg) == 2) {
                        calculated_percent = incoming_percent;
                        strncpy(current_msg, incoming_msg, sizeof(current_msg) - 1);
                        last_cloud_rx_time = now; // รีเซ็ตเวลา Heartbeat
                        is_cloud_mode = true;
                    }
                    rx_pos = 0; // เคลียร์บัฟเฟอร์รับข้อมูล
                }
            } else {
                if (rx_pos < sizeof(rx_line) - 1) {
                    rx_line[rx_pos++] = (char)byte_in;
                }
            }
        }

        // 4. กลไก Hybrid Fallback ตรวจสอบว่า Kestrel ยังสื่อสารอยู่หรือไม่
        // หากไม่มีข้อมูลจาก Kestrel เกิน 1.5 วินาที (1500 ms) ให้สลับกลับสู่ Edge Mode
        if (now - last_cloud_rx_time > 1500) {
            is_cloud_mode = false;
            // คำนวณแบบ Local Edge Scaling (Linear 0-4095 -> 0-100%)
            calculated_percent = (raw_adc * 100) / 4095;
            strncpy(current_msg, "LOCAL EDGE", sizeof(current_msg) - 1);
        }

        // 5. สั่งเรนเดอร์หน้าจอ Multi-Zone
        render_multizone_ui(raw_adc, calculated_percent, is_cloud_mode, current_msg);

        // หน่วงเวลาสำหรับความถี่ 20 Hz (รอบละ 50 ms)
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
