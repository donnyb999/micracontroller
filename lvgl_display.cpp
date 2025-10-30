/*
 * LVGL display implementation for the shotStopper controller.
 *
 * Added battery percentage indicator to the Shot Stopper screen,
 * updated periodically via an LVGL timer.
 * Added inactivity timer for screen dimming and backlight off.
 * Implemented moving average filter for battery readings to stabilize percentage.
 * Preset buttons now use the debounce timer before triggering BLE write.
 */
#include "lvgl_display.h"
#include "ble_client.h"
#include "encoder.h" // Needed for extern ble_write_timer
#include "app_events.h"
#include "home_assistant.h"
#include "lcd_bl_pwm_bsp.h" // Include backlight functions
#include <lvgl.h>
#include <cstdio>
#include <Arduino.h> // Required for analogReadMilliVolts, FreeRTOS timers
#include <Preferences.h> // Needed for preset saving/loading

// --- Brightness / Inactivity ---
#define INACTIVITY_TIMEOUT_DIM_MS 30000 // 30 seconds to dim
#define INACTIVITY_TIMEOUT_OFF_MS 30000 // Another 30 seconds (60 total) to off
#define BRIGHTNESS_HIGH 178 // ~70% (255 * 0.7)
#define BRIGHTNESS_DIM 51   // ~20% (255 * 0.2)
#define BRIGHTNESS_OFF 0    // 0%

static lv_timer_t* inactivity_timer = NULL;
static uint8_t current_brightness_level = BRIGHTNESS_HIGH; // Track current level

// --- Battery Monitoring ---
#define BATTERY_ADC_PIN 1   // GPIO1 for battery voltage divider
#define BATTERY_MAX_MV 4200 // Max voltage (4.2V) in millivolts
#define BATTERY_MIN_MV 3000 // Min voltage (3.0V) in millivolts (conservative)
#define BATTERY_READING_COUNT 5 // Number of readings to average

static uint32_t battery_readings[BATTERY_READING_COUNT];
static int battery_reading_index = 0;
static uint32_t battery_reading_sum = 0;
static bool battery_filter_initialized = false;


// --- Global State & UI Objects ---
lv_obj_t* screen_shot_stopper;
lv_obj_t* screen_ha;

// HA Screen State
static ha_control_t selected_ha_control = HA_CONTROL_NONE;
static lv_timer_t* deselection_timer = NULL;
static lv_style_t style_selected;
static lv_obj_t* selected_ui_obj = NULL;
static lv_timer_t* power_long_press_timer = NULL; // Timer for power button

// HA Screen UI Elements
static lv_obj_t* ha_on_off_btn;
static lv_obj_t* ha_mode_cont;
static lv_obj_t* ha_mode_label;
static lv_obj_t* ha_preinf_time_cont;
static lv_obj_t* ha_preinf_time_label;
static lv_obj_t* ha_temp_cont;
static lv_obj_t* ha_temp_label;
static lv_obj_t* ha_steam_cont;
static lv_obj_t* ha_steam_label;
static lv_obj_t* ha_last_shot_label;
static lv_obj_t* ha_backflush_cont;

// HA Value Cache
static int8_t current_mode_index = 0;
static const char* PREINFUSION_MODES[] = {"Pre-brew", "Pre-infusion", "Disabled"};
static float current_temp = 93.0;
static int8_t current_steam = 3;
static float current_preinfusion_time = 0.8;

// Shot Stopper Screen Globals
lv_obj_t * weight_label;
lv_obj_t * checkmark_label;
lv_obj_t* preset_btns[3];
lv_obj_t* preset_labels[3];
lv_obj_t* title_label;
static lv_obj_t* ble_status_label; // For BLE status
static lv_obj_t* battery_label;    // For Battery status
static int8_t preset_weights[3] = {36, 40, 45}; // Default values if none are saved
static const char* PRESET_KEYS[3] = {"p1", "p2", "p3"};
extern Preferences preferences; // Declare external Preferences object

// Forward Declarations
void create_ha_screen(lv_obj_t* parent);
void create_shot_stopper_screen(lv_obj_t* parent);
static void deselect_all_ha_controls();
static void swipe_event_cb(lv_event_t* e);
void update_preset_label(uint8_t index); // Declare for use in create screen
void load_presets(); // Declare for use in create screen
static void battery_timer_cb(lv_timer_t* timer); // Battery timer callback
static void inactivity_timer_cb(lv_timer_t* timer); // Inactivity timer callback
void reset_inactivity_timer(); // Declaration for internal use


