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

void ble_client_init();
void ble_perform_initial_read(); // New function to be called from app_init
void write_target_weight(int8_t weight);
// internal_read_weight is not public
// int8_t read_target_weight(); // This function was removed as it's internal now

extern int8_t target_weight; // Make the global variable accessible

#endif // BLE_CLIENT_H

