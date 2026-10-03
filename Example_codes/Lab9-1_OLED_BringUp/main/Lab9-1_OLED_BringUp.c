#include <stdio.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"  // สำหรับ vTaskDelay() และ pdMS_TO_TICKS
#include "string.h"            // สำหรับ memset()
#include "font5x7.h"           // ตาราง ASCII 5x7 Font Matrix


// --- 1. กำหนดขา GPIO ตามวงจรจริงในไดอะแกรม (Images/Lab9-1-connection.svg) ---
#define OLED_PIN_SCK    (GPIO_NUM_18) // D0 (SPI Clock)
#define OLED_PIN_MOSI   (GPIO_NUM_23) // D1 (SPI MOSI Data)
#define OLED_PIN_RES    (GPIO_NUM_4)  // RES (Reset - Active LOW)
#define OLED_PIN_DC     (GPIO_NUM_2)  // DC (0 = Command, 1 = Data)
#define OLED_PIN_CS     (GPIO_NUM_5)  // CS (Chip Select - Active LOW)
    
static spi_device_handle_t s_spi_handle = NULL;

// 1. ฟังก์ชันกำหนดค่าเริ่มต้นพิน GPIO และบัสฮาร์ดแวร์ SPI2
esp_err_t oled_spi_init(void)
{
    // กำหนดขา DC และ RES เป็น Output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RES),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // กำหนดค่าบัส SPI (Master Out Only - ไม่ใช้ MISO)
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,               // จอนี้ Write-Only ไม่มีขา MISO
        .mosi_io_num = OLED_PIN_MOSI,     // GPIO 23
        .sclk_io_num = OLED_PIN_SCK,      // GPIO 18
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024 + 16,
    };

    // ใช้ SPI2_HOST (VSPI บน ESP32)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    // ผูก Device เข้ากับ Bus (ความถี่ 10 MHz, SPI Mode 0)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz แสดงผลลื่นไหล
        .mode = 0,                          // Mode 0: CPOL=0, CPHA=0
        .spics_io_num = OLED_PIN_CS,        // GPIO 5
        .queue_size = 7,
    };

    return spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
}

// 2. ฟังก์ชันส่งคำสั่ง 1 ไบต์ (Command: DC = 0)
void oled_send_cmd(uint8_t cmd)
{
    gpio_set_level(OLED_PIN_DC, 0); // ดึง LOW เพื่อบอกชิปว่าเป็นคำสั่ง
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8; // 8 บิต (1 ไบต์)
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(s_spi_handle, &t);
}

// 3. ฟังก์ชันส่งบล็อกข้อมูลพิกเซล (Data: DC = 1)
void oled_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(OLED_PIN_DC, 1); // ดึง HIGH เพื่อบอกชิปว่าเป็นข้อมูลพิกเซล
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; // จำนวนบิต
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi_handle, &t);
}

// --- Framebuffer ขนาด 1KB ในแรมของ ESP32 ---
static uint8_t s_oled_buffer[1024];

void oled_clear(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

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

void oled_flush(void)
{
    oled_send_cmd(0x21); // Column Address
    oled_send_cmd(0x00); // Start Column
    oled_send_cmd(0x7F); // End Column (127)
    oled_send_cmd(0x22); // Page Address
    oled_send_cmd(0x00); // Start Page (0)
    oled_send_data(s_oled_buffer, sizeof(s_oled_buffer));
}

// 4. ฟังก์ชันวาดตัวอักษรเดี่ยว 1 ตัวจากตาราง Font Matrix 5x7
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
    // เว้นช่องไฟระหว่างตัวอักษร 1 พิกเซล
    for (int row = 0; row < 7; row++) {
        oled_draw_pixel(x + 5, y + row, !color);
    }
}

// 5. ฟังก์ชันพิมพ์สตริงข้อความเรียงต่อกัน
void oled_draw_string(int x, int y, const char *str, bool color)
{
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 6; // ตัวอักษรกว้าง 5 พิกเซล + ช่องไฟ 1 พิกเซล
        if (x + 6 > 128) break; // ป้องกันข้อความล้นขอบจอขวา
        str++;
    }
}

void app_main(void)
{
    // 1. เริ่มต้นระบบบัส SPI2 และตั้งค่าพิน DC/RES
    ESP_ERROR_CHECK(oled_spi_init());

    // 2. ลำดับการ Hardware Reset (ขา RES)
    gpio_set_level(OLED_PIN_RES, 0); // ดึง LOW เพื่อเริ่มรีเซ็ต
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(OLED_PIN_RES, 1); // ดึง HIGH กลับพร้อมทำงาน
    vTaskDelay(pdMS_TO_TICKS(15));

    // 3. ส่งคำสั่งเปิดวงจรทวีแรงดัน (Charge Pump) และเปิดจอ
    // ------------------------------
    // step 1 Set Display OFF ปิดการแสดงผลชั่วคราวเพื่อเตรียมการคอนฟิกเรจิสเตอร์                  
    oled_send_cmd(0xAE); // Display OFF
    // step 6 Enable Charge Pump(สำคัญที่สุด)สั่งเปิดวงจรทวีแรงดัน 7.5V ภายในชิป
    oled_send_cmd(0x8D); // Charge Pump Setting
    oled_send_cmd(0x14); // 0x14 = Enable Charge Pump (หากส่ง 0x10 จอจะดับสนิท!)
    // step 7 Set Memory Addressing Mode ตั้งค่าเป็น Horizontal Addressing Mode เพื่อให้เขียนข้อมูลต่อเนื่อง
    oled_send_cmd(0x20); // Addressing Mode
    oled_send_cmd(0x00); // Horizontal Mode
    // step 16 Set Display ON ปล่อยแสงสว่างจากแผง OLED
    oled_send_cmd(0xAF); // Display ON!
    // หมายเหตุ ในตัวอย่างนี้ไม่ได้ตั้งค่าครบทุกเงื่อนไข ให้ไปดูในตารางลำดับคำสั่งมาตรฐานในการเริ่มต้นระบบ (หัวข้อ 9.2.2)

    // 4. ทดสอบถมหน้าจอ (สว่างทั้งจอ 1,024 ไบต์)
    uint8_t buffer[128];
    memset(buffer, 0xFF, sizeof(buffer));
    oled_send_cmd(0x21); oled_send_cmd(0x00); oled_send_cmd(0x7F);
    oled_send_cmd(0x22); oled_send_cmd(0x00); oled_send_cmd(0x07);
    for (int page = 0; page < 8; page++) {
        oled_send_data(buffer, sizeof(buffer));
    }
    vTaskDelay(pdMS_TO_TICKS(1500)); // โชว์จอขาว 1.5 วินาที

    // 5. ทดสอบจุด 4 มุมจอ (Corner Pixels Test)
    oled_clear();
    oled_draw_pixel(0, 0, true);     // มุมบนซ้าย (เหลือง)
    oled_draw_pixel(127, 0, true);   // มุมบนขวา (เหลือง)
    oled_draw_pixel(0, 63, true);    // มุมล่างซ้าย (ฟ้า)
    oled_draw_pixel(127, 63, true);  // มุมล่างขวา (ฟ้า)
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(1500)); // โชว์ 4 จุด 1.5 วินาที

    // 6. พิมพ์ข้อความ Hello World และ รหัสนักศึกษา
    oled_clear();
    oled_draw_string(30, 4, "HELLO WORLD", true);   // โซนสีเหลือง
    oled_draw_string(24, 32, "ID: 65012345", true);  // โซนสีฟ้า
    oled_flush();
}