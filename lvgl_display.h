/*
 * Header for the LVGL display module.
 *
 * Declares the functions for initializing the UI and updating its elements.
 * Added update_battery_status function.
 * Added reset_inactivity_timer function.
 */
#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

#include <stdint.h>
#include "app_events.h" // Include status definitions

// Forward declare lv_obj_t type instead of including the full header
struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

// Enum for selectable controls on HA screen
typedef enum {
    HA_CONTROL_NONE,
    HA_CONTROL_MODE,
    HA_CONTROL_PREINF_TIME,
    HA_CONTROL_TEMP,
    HA_CONTROL_STEAM,
    HA_CONTROL_BACKFLUSH
} ha_control_t;


#ifdef __cplusplus
extern "C" {
#endif

// LVGL UI Initialization
void lvgl_display_init();

// Shot Stopper Screen Updates
void update_display_value(int8_t weight);
void show_verification_checkmark();
void hide_verification_checkmark();
void update_ble_status(ble_status_t status);
void update_battery_status(uint8_t percentage);

// Home Assistant Screen Updates
void update_ha_power_switch_ui(bool state);
void update_ha_mode_ui(int8_t mode_index);
void update_ha_temperature_ui(float temp);
void update_ha_steam_power_ui(int power);
void update_ha_preinfusion_time_ui(float time);
void update_ha_last_shot_ui(float seconds);

// Functions called by Encoder/Input handlers
void ha_ui_handle_encoder_turn(int8_t direction);
void ha_ui_reset_deselection_timer();
void reset_inactivity_timer(); // New function for brightness

// Expose screen pointers for encoder logic
extern lv_obj_t* screen_shot_stopper;
extern lv_obj_t* screen_ha;


#ifdef __cplusplus
}
#endif

#endif // LVGL_DISPLAY_H

