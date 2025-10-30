/*
 * Header for the Home Assistant integration module.
 *
 * This file declares the functions for initializing the HA connection
 * and for sending commands from the UI to Home Assistant. It also
 * declares the HA entity objects as extern so they can be accessed
 * from other parts of the application. The backflush button has been
 * correctly implemented as a switch.
 */
#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <ArduinoHA.h>
#include <cstdint>

// Function to initialize the Home Assistant connection
void ha_init();

// --- Functions to send commands from UI to HA ---
void ha_set_machine_power(bool state);
void ha_set_preinfusion_mode(int8_t mode_index);
void ha_set_target_temperature(float temp);
void ha_set_steam_power(int8_t power);
void ha_set_preinfusion_time(float time);
void ha_trigger_backflush();

// --- HA Device & Entity Declarations ---
extern HADevice ha_device;
extern HAMqtt mqtt;

extern HASwitch machinePower;
extern HASelect preinfusionMode;
extern HASwitch backflushSwitch; // Corrected type to HASwitch
extern HANumber targetTemperature;
extern HANumber steamPower;
extern HANumber preinfusionTime;
extern HANumber lastShotDuration;

#endif // HOME_ASSISTANT_H

