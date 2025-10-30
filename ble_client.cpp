/*
 * Bluetooth LE client implementation for the shotStopper controller.
 *
 * Implements a "scan-on-demand" and "connect-on-demand" strategy.
 * An initial read is performed in a separate task on boot which loops
 * until successful, releasing its mutex on each loop to allow
 * write tasks (from encoder/presets) to interrupt.
 *
 * connectToServer is now fully synchronous and iterates scan results
 * to avoid race conditions.
 * Corrected BLEScanResults pointer and 'final_success' typo.
 */

#include <Arduino.h>
#include <lvgl.h> // Include LVGL FIRST
#include "ble_client.h"
#include "lvgl_display.h" // Include AFTER lvgl.h
#include "app_events.h" // Include status definitions
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// BLE UUIDs
static BLEUUID serviceUUID("00000000-0000-0000-0000-000000000ffe");
static BLEUUID charUUID("00000000-0000-0000-0000-00000000ff11");

// State variables
static bool connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr; // Device to connect to
static BLEClient* pClient = nullptr;
static SemaphoreHandle_t bleMutex = nullptr; // Mutex to protect BLE operations
static TaskHandle_t writeTaskHandle = NULL;
static TaskHandle_t initialReadTaskHandle = NULL;

// Global target weight
int8_t target_weight = 36; // Default value

// Forward declarations
bool connectToServer();
void disconnectFromServer();
bool internal_write_weight(int8_t weight);
int8_t internal_read_weight();
static void initial_read_task(void* pvParameters); // Task for boot-up read
static void write_verify_task(void* pvParameters); // Task for writing weight


// --- BLE Callbacks ---

// This is no longer used by connectToServer, but is still needed for BLEDevice
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        // This callback is no longer responsible for finding the device
        // for connectToServer, but we leave the class definition.
    }
};

// Callback for handling connection and disconnection events
class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        connected = true;
        update_ble_status(BLE_STATUS_CONNECTED); // Update UI
        Serial.printf("[%lu] Connected to BLE Server.\n", millis());
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        pRemoteCharacteristic = nullptr; // Crucial: Invalidate characteristic pointer
        update_ble_status(BLE_STATUS_DISCONNECTED); // Update UI
        Serial.printf("[%lu] Disconnected from BLE Server.\n", millis());
    }
};

// --- Core BLE Functions (Connect, Disconnect, Read, Write) ---