// --- Timer & Encoder Logic ---

// Timer callback to deselect the active HA control after 5 seconds of inactivity
static void deselect_timer_cb(lv_timer_t* timer) {
    Serial.println("Deselection timer fired.");
    deselect_all_ha_controls();
    lv_timer_del(deselection_timer);
    deselection_timer = NULL;
}

// Resets the 5-second HA deselection timer.
void ha_ui_reset_deselection_timer() {
    if (deselection_timer) {
        lv_timer_reset(deselection_timer);
    }
}

// Central handler for all encoder events on the Home Assistant screen
void ha_ui_handle_encoder_turn(int8_t direction) {
    reset_inactivity_timer(); // Reset brightness on encoder turn
    if (selected_ha_control == HA_CONTROL_NONE) return;

    ha_ui_reset_deselection_timer(); // Reset HA control selection timer

    static int8_t mode_counter = 0;
    static int8_t steam_counter = 0;
    static int8_t backflush_counter = 0;

    switch (selected_ha_control) {
        case HA_CONTROL_MODE:
            mode_counter += direction;
            if (abs(mode_counter) >= 3) {
                current_mode_index = (current_mode_index + (mode_counter > 0 ? 1 : -1) + 3) % 3;
                ha_set_preinfusion_mode(current_mode_index);
                update_ha_mode_ui(current_mode_index);
                mode_counter = 0;
            }
            break;
        case HA_CONTROL_PREINF_TIME:
            current_preinfusion_time += (float)direction * 0.1;
            if (current_preinfusion_time < 0.0) current_preinfusion_time = 0.0;
            ha_set_preinfusion_time(current_preinfusion_time);
            update_ha_preinfusion_time_ui(current_preinfusion_time);
            break;
        case HA_CONTROL_TEMP:
            current_temp += (float)direction * 0.1;
            ha_set_target_temperature(current_temp);
            update_ha_temperature_ui(current_temp);
            break;
        case HA_CONTROL_STEAM:
            steam_counter += direction;
            if (abs(steam_counter) >= 3) {
                current_steam += (steam_counter > 0 ? 1 : -1);
                if (current_steam < 1) current_steam = 1;
                if (current_steam > 3) current_steam = 3;
                ha_set_steam_power(current_steam);
                update_ha_steam_power_ui(current_steam);
                steam_counter = 0;
            }
            break;
        case HA_CONTROL_BACKFLUSH:
            backflush_counter += direction;
             if (abs(backflush_counter) >= 3) {
                ha_trigger_backflush();
                Serial.println("Backflush activated via encoder.");
                deselect_all_ha_controls();
                backflush_counter = 0;
            }
            break;
        default:
            break;
    }
}

// --- Brightness Inactivity Logic ---

// Callback for the main inactivity timer
static void inactivity_timer_cb(lv_timer_t* timer) {
    Serial.printf("Inactivity timer fired. Current brightness level: %d\n", current_brightness_level);
    if (current_brightness_level == BRIGHTNESS_HIGH) {
        Serial.println("Dimming screen to 20%");
        setUpdutySubdivide(BRIGHTNESS_DIM);
        current_brightness_level = BRIGHTNESS_DIM;
        // Keep timer running, next timeout will turn screen off
        lv_timer_set_period(timer, INACTIVITY_TIMEOUT_OFF_MS); // Set period for next stage
        lv_timer_reset(timer); // Reset countdown for the next stage
    } else if (current_brightness_level == BRIGHTNESS_DIM) {
        Serial.println("Turning screen off");
        setUpdutySubdivide(BRIGHTNESS_OFF);
        current_brightness_level = BRIGHTNESS_OFF;
        lv_timer_pause(timer); // Pause timer when screen is off
    }
}

// Function to reset brightness to high and restart the inactivity timer
void reset_inactivity_timer() {
    if (current_brightness_level != BRIGHTNESS_HIGH) {
        Serial.println("Activity detected, setting brightness to high.");
        setUpdutySubdivide(BRIGHTNESS_HIGH);
        current_brightness_level = BRIGHTNESS_HIGH;
    }
    if (inactivity_timer) {
        // Serial.println("Resetting inactivity timer."); // Debug log if needed
        lv_timer_set_period(inactivity_timer, INACTIVITY_TIMEOUT_DIM_MS); // Reset period to initial dim timeout
        lv_timer_reset(inactivity_timer); // Reset countdown
        lv_timer_resume(inactivity_timer); // Ensure it's running
    } else {
        Serial.println("Error: Inactivity timer not initialized!");
    }
}


