/*
 * Home Assistant integration logic.
 *
 * Handles connecting to WiFi, MQTT broker, and managing
 * Home Assistant entities (switch, select, button, numbers, sensor).
 * Corrected ambiguous setState call, HASelect options format,
 * and removed inaccessible variables from publish function.
 * Corrected HANumeric::toInt() to toInt8().
 */

#include <WiFi.h>
#include <ArduinoHA.h> // ArduinoHA library
#include "secrets.h"    // For credentials - MAKE SURE MQTT_SERVER IS DEFINED HERE!
#include "home_assistant.h"
#include "lvgl_display.h" // To update UI based on HA commands

// WiFi and MQTT credentials (from secrets.h)
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* mqtt_server = MQTT_SERVER; // Ensure this is defined in secrets.h
const int mqtt_port = MQTT_PORT;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;

WiFiClient client;
HADevice device("esp32_linea_micra_ctrl"); // Unique device name
HAMqtt mqtt(client, device);

// Define HA entities
HASwitch machinePower("linea_micra_power"); // Unique ID for the power switch
HASelect preinfusionMode("linea_micra_mode"); // Unique ID for mode select
HASwitch backflushSwitch("linea_micra_backflush"); // Changed to HASwitch
HANumber targetTemperature("linea_micra_target_temp", HANumber::PrecisionP1); // Unique ID, PrecisionP1 for 0.1
HANumber steamPower("linea_micra_steam_power", HANumber::PrecisionP0); // Unique ID, PrecisionP0 for integer
HANumber preinfusionTime("linea_micra_preinfusion_time", HANumber::PrecisionP1); // Unique ID, PrecisionP1 for 0.1
HANumber lastShotDuration("linea_micra_last_shot", HANumber::PrecisionP1); // Changed to HANumber to receive updates

// Preinfusion mode options - Not used directly by setOptions anymore
// const char* modes[] = {"Pre-brew", "Pre-infusion", "Disabled"};

// --- Callback Functions for HA Commands ---

void onPowerSwitchCommand(bool state, HASwitch* sender) {
    Serial.printf("Received power command from HA: %s\n", state ? "ON" : "OFF");
    update_ha_power_switch_ui(state);
    // You'll need an automation in HA to link this switch to the actual machine power control
}

void onModeSelectCommand(int8_t index, HASelect* sender) {
    // Assuming modes array has 3 elements
    const char* modes_lookup[] = {"Pre-brew", "Pre-infusion", "Disabled"}; // Local lookup
    if (index >= 0 && index < 3) {
        Serial.printf("Received mode command from HA: %s (index %d)\n", modes_lookup[index], index);
        sender->setCurrentState(index); // Acknowledge the state change back to HA
        update_ha_mode_ui(index);
    } else {
        Serial.printf("Received invalid mode index from HA: %d\n", index);
    }
}

// Callback for the backflush switch (likely won't be called if controlled from ESP)
void onBackflushCommand(bool state, HASwitch* sender) {
     Serial.printf("Received backflush command from HA: %s\n", state ? "ON" : "OFF");
    // This callback might not be strictly needed if only triggering from ESP,
    // but good practice to include.
    // The HA automation should turn this switch off automatically.
}


void onTargetTempCommand(HANumeric number, HANumber* sender) {
    float temp = number.toFloat();
    Serial.printf("Received target temperature command from HA: %.1f\n", temp);
    sender->setState(temp); // Acknowledge state back to HA
    update_ha_temperature_ui(temp);
}

void onSteamPowerCommand(HANumeric number, HANumber* sender) {
    // Corrected: Use toInt8() as suggested by the compiler
    int8_t power = number.toInt8();

    if (power >= 1 && power <= 3) {
        Serial.printf("Received steam power command from HA: %d\n", power);
        // Corrected: Explicitly cast to int8_t to resolve ambiguity
        sender->setState((int8_t)power); // Acknowledge state back to HA
        update_ha_steam_power_ui(power);
    } else {
         // Corrected: Use toInt8() in the logging statement as well
         Serial.printf("Received invalid steam power value from HA: %d\n", (int)number.toInt8()); // Log original value
    }
}

void onPreinfusionTimeCommand(HANumeric number, HANumber* sender) {
    float time = number.toFloat();
    Serial.printf("Received preinfusion time command from HA: %.1f\n", time);
    sender->setState(time); // Acknowledge state back to HA
    update_ha_preinfusion_time_ui(time);
}

// Callback for when HA sends updates FOR the last shot duration
void onLastShotUpdate(HANumeric number, HANumber* sender) {
    float duration = number.toFloat();
    Serial.printf("Received last shot update from HA: %.1fs\n", duration);
    // No need to set state back to HA for a sensor-like input
    update_ha_last_shot_ui(duration);
}

