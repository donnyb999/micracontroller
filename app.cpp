/*
 * Main application logic for the shotStopper controller.
 *
 * This file contains the primary setup and coordination logic,
 * separating it from the Arduino-specific .ino file.
 * It now initializes and coordinates the persistent BLE and WiFi/MQTT tasks,
 * pinning them to the same core to prevent radio hardware conflicts.
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

    lcd_lvgl_Init();
    lcd_bl_pwm_bsp_init(BRIGHTNESS_HIGH);
    encoder_init();
    preferences.begin("shotStopper", false);

    // Initialize BLE client task (creates the persistent task)
    ble_client_task_init();

    // Initialize Home Assistant (connects to WiFi/MQTT)
    ha_init();

    // After BLE and HA are initialized, create the HA loop task
    // Pin both tasks to the same core (APP_CPU) to prevent radio conflicts
    xTaskCreatePinnedToCore(
        ha_loop_task,
        "HA_Loop_Task",
        4096,
        NULL,
        1, // Priority
        &ha_loop_task_handle,
        APP_CPU_NUM // Core ID
    );



    // Send the initial read command to the BLE task
    BLECommand initial_read_cmd = { .type = BLE_READ_WEIGHT, .payload = 0 };
    send_ble_command(initial_read_cmd);

    // Create the FreeRTOS task for the HA MQTT loop
    xTaskCreate(
        ha_loop_task,          // Task function
        "HA Loop Task",        // Task name
        4096,                  // Stack size
        NULL,                  // Task parameters
        1,                     // Priority
        &ha_loop_task_handle   // Task handle
    );

    Serial.println("Application initialization complete.");
}
