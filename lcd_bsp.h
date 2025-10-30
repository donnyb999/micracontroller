#ifndef LCD_BSP_H
#define LCD_BSP_H
#include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
// #include "demos/lv_demos.h" // Removed for LVGL v9+
#include "lvgl.h" // Use #include "lvgl.h" for v9+
#include "esp_check.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// LVGL v9 prototypes
static void example_lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map);
static void example_lvgl_rounder_cb(lv_event_t * e); // Made static
static void example_increase_lvgl_tick(void *arg);
static void example_lvgl_port_task(void *arg);
static void example_lvgl_unlock(void);
static bool example_lvgl_lock(int timeout_ms);
void lcd_lvgl_Init(void);
static void example_lvgl_touch_cb(lv_indev_t * indev, lv_indev_data_t * data);

#ifdef __cplusplus
}
#endif

#endif

