/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "example";

static i2c_master_bus_handle_t s_i2c_bus = NULL;

static esp_err_t i2c_master_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &s_i2c_bus), TAG, "Create I2C master bus failed");

    return ESP_OK;
}

/* The CH422G IO expander responds on several 7-bit addresses (0x24 mode,
   0x38 output). Add a transient device for the target address, send one byte,
   then remove it. */
static esp_err_t ch422g_write_byte(uint8_t dev_addr, uint8_t value)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_config, &dev), TAG, "Add CH422G device failed");
    esp_err_t ret = i2c_master_transmit(dev, &value, 1, I2C_MASTER_TIMEOUT_MS);
    i2c_master_bus_rm_device(dev);
    return ret;
}

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
static void gpio_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
    };
    gpio_config(&io_conf);
}

static void waveshare_esp32_s3_touch_reset(void)
{
    ch422g_write_byte(0x24, 0x01);
    ch422g_write_byte(0x38, 0x2C);
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);
    ch422g_write_byte(0x38, 0x2E);
    esp_rom_delay_us(200 * 1000);
}
#endif

esp_err_t waveshare_esp32_s3_rgb_lcd_init(
    uint8_t frame_buffer_count, esp_lcd_panel_handle_t *panel_handle, esp_lcd_touch_handle_t *touch_handle
)
{
    ESP_RETURN_ON_FALSE((panel_handle != NULL) && (touch_handle != NULL), ESP_ERR_INVALID_ARG, TAG, "Invalid arg");

    *panel_handle = NULL;
    *touch_handle = NULL;

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
#if ESP_PANEL_USE_1024_600_LCD
            .hsync_back_porch = 145,
            .hsync_front_porch = 170,
            .hsync_pulse_width = 30,
            .vsync_back_porch = 23,
            .vsync_front_porch = 12,
            .vsync_pulse_width = 2,
#else
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
#endif
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,
        .num_fbs = frame_buffer_count,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE,
        .dma_burst_size = 64,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, panel_handle), TAG, "Create RGB panel failed");

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel_handle), TAG, "Initialize RGB panel failed");

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
    ESP_LOGI(TAG, "Initialize I2C bus");
    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "Initialize I2C failed");
    ESP_LOGI(TAG, "Initialize GPIO");
    gpio_init();
    ESP_LOGI(TAG, "Reset touch controller");
    waveshare_esp32_s3_touch_reset();

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;

    ESP_LOGI(TAG, "Initialize I2C panel IO");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle),
        TAG, "Create touch IO failed"
    );

    ESP_LOGI(TAG, "Initialize touch controller GT911");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, touch_handle), TAG, "Create GT911 failed");
#endif

    return ESP_OK;
}

esp_err_t waveshare_rgb_lcd_backlight_on(void)
{
    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "Initialize I2C failed");
    ESP_RETURN_ON_ERROR(ch422g_write_byte(0x24, 0x01), TAG, "Configure CH422G failed");
    ESP_RETURN_ON_ERROR(ch422g_write_byte(0x38, 0x1E), TAG, "Enable backlight failed");
    return ESP_OK;
}
