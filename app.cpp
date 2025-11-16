/*
 * Main application logic for the shotStopper controller.
 *
 * This file contains the primary setup and coordination logic,
 * separating it from the Arduino-specific .ino file.
 * It now initializes and coordinates the persistent BLE task.
 */

#include "app.h"
#include "ble_client.h"
#include "encoder.h"
#include "lcd_bsp.h"
#include "lcd_bl_pwm_bsp.h"
#include <Arduino.h>
#include <Preferences.h>

Preferences preferences;

// Task handles
extern TaskHandle_t ble_task_handle; // Defined in ble_client.cpp

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

    // Send the initial read command to the BLE task
    BLECommand initial_read_cmd = { .type = BLE_READ_WEIGHT, .payload = 0 };
    send_ble_command(initial_read_cmd);

    Serial.println("Application initialization complete.");
}
