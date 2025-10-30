/*
 * Rotary encoder implementation.
 *
 * Handles knob rotation events. Routes events to either the Shot Stopper
 * weight control or the Home Assistant UI handler based on the active screen.
 *
 * Implements a 1-second debounce timer (ble_write_timer) for the Shot Stopper
 * screen to prevent rapid, successive BLE write requests. The UI is updated
 * instantly, but the BLE write is only triggered after the user stops
 * turning the knob for 1 second.
 * Calls reset_inactivity_timer() on encoder turn.
 * Corrected ble_write_timer definition (removed static).
 */

#include <Arduino.h>
#include <lvgl.h> // Include LVGL FIRST
#include "encoder.h"
#include "ble_client.h" // Include BLE client for write_target_weight
#include "lvgl_display.h" // Include display header AFTER lvgl.h
#include "bidi_switch_knob.h" // Make sure this is the correct header name


// External variable for the target weight (used by Shot Stopper screen)
extern int8_t target_weight;

// Timer handle for debouncing BLE writes
// Corrected: Removed 'static' to match 'extern' declaration in header
TimerHandle_t ble_write_timer = NULL;

// Encoder pin definitions
#define ENCODER_PIN_A 8
#define ENCODER_PIN_B 7

// Forward declaration for the HA screen encoder handler
// extern void ha_ui_handle_encoder_turn(int8_t direction); // Declared in lvgl_display.h
// Forward declaration for the HA screen timer reset
// extern void ha_ui_reset_deselection_timer(); // Declared in lvgl_display.h

// Callback function for the BLE write timer
// This function is called 1 second *after* the last encoder turn
static void ble_write_timer_callback(TimerHandle_t xTimer) {
    Serial.printf("[%lu] BLE write timer expired. Writing final weight: %d\n", millis(), target_weight);
    write_target_weight(target_weight); // Call the actual BLE write function
}

// Callback for left rotation
static void knob_left_cb(void* arg, void* data) {
    reset_inactivity_timer(); // Reset brightness/inactivity timer

    lv_obj_t* current_screen = lv_scr_act(); // Get the currently active screen

    if (current_screen == screen_shot_stopper) {
        target_weight--;
        Serial.printf("Encoder left (Shot Stopper). New target weight: %d\n", target_weight);
        hide_verification_checkmark();
        update_display_value(target_weight); // Update UI immediately

        // Don't write yet, just reset the debounce timer
        if (ble_write_timer != NULL) {
            xTimerReset(ble_write_timer, portMAX_DELAY); // Reset timer to 1 second
        }
    } else if (current_screen == screen_ha) {
        Serial.println("Encoder left (HA Screen).");
        ha_ui_handle_encoder_turn(-1); // Pass direction -1 for left turn
    }
}

// Callback for right rotation
static void knob_right_cb(void* arg, void* data) {
     reset_inactivity_timer(); // Reset brightness/inactivity timer

     lv_obj_t* current_screen = lv_scr_act(); // Get the currently active screen

    if (current_screen == screen_shot_stopper) {
        target_weight++;
        Serial.printf("Encoder right (Shot Stopper). New target weight: %d\n", target_weight);
        hide_verification_checkmark();
        update_display_value(target_weight); // Update UI immediately

        // Don't write yet, just reset the debounce timer
        if (ble_write_timer != NULL) {
            xTimerReset(ble_write_timer, portMAX_DELAY); // Reset timer to 1 second
        }
    } else if (current_screen == screen_ha) {
         Serial.println("Encoder right (HA Screen).");
         ha_ui_handle_encoder_turn(1); // Pass direction 1 for right turn
    }
}

// Initialize the rotary encoder
void encoder_init() {
    knob_config_t cfg = {
        .gpio_encoder_a = ENCODER_PIN_A,
        .gpio_encoder_b = ENCODER_PIN_B,
    };
    knob_handle_t s_knob = iot_knob_create(&cfg);
    if (s_knob) {
        iot_knob_register_cb(s_knob, KNOB_LEFT, knob_left_cb, NULL);
        iot_knob_register_cb(s_knob, KNOB_RIGHT, knob_right_cb, NULL);
        Serial.println("Rotary encoder initialized successfully.");
    } else {
        Serial.println("Failed to initialize rotary encoder.");
    }

    // Create the one-shot timer for debouncing BLE writes. 1000ms delay.
    // It is a "one-shot" (pdFALSE) timer that will only fire once after being reset.
    ble_write_timer = xTimerCreate("bleWriteTimer",     // Name
                                   pdMS_TO_TICKS(1000), // 1000ms period
                                   pdFALSE,             // One-shot timer
                                   (void*)0,            // Timer ID
                                   ble_write_timer_callback); // Callback function

    if (ble_write_timer == NULL) {
        Serial.println("Failed to create ble_write_timer!");
    } else {
        Serial.println("BLE write debounce timer created.");
    }
}

