/**
 * @file MqttBridge.cpp
 * @brief Implementierung von WLAN/MQTT und Home-Assistant-Discovery.
 */

#include "MqttBridge.h"

#include "Config.h"
#include "secrets.h"

namespace {

/** Vergleicht die (nicht nullterminierte) Payload ohne Gross-/Kleinschreibung. */
bool payloadEquals(const uint8_t* payload, unsigned int length, const char* expected) {
    if (strlen(expected) != length) return false;
    return strncasecmp(reinterpret_cast<const char*>(payload), expected, length) == 0;
}

}  // namespace

// =============================================================================
// Oeffentliche Schnittstelle
// =============================================================================
void MqttBridge::begin(CommandHandler onCommand, OnlineHandler onOnline) {
    onCommand_ = std::move(onCommand);
    onOnline_  = std::move(onOnline);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);           // Modem-Sleep ist Pflicht bei WLAN + BLE (Koexistenz)
    WiFi.setAutoReconnect(true);

    // Geraete-ID wie in der Alt-Firmware -> HA-Entitaeten bleiben erhalten.
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    snprintf(deviceId_, sizeof(deviceId_), "catmat_gateway_%s", mac.c_str());
    snprintf(clientId_, sizeof(clientId_), "CatMat-%s", mac.c_str());

    mqtt_.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    mqtt_.setBufferSize(cfg::kMqttBufferSize);
    mqtt_.setKeepAlive(cfg::kMqttKeepAliveS);
    mqtt_.setSocketTimeout(cfg::kMqttSocketTimeoutS);
    mqtt_.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        handleMessage(topic, payload, length);
    });

    startWifi(millis());
}

void MqttBridge::update(uint32_t nowMs) {
    const bool wifiUp = WiFi.status() == WL_CONNECTED;

    switch (state_) {
        case State::WifiConnecting:
            if (wifiUp) {
                Serial.printf("[WIFI] Verbunden, IP %s\n", WiFi.localIP().toString().c_str());
                state_         = State::MqttConnecting;
                lastMqttTryMs_ = nowMs - cfg::kMqttRetryMs;   // sofort versuchen
            } else if (nowMs - wifiStartMs_ >= cfg::kWifiConnectTimeoutMs) {
                Serial.println("[WIFI] Timeout, starte neu.");
                startWifi(nowMs);
            }
            break;

        case State::MqttConnecting:
            if (!wifiUp) {
                state_       = State::WifiConnecting;
                wifiStartMs_ = nowMs;
            } else if (nowMs - lastMqttTryMs_ >= cfg::kMqttRetryMs) {
                lastMqttTryMs_ = nowMs;
                if (connectBroker()) {
                    state_ = State::Online;
                    if (onOnline_) onOnline_();
                }
            }
            break;

        case State::Online:
            if (!wifiUp) {
                Serial.println("[WIFI] Verbindung verloren.");
                state_       = State::WifiConnecting;
                wifiStartMs_ = nowMs;
            } else if (!mqtt_.connected()) {
                Serial.printf("[MQTT] Verbindung verloren (state %d).\n", mqtt_.state());
                state_         = State::MqttConnecting;
                lastMqttTryMs_ = nowMs;
            } else {
                mqtt_.loop();
            }
            break;

    }
}

bool MqttBridge::publish(const char* topic, const char* payload, bool retained) {
    if (state_ != State::Online) return false;
    return mqtt_.publish(topic, payload, retained);
}

bool MqttBridge::publish(const char* topic, int value, bool retained) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return publish(topic, buffer, retained);
}

// =============================================================================
// Intern
// =============================================================================
void MqttBridge::startWifi(uint32_t nowMs) {
    WiFi.disconnect();
    WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
    wifiStartMs_ = nowMs;
    state_       = State::WifiConnecting;
}

