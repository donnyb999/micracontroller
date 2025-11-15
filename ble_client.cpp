/*
 * Bluetooth LE client implementation for the shotStopper controller.
 *
 * This refactored version uses a single, persistent FreeRTOS task to manage
 * all BLE operations. Commands (connect, read, write, disconnect) are sent
 * to this task via a FreeRTOS queue, preventing resource conflicts with the
 * WiFi/MQTT task and improving stability.
 */

#include <Arduino.h>
#include <lvgl.h>
#include "ble_client.h"
#include "lvgl_display.h"
#include "app_events.h"
#include "BLECommand.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// BLE UUIDs
static BLEUUID serviceUUID("00000000-0000-0000-0000-000000000ffe");
static BLEUUID charUUID("00000000-0000-0000-0000-00000000ff11");

// State variables
static bool connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr;
static BLEClient* pClient = nullptr;
TaskHandle_t ble_task_handle = NULL;
QueueHandle_t bleCommandQueue = NULL;

// Global target weight
int8_t target_weight = 36; // Default value

// --- Forward Declarations ---
bool connectToServer();
void disconnectFromServer();
bool internal_write_weight(int8_t weight);
int8_t internal_read_weight();
void ble_client_task(void *pvParameters);

// --- BLE Callbacks ---
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {}
};

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        connected = true;
        update_ble_status(BLE_STATUS_CONNECTED);
        Serial.printf("[%lu] Connected to BLE Server.\n", millis());
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        pRemoteCharacteristic = nullptr;
        update_ble_status(BLE_STATUS_DISCONNECTED);
        Serial.printf("[%lu] Disconnected from BLE Server.\n", millis());
    }
};

// --- Core BLE Functions ---
bool connectToServer() {
    if (connected) return true;
    update_ble_status(BLE_STATUS_CONNECTING);

    BLEScan* pScan = BLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    BLEScanResults* results = pScan->start(5, false);

    if (results == nullptr) {
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }

    myDevice = nullptr;
    for (int i = 0; i < results->getCount(); i++) {
        BLEAdvertisedDevice device = results->getDevice(i);
        if (device.isAdvertisingService(serviceUUID)) {
            myDevice = new BLEAdvertisedDevice(device);
            break;
        }
    }
    pScan->clearResults();

    if (myDevice == nullptr) {
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }

    if (pClient == nullptr) {
        pClient = BLEDevice::createClient();
        pClient->setClientCallbacks(new MyClientCallback());
    }

    if (!pClient->connect(myDevice)) {
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        pClient->disconnect();
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        pClient->disconnect();
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }

    update_ble_status(BLE_STATUS_CONNECTED);
    return true;
}

void disconnectFromServer() {
    if (pClient != nullptr && pClient->isConnected()) {
        pClient->disconnect();
    }
    connected = false;
    pRemoteCharacteristic = nullptr;
    update_ble_status(BLE_STATUS_DISCONNECTED);
}

int8_t internal_read_weight() {
    if (connected && pRemoteCharacteristic && pRemoteCharacteristic->canRead()) {
        std::string value = pRemoteCharacteristic->readValue();
        if (!value.empty()) {
            return (int8_t)value[0];
        }
    }
    return -1;
}

bool internal_write_weight(int8_t weight) {
    if (connected && pRemoteCharacteristic && pRemoteCharacteristic->canWrite()) {
        return pRemoteCharacteristic->writeValue((uint8_t*)&weight, 1, true);
    }
    return false;
}

// --- FreeRTOS Task for BLE ---
void ble_client_task(void *pvParameters) {
    BLECommand cmd;
    Serial.println("BLE client task started.");

    while (true) {
        if (xQueueReceive(bleCommandQueue, &cmd, portMAX_DELAY)) {
            switch (cmd.type) {
                case BLE_CONNECT:
                    connectToServer();
                    break;
                case BLE_DISCONNECT:
                    disconnectFromServer();
                    break;
                case BLE_READ_WEIGHT:
                    if (connectToServer()) {
                        int8_t weight = internal_read_weight();
                        if (weight != -1) {
                            target_weight = weight;
                            update_display_value(target_weight);
                            show_verification_checkmark();
                        }
                        disconnectFromServer();
                    }
                    break;
                case BLE_WRITE_WEIGHT:
                    if (connectToServer()) {
                        if (internal_write_weight(cmd.payload)) {
                            int8_t read_value = internal_read_weight();
                            if (read_value == cmd.payload) {
                                target_weight = cmd.payload;
                                update_display_value(target_weight);
                                show_verification_checkmark();
                            } else {
                                update_ble_status(BLE_STATUS_FAILED);
                            }
                        } else {
                            update_ble_status(BLE_STATUS_FAILED);
                        }
                        disconnectFromServer();
                    }
                    break;
            }
        }
    }
}

// --- Public Functions ---
void ble_client_task_init() {
    bleCommandQueue = xQueueCreate(10, sizeof(BLECommand));
    BLEDevice::init("");
    BLEDevice::getScan()->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    
    xTaskCreatePinnedToCore(
        ble_client_task,
        "BLE_Client_Task",
        4096,
        NULL,
        1, // Lowered priority to match HA task
        &ble_task_handle,
        APP_CPU_NUM
    );
}

void send_ble_command(BLECommand command) {
    hide_verification_checkmark();
    update_ble_status(BLE_STATUS_CONNECTING);
    if (xQueueSend(bleCommandQueue, &command, (TickType_t)10) != pdPASS) {
        Serial.println("Failed to send command to BLE queue.");
        update_ble_status(BLE_STATUS_FAILED);
    }
}
