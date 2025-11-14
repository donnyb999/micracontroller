/*
 * Main application logic for the shotStopper controller.
 *
 * This file contains the primary setup and coordination logic,
 * separating it from the Arduino-specific .ino file.
 * Now spawns a task for the initial BLE read on boot.
 * Sets initial screen brightness.
 */

#include "app.h"
#include "ble_client.h" // Include BLE client for initial read
#include "encoder.h"
#include "lcd_bsp.h"
#include "lcd_bl_pwm_bsp.h" // Include the backlight header
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h> // Include for WiFi status check
#include "home_assistant.h" // Include for HA init

Preferences preferences;

// Task handle for the HA MQTT loop
TaskHandle_t ha_loop_task_handle = NULL;

// --- FreeRTOS Task for HA Loop ---
void ha_loop_task(void *pvParameters) {
    Serial.println("HA MQTT loop task started.");
    for (;;) {
        // Maintain WiFi connection
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected. Attempting to reconnect...");
            WiFi.reconnect();
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before retrying
            continue; // Skip MQTT loop until WiFi is back
        }

        // Maintain MQTT connection and process messages
        mqtt.loop(); // Call loop to process MQTT messages

        // A small delay to yield CPU time to other tasks
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Define brightness levels (0-255 for 8-bit PWM)
#define BRIGHTNESS_HIGH 178 // ~70% (255 * 0.7)
#define BRIGHTNESS_DIM 51   // ~20% (255 * 0.2)
#define BRIGHTNESS_OFF 0    // 0%

void app_init() {
    Serial.println("Initializing main application...");

    // Initialize the display driver and LVGL
    lcd_lvgl_Init();

    // Initialize and set initial backlight brightness (70%)
    lcd_bl_pwm_bsp_init(BRIGHTNESS_HIGH);

    // Initialize the rotary encoder
    encoder_init();

    // Initialize BLE client (no scan yet)
    ble_client_init();

    // Initialize non-volatile storage
    preferences.begin("shotStopper", false);

    // Initialize Home Assistant (connects to WiFi/MQTT)
    ha_init();

    // After all other init, create a task to perform the initial BLE read
    // This runs in parallel and doesn't block the main setup.
    ble_perform_initial_read();

    Serial.println("Application initialization complete.");
}

