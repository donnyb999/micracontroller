/*
 * Header for the rotary encoder module.
 * Declares the initialization function.
 * Exposes the BLE write timer handle.
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h> // Includes FreeRTOS.h via dependencies

// Make the BLE write timer handle available externally
extern TimerHandle_t ble_write_timer;

void encoder_init();

#endif // ENCODER_H