// --- UI Creation & Event Handlers ---

// Deselects any active HA control and removes its highlight
static void deselect_all_ha_controls() {
    if (selected_ui_obj) {
        lv_obj_remove_style(selected_ui_obj, &style_selected, 0);
        selected_ui_obj = NULL;
    }
    selected_ha_control = HA_CONTROL_NONE;
}

// Generic event handler for tapping a selectable HA control
static void ha_select_event_cb(lv_event_t* e) {
    reset_inactivity_timer(); // Reset brightness on touch
    deselect_all_ha_controls();

    ha_control_t control_type = (ha_control_t)(intptr_t)lv_event_get_user_data(e);
    selected_ui_obj = (lv_obj_t*)lv_event_get_target(e); // Cast to lv_obj_t*

    selected_ha_control = control_type;
    lv_obj_add_style(selected_ui_obj, &style_selected, 0);
    Serial.printf("Selected control: %d\n", control_type);

    if (deselection_timer) {
        lv_timer_reset(deselection_timer);
    } else {
        deselection_timer = lv_timer_create(deselect_timer_cb, 5000, NULL);
        lv_timer_set_repeat_count(deselection_timer, 1);
    }
}

// Manual long press implementation for power button
static void power_long_press_timer_cb(lv_timer_t* timer) {
    Serial.println("Power button long-press timer fired.");
    ha_set_machine_power(!lv_obj_has_state(ha_on_off_btn, LV_STATE_CHECKED));
    power_long_press_timer = NULL; // Timer is one-shot, clear its handle
}

static void ha_power_press_event_cb(lv_event_t* e) {
    reset_inactivity_timer(); // Reset brightness on touch
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        Serial.println("Power button pressed, starting 2s timer.");
        if (power_long_press_timer) {
            lv_timer_del(power_long_press_timer);
        }
        power_long_press_timer = lv_timer_create(power_long_press_timer_cb, 2000, NULL);
        lv_timer_set_repeat_count(power_long_press_timer, 1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (power_long_press_timer) {
            Serial.println("Power button released, deleting timer.");
            lv_timer_del(power_long_press_timer);
            power_long_press_timer = NULL;
        }
    }
}

// Event handler for screen swiping
static void swipe_event_cb(lv_event_t* e) {
    reset_inactivity_timer(); // Reset brightness on swipe
    lv_indev_t * indev = lv_indev_active();
    if(indev == NULL) return; // Should not happen with gestures

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    Serial.printf("Swipe event detected! Direction: %d\n", dir); // DEBUG

    if (dir == LV_DIR_TOP) {
        Serial.println("Swiped UP - Loading HA screen."); // DEBUG
        lv_scr_load_anim(screen_ha, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, false);
    } else if (dir == LV_DIR_BOTTOM) {
        Serial.println("Swiped DOWN - Loading Shot Stopper screen."); // DEBUG
        lv_scr_load_anim(screen_shot_stopper, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 300, 0, false);
    } else {
         Serial.println("Swipe direction not vertical."); // DEBUG
    }
}

