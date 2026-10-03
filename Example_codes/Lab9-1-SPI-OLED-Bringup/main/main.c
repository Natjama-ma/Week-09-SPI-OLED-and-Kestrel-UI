// ============================================================================
// ใบงานการทดลองที่ 9.1: SPI OLED SSD1306 Multi-Zone UI Display Engine
// สาขาวิชาวิศวกรรมคอมพิวเตอร์และการศึกษา
// ============================================================================

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ssd1306_spi.h"

static const char *TAG = "MAIN_APP";

// ============================================================================
// ฟังก์ชันเรนเดอร์ Multi-Zone UI (Header, Gauge Bar, Footer)
// ============================================================================
static void render_multizone_ui(int percent, int raw_adc, const char *status_msg)
{
    char text_buf[32];

    // ล้างบัฟเฟอร์ 1KB ก่อนเริ่มวาดเฟรมใหม่
    oled_clear();

    // ------------------------------------------------------------------------
    // โซนที่ 1: Header Bar (Y = 0 ถึง 15)
    // ------------------------------------------------------------------------
    oled_draw_string(2, 2, "ESP32", true);
    oled_draw_string(45, 2, "SPI-OLED", true);
    oled_draw_string(100, 2, "OK", true);
    // ตีเส้นกั้นแนวนอนแบ่งโซน 1 ออกจากโซน 2
    oled_draw_line_h(0, 13, 128, true);

    // ------------------------------------------------------------------------
    // โซนที่ 2: Value & Progress Gauge Bar (Y = 16 ถึง 47)
    // ------------------------------------------------------------------------
    snprintf(text_buf, sizeof(text_buf), "ADC:%4d (%3d%%)", raw_adc, percent);
    oled_draw_string(4, 18, text_buf, true);

    // วาดกรอบสี่เหลี่ยมของมาตรวัด Gauge (พิกัด X=8, Y=30, กว้าง=112, สูง=12)
    oled_draw_rect(8, 30, 112, 12, true);

    // คำนวณความยาวของแถบสถานะภายใน (ความกว้างสูงสุด 108 พิกเซล)
    int fill_width = (percent * 108) / 100;
    if (fill_width > 108) fill_width = 108;
    if (fill_width < 0)   fill_width = 0;

    // ถมแถบสีขาวตามเปอร์เซ็นต์
    if (fill_width > 0) {
        oled_fill_rect(10, 32, fill_width, 8, true);
    }

    // ตีเส้นกั้นแนวนอนแบ่งโซน 2 ออกจากโซน 3
    oled_draw_line_h(0, 48, 128, true);

    // ------------------------------------------------------------------------
    // โซนที่ 3: Footer & Message Action Bar (Y = 48 ถึง 63)
    // ------------------------------------------------------------------------
    snprintf(text_buf, sizeof(text_buf), "> %s", status_msg);
    oled_draw_string(2, 52, text_buf, true);

    // ยิงส่ง Framebuffer ขนาด 1KB ทั้งแผ่นขึ้นสู่หน้าจอผ่าน SPI
    oled_flush();
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  เริ่มต้นการทดลอง Week 09: SPI OLED Bringup & UI  ");
    ESP_LOGI(TAG, "==================================================");

    // ขั้นที่ 1: กำหนดค่าฮาร์ดแวร์บัส SPI และปลุกจอด้วย Magic Init Sequence
    ESP_ERROR_CHECK(oled_spi_init());
    oled_init_display();

    // ขั้นที่ 2: Hardware Proof-of-Life ทดสอบจุดพิกเซลด้วยลายตารางหมากรุก (Checkerboard)
    ESP_LOGI(TAG, "ทดสอบพิกเซลหน้าจอ: แสดงลายตารางหมากรุก (Checkerboard)...");
    oled_fill_pattern(0xAA); // 0b10101010 สลับจุด
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(1500));

    oled_fill_pattern(0x55); // 0b01010101
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // ขั้นที่ 3: แสดงหน้าจอต้อนรับ (Splash Screen)
    oled_clear();
    oled_draw_rect(0, 0, 128, 64, true);
    oled_draw_string(14, 16, "KESTREL EDGE UI", true);
    oled_draw_string(20, 32, "SSD1306 4-SPI", true);
    oled_draw_string(26, 46, "1KB GDDRAM OK", true);
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "เข้าสู่ลูปจำลอง Multi-Zone Dynamic Gauge...");

    int simulated_adc = 0;
    int percent = 0;
    int step = 1;

    while (1) {
        // คำนวณเปอร์เซ็นต์และค่าจำลอง ADC (0 ถึง 4095)
        percent += step * 2;
        if (percent >= 100) {
            percent = 100;
            step = -1; // กวาดถอยหลัง
        } else if (percent <= 0) {
            percent = 0;
            step = 1;  // กวาดไปข้างหน้า
        }

        simulated_adc = (percent * 4095) / 100;

        // กำหนดข้อความแจ้งเตือนตามระดับเปอร์เซ็นต์
        const char *status;
        if (percent > 85) {
            status = "WARNING: HIGH!";
        } else if (percent > 40) {
            status = "STATUS: NORMAL";
        } else {
            status = "SYSTEM IDLE...";
        }

        // วาดและอัปเดตหน้าจอ Multi-Zone
        render_multizone_ui(percent, simulated_adc, status);

        vTaskDelay(pdMS_TO_TICKS(50)); // อัปเดตที่ความเร็ว 20 FPS อย่างนุ่มนวล
    }
}
