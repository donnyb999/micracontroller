/*
 * Wi-Fi and MQTT Broker Credentials
 *
 * This file contains your network and server secrets.
 * It is kept separate from the main application logic for security
 * and ease of configuration.
 */

#ifndef SECRETS_H
#define SECRETS_H

// -- Wi-Fi Credentials --
#define WIFI_SSID "HillHouse"
#define WIFI_PASSWORD "avabear17"

// -- MQTT Broker Configuration --
#define MQTT_SERVER "192.168.50.32"
#define MQTT_PORT 1883
#define MQTT_USER "hass"
#define MQTT_PASSWORD "flazenf1"

// -- Home Assistant Device Configuration --
#define HA_DEVICE_NAME "Linea Micra Control"
#define HA_UNIQUE_ID "linea_micra_controller_01"

#endif // SECRETS_H
