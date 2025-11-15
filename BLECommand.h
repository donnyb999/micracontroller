#ifndef BLE_COMMAND_H
#define BLE_COMMAND_H

#include <cstdint>

typedef enum {
    BLE_CONNECT,
    BLE_DISCONNECT,
    BLE_READ_WEIGHT,
    BLE_WRITE_WEIGHT
} BLECommandType;

typedef struct {
    BLECommandType type;
    int8_t payload; // Used for BLE_WRITE_WEIGHT
} BLECommand;

#endif // BLE_COMMAND_H