// --- Initialization and Loop ---

void ha_init() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Set device info (optional)
    device.setName("Linea Micra Controller");
    device.setManufacturer("YourName/DIY");
    device.setModel("ESP32-S3");
    device.setSoftwareVersion("1.0.0");

    // Configure entities
    machinePower.setName("Machine Power");
    machinePower.setIcon("mdi:power");
    machinePower.onCommand(onPowerSwitchCommand);

    preinfusionMode.setName("Pre-infusion Mode");
    preinfusionMode.setIcon("mdi:water-opacity");
    // Corrected: Provide options as a comma-separated string
    preinfusionMode.setOptions("Pre-brew;Pre-infusion;Disabled");
    preinfusionMode.onCommand(onModeSelectCommand);

    backflushSwitch.setName("Backflush");
    backflushSwitch.setIcon("mdi:refresh");
    backflushSwitch.onCommand(onBackflushCommand); // Add callback if needed

    targetTemperature.setName("Target Temperature");
    targetTemperature.setIcon("mdi:thermometer");
    targetTemperature.setUnitOfMeasurement("°C");
    targetTemperature.setMode(HANumber::ModeBox); // Or ModeSlider
    targetTemperature.setMin(85.0);
    targetTemperature.setMax(100.0);
    targetTemperature.setStep(0.1);
    targetTemperature.onCommand(onTargetTempCommand);

    steamPower.setName("Steam Power");
    steamPower.setIcon("mdi:creation"); // Example icon
    steamPower.setUnitOfMeasurement(""); // No unit
    steamPower.setMode(HANumber::ModeBox); // Or ModeSlider
    steamPower.setMin(1);
    steamPower.setMax(3);
    steamPower.setStep(1);
    steamPower.onCommand(onSteamPowerCommand);

    preinfusionTime.setName("Pre-infusion Time");
    preinfusionTime.setIcon("mdi:timer-sand");
    preinfusionTime.setUnitOfMeasurement("s");
    preinfusionTime.setMode(HANumber::ModeBox); // Or ModeSlider
    preinfusionTime.setMin(0.0);
    preinfusionTime.setMax(10.0); // Adjust max as needed
    preinfusionTime.setStep(0.1);
    preinfusionTime.onCommand(onPreinfusionTimeCommand);

    lastShotDuration.setName("Last Shot Duration");
    lastShotDuration.setIcon("mdi:timer-outline");
    lastShotDuration.setUnitOfMeasurement("s");
    // No setMode needed for sensor-like number
    lastShotDuration.setMin(0.0);
    lastShotDuration.setStep(0.1);
    lastShotDuration.onCommand(onLastShotUpdate); // Use onCommand to receive updates


    Serial.println("Connecting to MQTT broker...");
    mqtt.begin(mqtt_server, mqtt_user, mqtt_password);

    Serial.println("HA Init Complete.");
}

void ha_loop() {
    mqtt.loop();
}

// --- Functions to Send Updates TO Home Assistant ---

void ha_set_machine_power(bool state) {
    machinePower.setState(state);
}

void ha_set_preinfusion_mode(int8_t index) {
     if (index >= 0 && index < 3) {
        preinfusionMode.setCurrentState(index);
     }
}

void ha_set_target_temperature(float temp) {
    targetTemperature.setState(temp);
}

void ha_set_steam_power(int8_t power) {
    if (power >= 1 && power <= 3) {
        // Corrected: Explicitly cast to int8_t
        steamPower.setState((int8_t)power);
    }
}

void ha_set_preinfusion_time(float time) {
    preinfusionTime.setState(time);
}

void ha_trigger_backflush() {
    // Turn the switch ON, HA automation will trigger and turn it OFF
    backflushSwitch.setState(true);
}

// Function to update HA with current values (e.g., on boot or reconnect)
void ha_publish_initial_states() {
    // It's generally better to read these from the machine via HA first,
    // but if needed, you can publish the ESP's current cache.
    // The variables current_... are not accessible here.
    // Consider fetching state from HA entities after connection if needed.
    // machinePower.setState(current_power_state);
    // preinfusionMode.setCurrentState(current_mode_index); // Error: current_mode_index not declared
    // targetTemperature.setState(current_temp); // Error: current_temp not declared
    // steamPower.setState(current_steam); // Error: current_steam not declared
    // preinfusionTime.setState(current_preinfusion_time); // Error: current_preinfusion_time not declared
    
    // lastShotDuration is updated by HA, no need to publish initial state here
    backflushSwitch.setState(false); // Ensure backflush switch is initially off
    Serial.println("Initial HA states published (except those needing read-back).");
}

