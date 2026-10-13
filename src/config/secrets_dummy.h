#pragma once
/**
 * @file secrets_dummy.h
 * @brief Vorlage fuer secrets.h (diese Datei wird versioniert).
 *
 * Kopieren nach secrets.h und echte Werte eintragen.
 * secrets.h selbst ist per .gitignore vom Repository ausgeschlossen.
 */

// --- WLAN ---
#define SECRET_WIFI_SSID "DEIN_WLAN"
#define SECRET_WIFI_PASS "DEIN_PASSWORT"

// --- MQTT-Broker (Home Assistant) ---
#define SECRET_MQTT_HOST "192.168.0.10"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_USER ""
#define SECRET_MQTT_PASS ""

// --- Bluetooth-MAC-Adressen ---
#define SECRET_MAC_MAT   "00:00:00:00:00:00"   // BLE-Katzenmatte
#define SECRET_MAC_KIPPY "00:00:00:00:00:00"   // Kippy-Halsband