bool MqttBridge::connectBroker() {
    Serial.printf("[MQTT] Verbinde zu %s:%d ...\n", SECRET_MQTT_HOST, SECRET_MQTT_PORT);
    // Last Will: Broker setzt "offline", wenn das Gateway unerwartet verschwindet.
    const bool ok = mqtt_.connect(clientId_, SECRET_MQTT_USER, SECRET_MQTT_PASS,
                                  cfg::kTopicAvailability, 0, true, cfg::kPayloadOffline);
    if (!ok) {
        Serial.printf("[MQTT] Fehlgeschlagen, rc=%d\n", mqtt_.state());
        return false;
    }

    Serial.println("[MQTT] Verbunden.");
    publishDiscovery();
    mqtt_.subscribe(cfg::kTopicCmdSet);
    mqtt_.publish(cfg::kTopicAvailability, cfg::kPayloadOnline, true);
    return true;
}

void MqttBridge::publishEntity(const char* component, const char* objectId,
                               const char* uniqueSuffix, const char* fields) {
    char topic[96];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, deviceId_, objectId);

    char payload[cfg::kMqttBufferSize - 128];
    snprintf(payload, sizeof(payload),
             "{%s,\"uniq_id\":\"%s_%s\",\"avty_t\":\"%s\","
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"Lola CatMat Gateway\","
             "\"mf\":\"LolaOS\",\"mdl\":\"ESP32 WROOM Hybrid\",\"sw\":\"%s\"}}",
             fields, deviceId_, uniqueSuffix, cfg::kTopicAvailability,
             deviceId_, cfg::kFirmwareVersion);

    if (!mqtt_.publish(topic, payload, true)) {
        Serial.printf("[MQTT] Discovery fehlgeschlagen: %s\n", objectId);
    }
}

void MqttBridge::publishDiscovery() {
    // Namen, objectIds und uniq_id-Suffixe identisch zur Alt-Firmware halten,
    // damit Home Assistant die bestehenden Entitaeten weiterverwendet.
    char f[256];

    snprintf(f, sizeof(f), "\"name\":\"CatMat Druck\",\"stat_t\":\"%s\",\"icon\":\"mdi:paw\"",
             cfg::kTopicPressure);
    publishEntity("sensor", "pressure", "press", f);

    snprintf(f, sizeof(f), "\"name\":\"CatMat Signalstaerke\",\"stat_t\":\"%s\","
             "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\"", cfg::kTopicMatRssi);
    publishEntity("sensor", "mat_rssi", "matrssi", f);

    snprintf(f, sizeof(f), "\"name\":\"Kippy Signalstaerke\",\"stat_t\":\"%s\","
             "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\"", cfg::kTopicKippyRssi);
    publishEntity("sensor", "kippy_rssi", "kippyrssi", f);

    snprintf(f, sizeof(f), "\"name\":\"CatMat Stream\",\"cmd_t\":\"%s\",\"stat_t\":\"%s\","
             "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"icon\":\"mdi:bluetooth-connect\"",
             cfg::kTopicCmdSet, cfg::kTopicCmdState);
    publishEntity("switch", "stream", "stream", f);

    snprintf(f, sizeof(f), "\"name\":\"CatMat Verbindungsalarm\",\"stat_t\":\"%s\","
             "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"dev_cla\":\"problem\"",
             cfg::kTopicDisconnect);
    publishEntity("binary_sensor", "disconnect", "alarm", f);

    snprintf(f, sizeof(f), "\"name\":\"Gateway WiFi Signal\",\"stat_t\":\"%s\","
             "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"ent_cat\":\"diagnostic\"",
             cfg::kTopicWifiRssi);
    publishEntity("sensor", "wifi_rssi", "wifirssi", f);

    snprintf(f, sizeof(f), "\"name\":\"CatMat Status\",\"stat_t\":\"%s\","
             "\"icon\":\"mdi:text-box-outline\",\"ent_cat\":\"diagnostic\"", cfg::kTopicDebug);
    publishEntity("sensor", "debug", "debug", f);

    Serial.println("[MQTT] Discovery gesendet.");
}

void MqttBridge::handleMessage(const char* topic, const uint8_t* payload, unsigned int length) {
    if (strcmp(topic, cfg::kTopicCmdSet) != 0 || !onCommand_) return;

    if (payloadEquals(payload, length, "ON")) {
        onCommand_(true);
    } else if (payloadEquals(payload, length, "OFF")) {
        onCommand_(false);
    }
}