// Function to scan FOR and connect TO the BLE server
bool connectToServer() {
    if (connected) {
        Serial.printf("[%lu] Already connected.\n", millis());
        return true;
    }

    update_ble_status(BLE_STATUS_CONNECTING); // Update UI to "Connecting"

    // 1. Scan for the device (Synchronous approach)
    Serial.printf("[%lu] Starting 5-second scan to find device...\n", millis());
    BLEScan* pScan = BLEDevice::getScan();
    // We don't set a callback, we'll check the results manually
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);

    // Start a blocking scan for 5 seconds and get the results
    // Corrected: pScan->start() returns a pointer
    BLEScanResults* results = pScan->start(5, false); // false = not continuous

    if (results == nullptr) {
        Serial.printf("[%lu] Scan failed to start.\n", millis());
        update_ble_status(BLE_STATUS_FAILED); // Red icon
        return false;
    }

    Serial.printf("[%lu] Scan finished. Found %d devices.\n", millis(), results->getCount());
    

    // Clear the old 'myDevice'
    if(myDevice != nullptr) {
        delete myDevice;
        myDevice = nullptr;
    }

    // 2. Iterate results to find our device
    // Corrected: Use -> operator for pointer
    for (int i = 0; i < results->getCount(); i++) { 
        BLEAdvertisedDevice device = results->getDevice(i);
        if (device.isAdvertisingService(serviceUUID)) {
            Serial.printf("[%lu] Found target device: %s\n", millis(), device.getAddress().toString().c_str());
            myDevice = new BLEAdvertisedDevice(device); // Store the found device info
            break; // Stop searching
        }
    }

    pScan->clearResults(); // Clean up scan results from memory

    // 3. Check if device was found
    if (myDevice == nullptr) {
        Serial.printf("[%lu] Target device not found in scan results.\n", millis());
        update_ble_status(BLE_STATUS_FAILED); // Red icon
        return false;
    }
    
    Serial.printf("[%lu] Device found. Attempting connection to %s\n", millis(), myDevice->getAddress().toString().c_str());

    // 4. Create Client and Connect
    if (pClient == nullptr) {
         pClient = BLEDevice::createClient();
         Serial.printf("[%lu]  - Created client\n", millis());
         pClient->setClientCallbacks(new MyClientCallback());
    }

    if (!pClient->connect(myDevice)) {
        Serial.printf("[%lu]  - Connection failed\n", millis());
        update_ble_status(BLE_STATUS_FAILED); // Update UI
        return false;
    }
    Serial.printf("[%lu]  - Connection successful (pending callback)\n", millis());
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for onConnect callback

    // 5. Get Service and Characteristic
    BLERemoteService* pRemoteService = nullptr;
    try { pRemoteService = pClient->getService(serviceUUID); } catch (...) { }

    if (pRemoteService == nullptr) {
        Serial.printf("[%lu] Failed to find service UUID.\n", millis());
        pClient->disconnect();
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }
    Serial.printf("[%lu]  - Found service\n", millis());

    try { pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID); } catch (...) { }

    if (pRemoteCharacteristic == nullptr) {
        Serial.printf("[%lu] Failed to find characteristic UUID.\n", millis());
        pClient->disconnect();
        update_ble_status(BLE_STATUS_FAILED);
        return false;
    }
    Serial.printf("[%lu]  - Found characteristic\n", millis());

    // Register for notifications (optional, but good)
    if (pRemoteCharacteristic->canNotify()) {
         const uint8_t notificationOn[] = {0x1, 0x0};
         BLERemoteDescriptor* pDesc = pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902));
         if (pDesc) {
            if(pDesc->writeValue((uint8_t*)notificationOn, 2, true)) {
                 pRemoteCharacteristic->registerForNotify(NULL); // Can use NULL if callback is simple
                 Serial.printf("[%lu]  - Registered for notifications.\n", millis());
            }
         }
    }

    update_ble_status(BLE_STATUS_CONNECTED); // Green icon
    return true;
}

// Function to disconnect from the BLE server
void disconnectFromServer() {
    if (pClient != nullptr && pClient->isConnected()) {
        Serial.printf("[%lu] Disconnecting from server...\n", millis());
        pClient->disconnect();
    } else {
         Serial.printf("[%lu] Already disconnected or client doesn't exist.\n", millis());
    }
    // Callback sets state and UI
    connected = false;
    pRemoteCharacteristic = nullptr;
    update_ble_status(BLE_STATUS_DISCONNECTED); // Ensure UI is grey
}

// Function to read the value internally, returns weight or -1 on error
int8_t internal_read_weight() {
    if (connected && pRemoteCharacteristic != nullptr && pRemoteCharacteristic->canRead()) {
        Serial.printf("[%lu] Reading target weight from BLE device...\n", millis());
        String value = ""; // Use Arduino String
        try {
             value = pRemoteCharacteristic->readValue();
        } catch (...) {
             Serial.printf("[%lu] Exception during readValue().\n", millis());
             return -1;
        }

        if (value.length() > 0) {
            Serial.printf("[%lu] Read value: %d\n", millis(), (int8_t)value[0]);
            return (int8_t)value[0];
        }
        Serial.printf("[%lu] Read failed: No data.\n", millis());
        return -1;
    } else {
        Serial.printf("[%lu] Cannot read: connected=%d, char=%p\n", millis(), connected, pRemoteCharacteristic);
        return -1;
    }
}

// Function to write the value internally, returns true on success
bool internal_write_weight(int8_t weight) {
    if (connected && pRemoteCharacteristic != nullptr && pRemoteCharacteristic->canWrite()) {
        Serial.printf("[%lu] Writing target weight to BLE device: %d\n", millis(), weight);
        bool writeSuccess = false;
        try {
             writeSuccess = pRemoteCharacteristic->writeValue((uint8_t*)&weight, 1, true); // true for response
        } catch (...) {
             Serial.printf("[%lu] Exception during writeValue().\n", millis());
             writeSuccess = false;
        }

        if(writeSuccess) {
            Serial.printf("[%lu] Write successful (with response).\n", millis());
            return true;
        } else {
            Serial.printf("[%lu] Write failed (or no response).\n", millis());
            return false;
        }
    } else {
         Serial.printf("[%lu] Cannot write: connected=%d, char=%p\n", millis(), connected, pRemoteCharacteristic);
        return false;
    }
}


