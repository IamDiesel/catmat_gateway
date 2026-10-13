#pragma once
/**
 * @file Config.h
 * @brief Zentrale Konfiguration des CatMat-Gateways (Timings, Topics, BLE).
 *
 * Alle Zeiten in Millisekunden. Die Werte fuer die Matte stammen aus der
 * Messung vom 2026-10-11 (siehe Projektdoku "catmat_ble_messung"):
 *   - Matte sendet exakt 1 Frame/s
 *   - Matte fordert selbst 225 ms / Latenz 4 an (Energiesparen)
 *   - Nach Einschalten/Verbindungsabbruch Advertising ca. alle 5 s, nach
 *     einiger Zeit ohne Verbindung nur noch sehr selten (ein Paket je 20-60 s)
 *   => Verbindung wird nie freiwillig getrennt; der Verbindungsaufbau lauscht
 *      dauerhaft (Initiator), statt erst auf eine Sichtung zu warten
 */

#include <stdint.h>

namespace cfg {

// =============================================================================
// Firmware / Geraet
// =============================================================================
constexpr const char* kFirmwareVersion = "2.4.1";
constexpr const char* kRepeaterName    = "CatMat-EXT";   ///< BLE-Name des Repeaters
constexpr int         kLedPin          = 2;              ///< Blaue LED DevKit C
constexpr int8_t      kTxPowerDbm      = 9;              ///< BLE-Sendeleistung (max. ESP32)

// =============================================================================
// Bedarf (steuert den Verbindungsalarm)
// =============================================================================
/// Gueltigkeit einer MQTT-Stream-Anforderung ("ON"). Der Tab5 erneuert alle
/// 180 s -> 7 min decken einen verpassten Keep-Alive ab.
constexpr uint32_t kLeaseTtlMs        = 7UL * 60UL * 1000UL;

// =============================================================================
// Matten-Verbindung
// =============================================================================
/// Dauer eines Initiator-Laufs. Danach startet sofort der naechste, der
/// Initiator lauscht also praktisch ununterbrochen auf das Advertising.
constexpr uint32_t kConnectTimeoutMs  = 30000;
constexpr uint32_t kConnectGuardMs    = 3000;   ///< Reserve, falls kein Callback kommt
constexpr uint32_t kDataWatchdogMs    = 3000;   ///< ~3 fehlende Frames = Verbindung tot
constexpr uint32_t kMinRetryGapMs     = 1000;   ///< Pause nach einem echten Fehler (kein Timeout)
/// Nach dem Booten so lange ohne Verbindung -> Verbindungsproblem (Alarm bei Bedarf).
constexpr uint32_t kStartupGraceMs    = 15000;
/// So lange ohne Verbindung -> Hinweis "ggf. Matte aus- und einschalten".
constexpr uint32_t kWakeHintMs        = 120000;

// =============================================================================
// Veroeffentlichung
// =============================================================================
constexpr uint32_t kHeartbeatMs       = 10000;  ///< wifi_rssi; Tab5 meldet "Offline" nach 15 s
constexpr uint32_t kMatRssiIntervalMs = 5000;   ///< mat_rssi waehrend Streaming
constexpr uint32_t kOfflineRefreshMs  = 30000;  ///< "unknown"/-130 erneut senden
constexpr int      kOfflineRssi       = -130;   ///< "nicht sichtbar" (Tab5 ignoriert)
constexpr int      kRssiPlausibleMin  = -100;   ///< kleinere Werte = Messfehler

// =============================================================================
// BLE-Scanner (passiv, Kippy; laeuft nur waehrend die Matte verbunden ist)
// =============================================================================
constexpr uint16_t kScanIntervalMs       = 100;   ///< BLE-Scan-Intervall
constexpr uint16_t kScanWindowPeriodicMs = 30;    ///< 30 % Funkzeit (schont Matten-Link/WLAN)
constexpr uint32_t kKippyScanWindowMs    = 3000;  ///< Dauer eines periodischen Fensters
constexpr uint32_t kKippyScanPeriodMs    = 15000; ///< Abstand der periodischen Fenster
constexpr uint32_t kKippyReportMs        = 15000; ///< Kippy-Mittelwert so oft melden
constexpr uint32_t kKippyAbsentMs        = 90000; ///< danach gilt Kippy als weg
constexpr uint32_t kScanStatsIntervalMs  = 30000; ///< Diagnose-Ausgabe der Scan-Zaehler

// =============================================================================
// WLAN / MQTT
// =============================================================================
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kMqttRetryMs          = 5000;
constexpr uint16_t kMqttKeepAliveS       = 30;
constexpr uint16_t kMqttSocketTimeoutS   = 2;
constexpr uint16_t kMqttBufferSize       = 768;  ///< Discovery-Payloads

// =============================================================================
// MQTT-Topics – Vertrag mit Lola OS (Tab5) und Home Assistant.
// NICHT aendern, ohne den Tab5 (secrets.h / MqttLogic.cpp) mitzuziehen!
// =============================================================================
constexpr const char* kTopicCmdSet     = "lolacatmat/switch/stream/set";
constexpr const char* kTopicCmdState   = "lolacatmat/switch/stream/state";
constexpr const char* kTopicDisconnect = "lolacatmat/switch/disconnect/state";
constexpr const char* kTopicPressure   = "lolacatmat/sensor/pressure";
constexpr const char* kTopicMatRssi    = "lolacatmat/sensor/mat_rssi";
constexpr const char* kTopicKippyRssi  = "lolacatmat/sensor/kippy_rssi";
constexpr const char* kTopicWifiRssi   = "lolacatmat/sensor/wifi_rssi";
constexpr const char* kTopicDebug      = "lolacatmat/sensor/debug";
constexpr const char* kTopicAvailability = "lolacatmat/status";   ///< online/offline (LWT)

constexpr const char* kPayloadOnline  = "online";
constexpr const char* kPayloadOffline = "offline";

}  // namespace cfg
