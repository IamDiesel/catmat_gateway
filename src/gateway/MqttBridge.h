#pragma once
/**
 * @file MqttBridge.h
 * @brief WLAN + MQTT, Home-Assistant-Discovery und Publish-API.
 *
 * Nicht blockierender Zustandsautomat:
 *
 *   WIFI_CONNECTING ──WLAN da──▶ MQTT_CONNECTING ──Broker ok──▶ ONLINE
 *         ▲   │ Timeout: neu starten     ▲    │ alle 5 s Versuch    │
 *         │   └───────────┘              │    └─────────┘           │
 *         ├───────────── WLAN weg ───────┴──────────────────────────┤
 *         │                              ▲                          │
 *         │                              └────── MQTT weg ──────────┘
 *
 * Beim Eintritt in ONLINE: Availability "online" (retained, LWT "offline"),
 * Discovery, Abo des Stream-Befehls, danach Rueckruf an den Controller.
 *
 * Laeuft ausschliesslich in loop() – PubSubClient ist nicht threadsicher.
 */

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <functional>

class MqttBridge {
public:
    enum class State : uint8_t { WifiConnecting, MqttConnecting, Online };

    using CommandHandler = std::function<void(bool on)>;   ///< stream/set ON/OFF
    using OnlineHandler  = std::function<void()>;          ///< nach (Re-)Connect

    void begin(CommandHandler onCommand, OnlineHandler onOnline);

    /** Zyklisch aus loop() aufrufen. */
    void update(uint32_t nowMs);

    bool  isOnline() const { return state_ == State::Online; }
    State state() const { return state_; }
    int   wifiRssi() const { return WiFi.RSSI(); }

    /** @return false, wenn nicht online (Wert wird dann verworfen). */
    bool publish(const char* topic, const char* payload, bool retained = false);
    bool publish(const char* topic, int value, bool retained = false);

private:
    void startWifi(uint32_t nowMs);
    bool connectBroker();
    void publishDiscovery();
    void publishEntity(const char* component, const char* objectId,
                       const char* uniqueSuffix, const char* fields);
    void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);

    WiFiClient   net_;
    PubSubClient mqtt_{net_};

    State    state_          = State::WifiConnecting;
    uint32_t wifiStartMs_    = 0;
    uint32_t lastMqttTryMs_  = 0;

    char deviceId_[32] = {0};   ///< "catmat_gateway_<MAC>" – identisch zur Alt-Firmware
    char clientId_[32] = {0};

    CommandHandler onCommand_;
    OnlineHandler  onOnline_;
};