// --- FreeRTOS Tasks ---

// Task for the initial read on boot
static void initial_read_task(void* pvParameters) {
    Serial.printf("[%lu] Initial read task started. Waiting 1s before first attempt.\n", millis());
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before starting

    while(true) { // Loop forever until successful
        if (xSemaphoreTake(bleMutex, pdMS_TO_TICKS(15000)) == pdTRUE) {
            Serial.printf("[%lu] Initial read task acquired mutex. Attempting connect...\n", millis());
            
            if (connectToServer()) { // This now scans AND connects
                Serial.printf("[%lu] Initial connect successful. Reading value...\n", millis());
                int8_t initial_weight = internal_read_weight();
                
                disconnectFromServer(); // Disconnect after read
                
                if (initial_weight != -1) {
                    target_weight = initial_weight;
                    update_display_value(target_weight);
                    show_verification_checkmark(); // Show checkmark for initial read
                    update_ble_status(BLE_STATUS_DISCONNECTED); // Set to Grey on success
                    Serial.printf("[%lu] Initial weight read: %d. Task succeeding.\n", millis(), target_weight);
                    
                    xSemaphoreGive(bleMutex); // Release mutex
                    break; // <<<--- EXIT THE LOOP ON SUCCESS
                } else {
                    Serial.printf("[%lu] Initial read failed.\n", millis());
                    update_ble_status(BLE_STATUS_FAILED); // Red icon
                }
            } else {
                Serial.printf("[%lu] Initial connect failed (device not found or error).\n", millis());
                // connectToServer() already sets FAILED status
            }

            xSemaphoreGive(bleMutex); // CRITICAL: Release mutex *before* delay
        } else {
            Serial.printf("[%lu] Initial read task failed to get mutex.\n", millis());
            update_ble_status(BLE_STATUS_FAILED); // Red icon
        }

        Serial.printf("[%lu] Retrying initial read in 5 seconds...\n", millis());
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds before retry
    }

    Serial.printf("[%lu] Initial read task finished. Deleting task.\n", millis());
    initialReadTaskHandle = NULL;
    vTaskDelete(NULL); // Delete self
}


// This task handles the connect -> write -> read -> disconnect sequence
void write_verify_task(void* pvParameters) {
    int8_t weight_to_write = *(int8_t*)pvParameters;
    bool final_success = false;

    // 1. Acquire Mutex
    Serial.printf("[%lu] Write task started for weight %d. Waiting for mutex...\n", millis(), weight_to_write);
    if (xSemaphoreTake(bleMutex, pdMS_TO_TICKS(15000)) == pdTRUE) {
        Serial.printf("[%lu] Write task acquired mutex.\n", millis());

        // 2. Connect (this now includes scanning)
        if (connectToServer()) {
            Serial.printf("[%lu] Connection successful. Proceeding to write.\n", millis());
            vTaskDelay(pdMS_TO_TICKS(100)); // Short delay

            // 3. Write
            if (internal_write_weight(weight_to_write)) {
                 Serial.printf("[%lu] Write command successful. Delaying before verify.\n", millis());
                 vTaskDelay(pdMS_TO_TICKS(200)); // Delay before reading back

                // 4. Read back to verify
                int8_t read_value = internal_read_weight();
                if (read_value == weight_to_write) {
                    Serial.printf("[%lu] Verification successful! Remote value (%d) matches written value (%d).\n", millis(), read_value, weight_to_write);
                    target_weight = weight_to_write; // Update global state ONLY on success
                    update_display_value(target_weight); // Update UI ONLY on success
                    show_verification_checkmark();
                    final_success = true; // Mark overall success
                } else {
                    Serial.printf("[%lu] Verification FAILED! Remote value (%d) != written value (%d).\n", millis(), read_value, weight_to_write);
                    hide_verification_checkmark();
                    update_ble_status(BLE_STATUS_FAILED); // Indicate failure
                }
            } else {
                 Serial.printf("[%lu] Write command failed.\n", millis());
                 hide_verification_checkmark();
                 update_ble_status(BLE_STATUS_FAILED);
            }

            // 5. Disconnect (regardless of write/verify outcome)
             Serial.printf("[%lu] Disconnecting after operation...\n", millis());
            disconnectFromServer();
             vTaskDelay(pdMS_TO_TICKS(500)); // Give some time for disconnect CB

        } else {
            Serial.printf("[%lu] Write task failed to connect.\n", millis());
            hide_verification_checkmark();
            // connectToServer already sets FAILED status if needed
        }

        // 6. Release Mutex
        xSemaphoreGive(bleMutex);
        Serial.printf("[%lu] Write task released mutex.\n", millis());

    } else {
        Serial.printf("[%lu] Write task failed to acquire mutex. Operation aborted.\n", millis());
        hide_verification_checkmark();
        update_ble_status(BLE_STATUS_FAILED); // Indicate failure
    }
    
    // Set icon to Grey if successful, otherwise it will be Red
    if (final_success) {
        update_ble_status(BLE_STATUS_DISCONNECTED); // Grey icon
    }

    Serial.printf("[%lu] Write task finished. Final success: %d. Deleting task.\n", millis(), final_success);
    writeTaskHandle = NULL; // Mark task as completed *before* deleting
    vTaskDelete(NULL); // Delete self
}

