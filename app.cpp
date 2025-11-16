/*
 * Main application logic for the shotStopper controller.
 *
 * This file contains the primary setup and coordination logic.
 * The initialization order is critical: Radio stacks (BLE, WiFi) are
 * initialized before LVGL to prevent heap fragmentation and memory
 * allocation failures.
 */

#include "app.h"
#include "ble_client.h"
#include "encoder.h"
#include "lcd_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "home_assistant.h"

Preferences preferences;

// Task handles
TaskHandle_t ha_loop_task_handle = NULL;
extern TaskHandle_t ble_task_handle; // Defined in ble_client.cpp

// --- FreeRTOS Task for HA Loop ---
void ha_loop_task(void *pvParameters) {
    Serial.println("HA MQTT loop task started.");
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected. Attempting to reconnect...");
            WiFi.reconnect();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        mqtt.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Define brightness levels
#define BRIGHTNESS_HIGH 178
#define BRIGHTNESS_DIM 51
#define BRIGHTNESS_OFF 0

void app_init() {
    Serial.println("Initializing main application...");

    // --- CRITICAL INITIALIZATION ORDER ---
    // 1. Initialize radio stacks first to allocate memory before heap fragmentation
    ble_client_task_init(); // Initialize BLE client task
    ha_init();              // Initialize Home Assistant (connects to WiFi/MQTT)

    // 2. Initialize remaining components
    lcd_lvgl_Init();
    lcd_bl_pwm_bsp_init(BRIGHTNESS_HIGH);
    encoder_init();
    preferences.begin("shotStopper", false);

    // 3. Create the HA loop task, pinning it to the same core as the BLE task
    xTaskCreatePinnedToCore(
        ha_loop_task,
        "HA_Loop_Task",
        3072, // Reduced stack size
        NULL,
        1, // Priority
        &ha_loop_task_handle,
        APP_CPU_NUM // Core ID
    );

    // 4. Send the initial read command to the BLE task
    BLECommand initial_read_cmd = { .type = BLE_READ_WEIGHT, .payload = 0 };
    send_ble_command(initial_read_cmd);

    Serial.println("Application initialization complete.");
}
