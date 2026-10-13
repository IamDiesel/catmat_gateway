#pragma once
/**
 * @file GatewayController.h
 * @brief "Gehirn" des Gateways: Matten-Zustandsautomat, Bedarfsregel, Alarm.
 *
 * Verhalten der Matte (gemessen 2026-10-12/13):
 *   - Direkt nach dem Einschalten und nach einem Verbindungsabbruch wirbt sie
 *     ca. alle 5 s, nach einiger Zeit ohne Verbindung nur noch sehr selten
 *     (einzelne Pakete im Abstand von 20-60 s).
 *   - Verbunden spart sie selbst Strom (225 ms Intervall, Latenz 4).
 *   => Die Verbindung wird NIE freiwillig getrennt.
 *   => Der Verbindungsaufbau lauscht DAUERHAFT (NimBLE-Initiator, je Lauf
 *      kConnectTimeoutMs, danach sofort der naechste). So wird auch ein
 *      einzelnes, seltenes Advertising-Paket sofort zur Verbindung.
 *
 * Matten-Zustandsautomat:
 *
 *               verbunden            FFF1 abonniert
 *   CONNECTING ───────────▶ DISCOVERING ───────────▶ STREAMING
 *     ▲  │  ▲                    │                       │
 *     │  └──┘ Lauf abgelaufen:    │                       │
 *     │       sofort neu starten  │                       │
 *     ├───────── Fehler ──────────┘                       │
 *     └────── Verbindung weg / 3 s keine gueltigen Daten ─┘
 *
 * Bedarf = Repeater-Client hat FFF1 abonniert  ODER  MQTT-Lease gueltig
 *   MQTT-Lease: "ON" auf stream/set gilt kLeaseTtlMs (Tab5 erneuert alle 3 min),
 *   "OFF" beendet sie sofort. Beim Booten gilt eine Lease als erteilt.
 *   Der Bedarf steuert nur den Alarm – die Verbindung bleibt immer bestehen.
 *
 * Verbindungsalarm (disconnect/state):
 *   Alarm = Bedarf UND Problem
 *   Problem: sofort bei Verbindungsverlust; nach dem Booten, wenn nach
 *   kStartupGraceMs noch keine Verbindung steht. Endet mit STREAMING.
 *   Nach kWakeHintMs ohne Verbindung: Hinweis "ggf. Matte neu einschalten".
 *
 * Funk: Kippy-Scan und Repeater-Advertising laufen nur in STREAMING, damit der
 * Initiator beim Verbindungsaufbau die volle Bluetooth-Funkzeit hat.
 */

#include <Arduino.h>

#include "BleScanner.h"
#include "Events.h"
#include "KippyTracker.h"
#include "MatLink.h"
#include "MqttBridge.h"
#include "RepeaterServer.h"
#include "StatusLed.h"

class GatewayController {
public:
    enum class MatState : uint8_t { Connecting, Discovering, Streaming };

    GatewayController(MatLink& mat, RepeaterServer& repeater, BleScanner& scanner,
                      MqttBridge& mqtt, StatusLed& led);

    void begin(uint32_t nowMs);

    /** Zyklisch aus loop(): Events abarbeiten, Zustaende fuehren, veroeffentlichen. */
    void update(uint32_t nowMs);

    // --- Rueckrufe aus MqttBridge (laufen in loop()) ---
    void onStreamCommand(bool on, uint32_t nowMs);
    void onMqttOnline(uint32_t nowMs);

    MatState matState() const { return state_; }
    bool     alarmActive() const { return alarm_; }

private:
    // Ereignisse
    void handleEvent(const events::Event& ev, uint32_t nowMs);
    void handleMatFrame(const events::Event& ev, uint32_t nowMs);

    // Matten-Zustandsautomat
    void runMatStateMachine(uint32_t nowMs);
    void enterState(MatState next, uint32_t nowMs);
    void startConnecting(uint32_t nowMs, const char* reason, bool newOutage);
    void startAttempt(uint32_t nowMs);
    void enterStreaming(uint32_t nowMs);
    void applyRadioUsers();

    // Bedarf und Alarm
    bool hasDemand() const;
    void updateLease(uint32_t nowMs);
    void updateAlarm();
    void setAlarm(bool active);

    // Veroeffentlichung
    void publishDebug(const char* message);
    void publishStreamState();
    void publishOfflineValues(uint32_t nowMs);
    void publishPeriodic(uint32_t nowMs);
    void updateLed();

    static const char* stateName(MatState state);

    MatLink&        mat_;
    RepeaterServer& repeater_;
    BleScanner&     scanner_;
    MqttBridge&     mqtt_;
    StatusLed&      led_;
    KippyTracker    kippy_;

    MatState state_          = MatState::Connecting;
    uint32_t stateSinceMs_   = 0;
    uint32_t lastFrameMs_    = 0;

    // Verbindungsaufbau
    bool     attemptRunning_ = false;      ///< NimBLE-Initiator laeuft gerade
    uint32_t attemptStartMs_ = 0;
    uint32_t retryAtMs_      = 0;          ///< naechster Lauf nach echtem Fehler
    uint32_t attempts_       = 0;          ///< Laeufe seit Beginn der Suche
    uint32_t offlineSinceMs_ = 0;          ///< seit wann keine Verbindung besteht
    bool     wakeHintSent_   = false;

    bool     leaseActive_  = false;
    uint32_t leaseUntilMs_ = 0;

    bool     linkProblem_ = false;         ///< Matte sollte verbunden sein, ist es aber nicht
    bool     alarm_       = false;         ///< zuletzt veroeffentlichter Alarmzustand
    String   lastDebug_;                   ///< letzter Status-Text (nach Reconnect erneut senden)

    uint32_t lastHeartbeatMs_      = 0;
    uint32_t lastMatRssiMs_        = 0;
    uint32_t lastOfflineRefreshMs_ = 0;
    uint32_t invalidFrames_        = 0;
};