// --- Public Functions (Called by other modules) ---

// Public function to create the initial read task
void ble_perform_initial_read() {
    if (initialReadTaskHandle != NULL) {
        Serial.println("Initial read task already running.");
        return;
    }
    Serial.println("Creating initial read task...");
    BaseType_t taskCreated = xTaskCreate(
        initial_read_task,
        "InitialReadTask",
        4096, // Stack size
        NULL, // No parameters
        5,    // Priority
        &initialReadTaskHandle
    );
    if (taskCreated != pdPASS) {
        Serial.println("Failed to create initial read task!");
        initialReadTaskHandle = NULL;
        update_ble_status(BLE_STATUS_FAILED);
    }
}

// Public function to initiate writing the target weight
void write_target_weight(int8_t weight) {
     static int8_t weight_buffer; // Static buffer to hold the weight argument

    // Check if another write/read task is already running
    if (writeTaskHandle != NULL || initialReadTaskHandle != NULL) {
         Serial.printf("[%lu] BLE operation already in progress. Ignoring new request for %d.\n", millis(), weight);
         return;
    }

    weight_buffer = weight; // Store the weight to be passed to the task
    hide_verification_checkmark(); // Hide checkmark immediately on new write request
    update_ble_status(BLE_STATUS_CONNECTING); // Set status to Connecting *before* creating task

    Serial.printf("[%lu] Creating write task for weight: %d\n", millis(), weight);
    BaseType_t taskCreated = xTaskCreate(write_verify_task,
                                         "BLE_WriteVerify",
                                         4096, // Stack size
                                         &weight_buffer, // Pass address of the buffer
                                         5,    // Priority
                                         &writeTaskHandle); // Store task handle

    if (taskCreated != pdPASS) {
        Serial.printf("[%lu] Failed to create write task!\n", millis());
        writeTaskHandle = NULL; // Ensure handle is null if creation failed
        update_ble_status(BLE_STATUS_FAILED);
        hide_verification_checkmark();
    }
}


// --- Initialization ---

// Initialize the BLE client
void ble_client_init() {
    Serial.println("Initializing BLE client...");
    bleMutex = xSemaphoreCreateMutex(); // Create mutex
    if (bleMutex == NULL) {
         Serial.println("!!! Failed to create BLE mutex !!!");
    }
    BLEDevice::init("");
    // We need to set a callback handler, even if it's an empty one, 
    // for the scan results iteration to work correctly.
    BLEDevice::getScan()->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    Serial.println("BLE client initialized. Ready for on-demand connection.");
    update_ble_status(BLE_STATUS_DISCONNECTED); // Initial status
}

