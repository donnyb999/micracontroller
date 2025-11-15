/*
 * Main application file for the ESP32-S3 Espresso Shot Stopper Controller.
 * This file is now simplified. All initialization logic has been moved to app_init()
 * to ensure correct setup order and task management. The main loop is empty because
 * all operations are now handled by their own FreeRTOS tasks.
 */

#include <Arduino.h>
#include "app.h" // Include the new main application header

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial monitor time to connect
  Serial.println("--- Shot Stopper Controller ---");
  
  app_init(); // Call the main application initializer

  Serial.println("Setup complete. Handing control over to FreeRTOS tasks.");
}

void loop() {
  // This function is required by the Arduino toolchain, but all work is
  // handled by FreeRTOS tasks.
}