// --- Home Assistant Screen Creation ---
void create_ha_screen(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x343a40), LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE); // Ensure scrolling is off

    lv_style_init(&style_selected);
    lv_style_set_border_color(&style_selected, lv_color_hex(0x89cff0));
    lv_style_set_border_width(&style_selected, 3);
    lv_style_set_border_side(&style_selected, LV_BORDER_SIDE_FULL);

    // --- Reworked circular layout for 360x360 display ---
    const int btn_size = 80;
    const int radius = 130;

    // Helper to create a circular button container.
    // Note: This returns a generic lv_obj_t* but creates an lv_btn. This is fine.
    // The caller is responsible for creating the label inside.
    auto create_circular_container = [&](ha_control_t ctrl_type) -> lv_obj_t* {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, btn_size, btn_size);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_event_cb(btn, ha_select_event_cb, LV_EVENT_CLICKED, (void*)ctrl_type);
        return btn;
    };

    // 1. Power Button (Top)
    ha_on_off_btn = lv_btn_create(parent);
    lv_obj_set_size(ha_on_off_btn, btn_size, btn_size);
    lv_obj_set_style_radius(ha_on_off_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(ha_on_off_btn, LV_ALIGN_CENTER, 0, -radius);
    lv_obj_add_flag(ha_on_off_btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(ha_on_off_btn, ha_power_press_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_t* on_off_label = lv_label_create(ha_on_off_btn);
    lv_label_set_text(on_off_label, LV_SYMBOL_POWER);
    lv_obj_center(on_off_label);

    // 2. Mode (Top-Right)
    ha_mode_cont = create_circular_container(HA_CONTROL_MODE);
    lv_obj_align(ha_mode_cont, LV_ALIGN_CENTER, (int)(radius * 0.866f), (int)(-radius * 0.5f));
    ha_mode_label = lv_label_create(ha_mode_cont);
    lv_obj_center(ha_mode_label);

    // 3. Pre-infusion Time (Bottom-Right)
    ha_preinf_time_cont = create_circular_container(HA_CONTROL_PREINF_TIME);
    lv_obj_align(ha_preinf_time_cont, LV_ALIGN_CENTER, (int)(radius * 0.866f), (int)(radius * 0.5f));
    ha_preinf_time_label = lv_label_create(ha_preinf_time_cont);
    lv_obj_center(ha_preinf_time_label);

    // 4. Backflush (Bottom)
    ha_backflush_cont = create_circular_container(HA_CONTROL_BACKFLUSH);
    lv_obj_align(ha_backflush_cont, LV_ALIGN_CENTER, 0, radius);
    lv_obj_t* backflush_label = lv_label_create(ha_backflush_cont);
    lv_label_set_text(backflush_label, LV_SYMBOL_REFRESH);
    lv_obj_center(backflush_label);

    // 5. Steam (Bottom-Left)
    ha_steam_cont = create_circular_container(HA_CONTROL_STEAM);
    lv_obj_align(ha_steam_cont, LV_ALIGN_CENTER, (int)(-radius * 0.866f), (int)(radius * 0.5f));
    ha_steam_label = lv_label_create(ha_steam_cont);
    lv_obj_center(ha_steam_label);

    // 6. Temp (Top-Left)
    ha_temp_cont = create_circular_container(HA_CONTROL_TEMP);
    lv_obj_align(ha_temp_cont, LV_ALIGN_CENTER, (int)(-radius * 0.866f), (int)(-radius * 0.5f));
    ha_temp_label = lv_label_create(ha_temp_cont);
    lv_obj_center(ha_temp_label);

    // Last Shot Label (Center)
    lv_obj_t* last_shot_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(last_shot_cont); // Make it transparent so it doesn't block view
    lv_obj_set_size(last_shot_cont, 120, 50);
    lv_obj_align(last_shot_cont, LV_ALIGN_CENTER, 0, 0);
    ha_last_shot_label = lv_label_create(last_shot_cont);
    lv_obj_center(ha_last_shot_label);
    lv_obj_set_style_text_align(ha_last_shot_label, LV_TEXT_ALIGN_CENTER, 0);

    update_ha_mode_ui(current_mode_index);
    update_ha_preinfusion_time_ui(current_preinfusion_time);
    update_ha_temperature_ui(current_temp);
    update_ha_steam_power_ui(current_steam);
    update_ha_last_shot_ui(0);
}

// --- Battery Timer Callback ---
static void battery_timer_cb(lv_timer_t* timer) {
    // Read voltage (mV). The divider is 100k+100k, so Vbat = V_adc * 2
    uint32_t current_reading_mv = analogReadMilliVolts(BATTERY_ADC_PIN) * 2;

    // Initialize filter on first run
    if (!battery_filter_initialized) {
        for (int i = 0; i < BATTERY_READING_COUNT; i++) {
            battery_readings[i] = current_reading_mv;
        }
        battery_reading_sum = current_reading_mv * BATTERY_READING_COUNT;
        battery_filter_initialized = true;
    } else {
        // Subtract the oldest reading from the sum
        battery_reading_sum -= battery_readings[battery_reading_index];
        // Add the new reading to the sum
        battery_reading_sum += current_reading_mv;
        // Store the new reading, replacing the oldest
        battery_readings[battery_reading_index] = current_reading_mv;
    }

    // Calculate the average voltage
    uint32_t average_mv = battery_reading_sum / BATTERY_READING_COUNT;

    // Move to the next index
    battery_reading_index = (battery_reading_index + 1) % BATTERY_READING_COUNT;


    // Calculate percentage based on the AVERAGE voltage
    int percentage = (int)(((float)average_mv - BATTERY_MIN_MV) / (BATTERY_MAX_MV - BATTERY_MIN_MV) * 100.0f);

    // Clamp percentage between 0 and 100
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    // Serial.printf("Battery mV (Avg): %lu, Percentage: %d%%\n", average_mv, percentage); // Debug

    // Update the UI
    update_battery_status((uint8_t)percentage);
}


// --- Main Initialization ---
void lvgl_display_init() {
    // Note: lv_init() is called in lcd_lvgl_Init() in lcd_bsp.c

    screen_shot_stopper = lv_obj_create(NULL);
    screen_ha = lv_obj_create(NULL);

    create_shot_stopper_screen(screen_shot_stopper);
    create_ha_screen(screen_ha);

    // Removed GESTURE_BUBBLE flags
    // lv_obj_add_flag(screen_shot_stopper, LV_OBJ_FLAG_GESTURE_BUBBLE);
    // lv_obj_add_flag(screen_ha, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Add event callbacks directly to screens
    lv_obj_add_event_cb(screen_shot_stopper, swipe_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen_ha, swipe_event_cb, LV_EVENT_GESTURE, NULL);

    lv_disp_load_scr(screen_shot_stopper);

    // Create a timer to update the battery status every 5 seconds (5000 ms)
    lv_timer_create(battery_timer_cb, 5000, NULL);
    // Call it once immediately to show initial status
    battery_timer_cb(NULL); // Call initial update
    Serial.println("Battery update timer created."); // Confirm timer creation

    // Create the main inactivity timer, initially set for the first dim timeout
    inactivity_timer = lv_timer_create(inactivity_timer_cb, INACTIVITY_TIMEOUT_DIM_MS, NULL);
    Serial.println("Inactivity timer created.");

}

// --- HA UI Update Functions ---
void update_ha_power_switch_ui(bool state) {
    if (ha_on_off_btn) {
        state ? lv_obj_add_state(ha_on_off_btn, LV_STATE_CHECKED) : lv_obj_clear_state(ha_on_off_btn, LV_STATE_CHECKED);
    }
}
void update_ha_mode_ui(int8_t mode_index) {
    current_mode_index = mode_index;
    if (ha_mode_label) {
        lv_label_set_text(ha_mode_label, PREINFUSION_MODES[current_mode_index]);
    }
}
void update_ha_temperature_ui(float temp) {
    current_temp = temp;
    if (ha_temp_label) {
        lv_label_set_text_fmt(ha_temp_label, "%.1f C", current_temp);
    }
}
void update_ha_steam_power_ui(int power) {
    current_steam = power;
    if (ha_steam_label) {
        lv_label_set_text_fmt(ha_steam_label, "Pwr: %d", current_steam);
    }
}
void update_ha_preinfusion_time_ui(float time) {
    current_preinfusion_time = time;
    if (ha_preinf_time_label) {
        lv_label_set_text_fmt(ha_preinf_time_label, "%.1fs", current_preinfusion_time);
    }
}
void update_ha_last_shot_ui(float seconds) {
    if (ha_last_shot_label) {
        lv_label_set_text_fmt(ha_last_shot_label, "Last: %.1fs", seconds);
    }
}

// --- Shot Stopper Screen Code ---

// --- Shot Stopper Preset Button Functions ---
// (Restored implementations)

// Update the label for a specific preset button
void update_preset_label(uint8_t index) {
    if (index < 3 && preset_labels[index]) {
        char buf[16];
        sprintf(buf, "%d g", preset_weights[index]);
        lv_label_set_text(preset_labels[index], buf);
    }
}

// Load presets from NVS and update the UI
void load_presets() {
    Serial.println("Loading presets from memory...");
    if (!preferences.isKey("p1")) { // Check if preferences have been initialized
         Serial.println("Preferences not found, using defaults.");
    }
    for (int i = 0; i < 3; i++) {
        // Get the value from preferences, using the hardcoded default if it's not found
        preset_weights[i] = preferences.getChar(PRESET_KEYS[i], preset_weights[i]);
        update_preset_label(i); // Update the label after loading
        Serial.printf("  Preset %d loaded with value: %d g\n", i + 1, preset_weights[i]);
    }
}

// Event handler for preset buttons (short click and long press)
static void preset_event_cb(lv_event_t * e) {
    reset_inactivity_timer(); // Reset brightness on touch
    Serial.println("Preset button callback fired!"); // DEBUG: Confirm callback fires
    lv_event_code_t code = lv_event_get_code(e);
    intptr_t preset_index = (intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_SHORT_CLICKED) {
        Serial.printf("Preset %ld tapped. Loading weight: %d g\n", preset_index + 1, preset_weights[preset_index]);

        target_weight = preset_weights[preset_index];

        hide_verification_checkmark();
        update_display_value(target_weight); // Update UI immediately

        // Reset the debounce timer instead of writing directly
        if (ble_write_timer != NULL) {
             Serial.printf("Resetting BLE write timer for preset %ld\n", preset_index + 1);
            xTimerReset(ble_write_timer, portMAX_DELAY); // Reset timer to 1 second
        } else {
             Serial.println("Error: ble_write_timer is NULL!");
        }
        // write_target_weight(target_weight); // REMOVED - Timer callback handles this

    } else if (code == LV_EVENT_LONG_PRESSED) {
        Serial.printf("Preset %ld long-pressed. Saving current weight: %d g\n", preset_index + 1, target_weight);

        preset_weights[preset_index] = target_weight;
        update_preset_label(preset_index);

        // Save the new preset value to non-volatile storage
        preferences.putChar(PRESET_KEYS[preset_index], target_weight);
        Serial.printf("Preset %ld saved to memory.\n", preset_index + 1);
    }
}

// Create the Shot Stopper Screen UI
void create_shot_stopper_screen(lv_obj_t* parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE); // Explicitly disable scroll

    ble_status_label = lv_label_create(parent);
    // Use symbol as requested
    lv_label_set_text(ble_status_label, LV_SYMBOL_BLUETOOTH);
    // Use lv_font_montserrat_24 for the BLE icon text as requested
    lv_obj_set_style_text_font(ble_status_label, &lv_font_montserrat_24, 0);
    // Use lv_color_make for initial grey color
    lv_obj_set_style_text_color(ble_status_label, lv_color_make(128, 128, 128), 0); // Default grey
    lv_obj_align(ble_status_label, LV_ALIGN_TOP_MID, 0, 10);

    title_label = lv_label_create(parent);
    lv_label_set_text(title_label, "Target Weight (g)");
    // Use lv_font_montserrat_24 as requested by user
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 50); // Adjusted Y position

    weight_label = lv_label_create(parent);
    lv_label_set_text(weight_label, "...");
    // Use lv_font_montserrat_48 if enabled in lv_conf.h, otherwise fallback
    // NOTE: Ensure LV_FONT_MONTSERRAT_48 is set to 1 in lv_conf.h
    #if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_48, 0);
    #else
    // Fallback if 48 is not enabled
    lv_obj_set_style_text_font(weight_label, &lv_font_montserrat_24, 0); // Use 24 as a fallback
    #warning "LV_FONT_MONTSERRAT_48 not enabled in lv_conf.h, using smaller font for weight."
    #endif
    lv_obj_set_style_text_color(weight_label, lv_color_white(), 0);
    lv_obj_align(weight_label, LV_ALIGN_CENTER, 0, -30); // Keep centered

    checkmark_label = lv_label_create(parent);
    lv_label_set_text(checkmark_label, LV_SYMBOL_OK);
    // Use lv_font_montserrat_24 as requested by user
    lv_obj_set_style_text_font(checkmark_label, &lv_font_montserrat_24, 0); // Size matches title
    lv_obj_set_style_text_color(checkmark_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align_to(checkmark_label, weight_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_add_flag(checkmark_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* preset_container = lv_obj_create(parent);
    lv_obj_remove_style_all(preset_container);
    lv_obj_set_size(preset_container, 320, 80);
    lv_obj_align(preset_container, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_flex_flow(preset_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(preset_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < 3; i++) {
        preset_btns[i] = lv_btn_create(preset_container);
        lv_obj_set_size(preset_btns[i], 90, 60);
        lv_obj_add_event_cb(preset_btns[i], preset_event_cb, LV_EVENT_ALL, (void*)(intptr_t)i);
        // Use lv_palette_main(LV_PALETTE_GREY) for background
        lv_obj_set_style_bg_color(preset_btns[i], lv_palette_main(LV_PALETTE_GREY), 0); // Standard grey

        preset_labels[i] = lv_label_create(preset_btns[i]);
        lv_obj_center(preset_labels[i]);
        // Set font for preset labels
        lv_obj_set_style_text_font(preset_labels[i], &lv_font_montserrat_24, 0); // Consistent size requested by user
        // Set preset label text color
        lv_obj_set_style_text_color(preset_labels[i], lv_color_white(), 0); // White text
    }

    // Create Battery Label
    battery_label = lv_label_create(parent);
    lv_label_set_text(battery_label, "Batt: --%");
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0); // Use a smaller font
    lv_obj_set_style_text_color(battery_label, lv_color_white(), 0);
    lv_obj_align(battery_label, LV_ALIGN_BOTTOM_MID, 0, -20); // Position bottom


    // Load presets after creating labels
    load_presets();
}

// Update the main weight display
void update_display_value(int8_t weight) {
    if(weight_label) {
        lv_label_set_text_fmt(weight_label, "%d g", weight);
        Serial.printf("Display updated to: %d g\n", weight);
    }
}

// Show/Hide checkmark
void show_verification_checkmark() {
    if (checkmark_label) {
        lv_obj_clear_flag(checkmark_label, LV_OBJ_FLAG_HIDDEN);
        Serial.println("Checkmark displayed.");
    }
}
void hide_verification_checkmark() {
    if (checkmark_label) {
        lv_obj_add_flag(checkmark_label, LV_OBJ_FLAG_HIDDEN);
        Serial.println("Checkmark hidden.");
    }
}

// Update BLE status icon color
void update_ble_status(ble_status_t status) {
    if (!ble_status_label) return;
    // Use lv_color_make for status colors
    switch (status) {
        case BLE_STATUS_DISCONNECTED:
            lv_obj_set_style_text_color(ble_status_label, lv_color_make(128, 128, 128), 0); // Grey
            break;
        case BLE_STATUS_CONNECTING:
            lv_obj_set_style_text_color(ble_status_label, lv_color_make(0, 123, 255), 0); // Blue
            break;
        case BLE_STATUS_CONNECTED:
            lv_obj_set_style_text_color(ble_status_label, lv_color_make(40, 167, 69), 0); // Green
            break;
        case BLE_STATUS_FAILED:
            lv_obj_set_style_text_color(ble_status_label, lv_color_make(220, 53, 69), 0); // Red
            break;
    }
}

// New function to update battery status label
void update_battery_status(uint8_t percentage) {
    if (battery_label) {
        // Use battery symbol if available in font, otherwise just text
        #ifdef LV_SYMBOL_BATTERY_FULL // Check if symbol is defined
            // Simple logic: show different symbols based on percentage
            if (percentage > 85) {
                lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_FULL " %d%%", percentage);
                lv_obj_set_style_text_color(battery_label, lv_color_make(0, 255, 0), 0);
            } else if (percentage > 60) {
                 lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_3 " %d%%", percentage);
                 lv_obj_set_style_text_color(battery_label, lv_color_make(123, 255, 0), 0);
            } else if (percentage > 30) {
                 lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_2 " %d%%", percentage);
                 lv_obj_set_style_text_color(battery_label, lv_color_make(217, 255, 0), 0);
            } else if (percentage > 15) {
                 lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_1 " %d%%", percentage);
                 lv_obj_set_style_text_color(battery_label, lv_color_make(255, 157, 0), 0);
            } else {
                 lv_label_set_text_fmt(battery_label, LV_SYMBOL_BATTERY_EMPTY " %d%%", percentage);
                 lv_obj_set_style_text_color(battery_label, lv_color_make(255, 0, 0), 0);
            }
        #else // Fallback if symbols aren't defined/enabled
            lv_label_set_text_fmt(battery_label, "Batt: %d%%", percentage);
        #endif
       // Serial.printf("Updating battery label: %d%%\n", percentage); // Debug
    } else {
       // Serial.println("Battery label object does not exist!"); // Debug
    }
}

