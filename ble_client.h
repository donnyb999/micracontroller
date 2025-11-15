/*
 * Header for the BLE client module.
 *
 * Declares the functions for initializing the BLE client and interacting
 * with the target weight characteristic.
 * Added ble_perform_initial_read for boot-up sequence.
 */
#ifndef BLE_CLIENT_H
#define BLE_CLIENT_H

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <BLECommand.h>

extern QueueHandle_t bleCommandQueue;
extern int8_t target_weight; // Make the global variable accessible

void ble_client_task_init();
void send_ble_command(BLECommand command);

#endif // BLE_CLIENT_H

