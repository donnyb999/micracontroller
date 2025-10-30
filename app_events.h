/*
 * Defines application-wide events and status types.
 *
 * This makes it easy to pass status information between different parts
 * of the application, like the BLE client and the LVGL display.
 */
#ifndef APP_EVENTS_H
#define APP_EVENTS_H

// Defines the possible states of the Bluetooth LE connection
typedef enum {
    BLE_STATUS_DISCONNECTED,
    BLE_STATUS_CONNECTING,
    BLE_STATUS_CONNECTED,
    BLE_STATUS_FAILED
} ble_status_t;

#endif // APP_EVENTS_H

