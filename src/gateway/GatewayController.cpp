/**
 * @file GatewayController.cpp
 * @brief Implementierung von Matten-Zustandsautomat, Bedarfsregel und Alarm.
 *
 * Die Status-Texte (debug-Topic) entsprechen weitgehend der Alt-Firmware,
 * damit die Anzeige auf dem Tab5 und die HA-Historie vertraut bleiben.
 * Bewusst ASCII (ohne Umlaute), da sie auf dem Tab5 angezeigt werden.
 */

#include "GatewayController.h"

#include "Config.h"
#include "MatFrame.h"

namespace {

/** Ueberlaufsicherer Zeitvergleich: "seit since sind mindestens duration ms vergangen". */
bool elapsedAtLeast(uint32_t nowMs, uint32_t sinceMs, uint32_t durationMs) {
    return static_cast<int32_t>(nowMs - sinceMs) >= static_cast<int32_t>(durationMs);
}

// Status-Texte
constexpr const char* kMsgWifiOk       = "WLAN Verbindung stabil.";
constexpr const char* kMsgSearching    = "Suche Matte...";
constexpr const char* kMsgConnecting   = "Verbindung zur Matte wird aufgebaut...";
constexpr const char* kMsgWakeHint     = "Matte antwortet nicht - ggf. Matte aus- und wieder einschalten.";
constexpr const char* kMsgUnreachable  = "Matte nicht erreichbar.";
constexpr const char* kMsgReady        = "Matte bereit!";
constexpr const char* kMsgDisconnected = "Matte getrennt.";
constexpr const char* kMsgNoData       = "Keine Daten von der Matte.";
constexpr const char* kMsgLeaseExpired = "Stream-Anforderung abgelaufen.";
constexpr const char* kMsgCmdOn        = "MQTT-Befehl: ON";
constexpr const char* kMsgCmdOff       = "MQTT-Befehl: OFF";
constexpr const char* kMsgTabletOn     = "Tablet verbunden.";
constexpr const char* kMsgTabletOff    = "Tablet getrennt.";

}  // namespace

GatewayController::GatewayController(MatLink& mat, RepeaterServer& repeater, BleScanner& scanner,
                                     MqttBridge& mqtt, StatusLed& led)
    : mat_(mat), repeater_(repeater), scanner_(scanner), mqtt_(mqtt), led_(led) {}

// =============================================================================
// Lebenszyklus
// =============================================================================
void GatewayController::begin(uint32_t nowMs) {
    // Boot-Lease: nach einem Neustart gilt sofort Bedarf (Alarm aktiv). Der Tab5
    // erneuert spaetestens nach 3 min.
    leaseActive_  = true;
    leaseUntilMs_ = nowMs + cfg::kLeaseTtlMs;

    lastHeartbeatMs_      = nowMs;
    lastOfflineRefreshMs_ = nowMs;
    kippy_.begin(nowMs);

    // Nach einem Neustart des Gateways hat die Matte gerade ihre Verbindung
    // verloren und wirbt haeufig -> sofort verbinden. Noch kein Problem:
    // das entsteht erst nach kStartupGraceMs ohne Verbindung.
    startConnecting(nowMs, kMsgSearching, true);
}

void GatewayController::update(uint32_t nowMs) {
    events::Event ev;
    while (events::receive(ev)) handleEvent(ev, nowMs);

    updateLease(nowMs);
    runMatStateMachine(nowMs);
    updateAlarm();

    applyRadioUsers();
    scanner_.update(nowMs);

    publishPeriodic(nowMs);
    updateLed();
}

// =============================================================================
// MQTT-Rueckrufe
// =============================================================================
void GatewayController::onStreamCommand(bool on, uint32_t nowMs) {
    if (on) {
        const bool wasActive = leaseActive_;
        leaseActive_  = true;
        leaseUntilMs_ = nowMs + cfg::kLeaseTtlMs;
        if (!wasActive) publishDebug(kMsgCmdOn);   // Keep-Alive des Tab5 nicht protokollieren
    } else {
        leaseActive_ = false;
        publishDebug(kMsgCmdOff);
    }
    publishStreamState();
}

void GatewayController::onMqttOnline(uint32_t nowMs) {
    // Retained-Zustaende nach jedem (Re-)Connect auffrischen.
    publishStreamState();
    mqtt_.publish(cfg::kTopicDisconnect, alarm_ ? "ON" : "OFF", true);
    mqtt_.publish(cfg::kTopicWifiRssi, mqtt_.wifiRssi());
    lastHeartbeatMs_ = nowMs;

    // Wie Alt-Firmware melden, danach den aktuellen Status wiederherstellen.
    const String current = lastDebug_;
    publishDebug(kMsgWifiOk);
    if (current.length() > 0) publishDebug(current.c_str());

    if (state_ != MatState::Streaming) publishOfflineValues(nowMs);
}

// =============================================================================
// Ereignisse
// =============================================================================
void GatewayController::handleEvent(const events::Event& ev, uint32_t nowMs) {
    using events::Type;

    switch (ev.type) {
        case Type::MatConnected:
            if (state_ == MatState::Connecting && attemptRunning_) {
                attemptRunning_ = false;
                enterState(MatState::Discovering, nowMs);
            } else {
                mat_.disconnect();   // verspaeteter Connect nach Abbruch
            }
            break;

        case Type::MatConnectFailed:
            if (state_ != MatState::Connecting) break;
            attemptRunning_ = false;
            if (MatLink::isTimeout(ev.value)) {
                // Normalfall bei selten werbender Matte: einfach weiter lauschen.
                Serial.printf("[MAT] Kein Advertising in %lu s - lausche weiter (Lauf %lu).\n",
                              static_cast<unsigned long>(cfg::kConnectTimeoutMs / 1000),
                              static_cast<unsigned long>(attempts_));
                retryAtMs_ = nowMs;
            } else {
                Serial.printf("[MAT] Aufbau fehlgeschlagen: %ld (%s)\n",
                              static_cast<long>(ev.value), MatLink::reasonText(ev.value));
                publishDebug(kMsgUnreachable);
                retryAtMs_ = nowMs + cfg::kMinRetryGapMs;
            }
            break;

        case Type::MatDisconnected:
            Serial.printf("[MAT] Getrennt: %ld (%s)\n",
                          static_cast<long>(ev.value), MatLink::reasonText(ev.value));
            // In CONNECTING ignorieren: ein fehlgeschlagener Aufbau kommt als
            // MatConnectFailed; ein hier eintreffendes Disconnect ist ein
            // verspaetetes Event der vorherigen (bereits verworfenen) Verbindung.
            if (state_ == MatState::Discovering || state_ == MatState::Streaming) {
                linkProblem_ = true;
                startConnecting(nowMs, kMsgDisconnected, true);
            }
            break;

        case Type::MatFrame:
            handleMatFrame(ev, nowMs);
            break;

        case Type::RepeaterConnected:
            repeater_.onClientConnected();
            publishDebug(kMsgTabletOn);
            break;

        case Type::RepeaterDisconnected:
            repeater_.onClientDisconnected();
            publishDebug(kMsgTabletOff);
            break;

        case Type::RepeaterSubscription:
            repeater_.onSubscriptionChanged(ev.value != 0);
            break;

        case Type::KippySeen:
            kippy_.onSample(ev.value, nowMs);
            break;
    }
}

void GatewayController::handleMatFrame(const events::Event& ev, uint32_t nowMs) {
    if (state_ != MatState::Streaming) return;

    if (!matframe::isValid(ev.payload, ev.length)) {
        ++invalidFrames_;
        Serial.printf("[MAT] Ungueltiger Rahmen (len %u), gesamt %lu\n",
                      ev.length, static_cast<unsigned long>(invalidFrames_));
        return;
    }

    lastFrameMs_ = nowMs;
    repeater_.forward(ev.payload, ev.length);

    // Sofort weiterreichen (kein Rate-Limit): die Matte liefert nur 1 Hz und
    // die Katzenklingel soll ohne Verzoegerung reagieren.
    const matframe::Decoded data = matframe::decode(ev.payload);
    mqtt_.publish(cfg::kTopicPressure, data.pressure);
    led_.pulse(nowMs);
}

// =============================================================================
// Matten-Zustandsautomat
// =============================================================================
void GatewayController::runMatStateMachine(uint32_t nowMs) {
    switch (state_) {
        case MatState::Connecting:
            // Initiator dauerhaft laufen lassen: abgelaufene Laeufe sofort erneuern.
            if (!attemptRunning_ && elapsedAtLeast(nowMs, retryAtMs_, 0)) {
                startAttempt(nowMs);
            }
            // Absicherung, falls NimBLE kein Ergebnis-Event liefert.
            if (attemptRunning_ &&
                elapsedAtLeast(nowMs, attemptStartMs_, cfg::kConnectTimeoutMs + cfg::kConnectGuardMs)) {
                mat_.cancelConnect();
                attemptRunning_ = false;
                retryAtMs_      = nowMs;
            }
            // Nach dem Booten: zu lange ohne Verbindung -> Problem (Alarm bei Bedarf).
            if (!linkProblem_ && elapsedAtLeast(nowMs, offlineSinceMs_, cfg::kStartupGraceMs)) {
                linkProblem_ = true;
            }
            // Sehr lange ohne Verbindung -> Matte reagiert womoeglich gar nicht mehr.
            if (!wakeHintSent_ && elapsedAtLeast(nowMs, offlineSinceMs_, cfg::kWakeHintMs)) {
                wakeHintSent_ = true;
                publishDebug(kMsgWakeHint);
            }
            break;

        case MatState::Discovering: {
            // Blockiert kurz (GATT-Discovery). Danach frische Zeit verwenden.
            const bool ok = mat_.subscribe();
            const uint32_t after = millis();
            if (ok) {
                enterStreaming(after);
            } else {
                mat_.disconnect();
                startConnecting(after, kMsgUnreachable, false);
                retryAtMs_ = after + cfg::kMinRetryGapMs;
            }
            break;
        }

        case MatState::Streaming:
            if (elapsedAtLeast(nowMs, lastFrameMs_, cfg::kDataWatchdogMs)) {
                mat_.disconnect();
                linkProblem_ = true;
                startConnecting(nowMs, kMsgNoData, true);
            }
            break;
    }
}

void GatewayController::enterState(MatState next, uint32_t nowMs) {
    if (next != state_) {
        Serial.printf("[MAT] %s -> %s\n", stateName(state_), stateName(next));
    }
    state_        = next;
    stateSinceMs_ = nowMs;
}

void GatewayController::startConnecting(uint32_t nowMs, const char* reason, bool newOutage) {
    if (reason != nullptr) publishDebug(reason);

    if (newOutage) {
        offlineSinceMs_ = nowMs;
        attempts_       = 0;
        wakeHintSent_   = false;
    }
    attemptRunning_ = false;
    retryAtMs_      = nowMs;

    enterState(MatState::Connecting, nowMs);
    publishOfflineValues(nowMs);
}

void GatewayController::startAttempt(uint32_t nowMs) {
    scanner_.stopNow();   // NimBLE kann nicht gleichzeitig scannen und initiieren
    ++attempts_;
    if (attempts_ == 1) publishDebug(kMsgConnecting);   // Folgelaeufe nur im seriellen Log

    attemptStartMs_ = nowMs;
    attemptRunning_ = mat_.startConnect();
    if (!attemptRunning_) {
        Serial.println("[MAT] Initiator konnte nicht gestartet werden.");
        retryAtMs_ = nowMs + cfg::kMinRetryGapMs;
    }
}

void GatewayController::enterStreaming(uint32_t nowMs) {
    Serial.printf("[MAT] Verbunden nach %lu s (%lu Lauf/Laeufe).\n",
                  static_cast<unsigned long>((nowMs - offlineSinceMs_) / 1000),
                  static_cast<unsigned long>(attempts_));
    enterState(MatState::Streaming, nowMs);
    lastFrameMs_   = nowMs;                                  // Watchdog-Startwert
    lastMatRssiMs_ = nowMs - cfg::kMatRssiIntervalMs;        // RSSI gleich senden
    linkProblem_   = false;
    publishDebug(kMsgReady);
}

void GatewayController::applyRadioUsers() {
    // Nur mit verbundener Matte: Kippy-Scan und Repeater-Advertising. Ohne Matte
    // bekommt der Initiator die volle Bluetooth-Funkzeit.
    const bool streaming = state_ == MatState::Streaming;
    scanner_.setMode(streaming ? BleScanner::Mode::Periodic : BleScanner::Mode::Off);
    repeater_.setAvailable(streaming);
}

// =============================================================================
// Bedarf und Alarm
// =============================================================================
bool GatewayController::hasDemand() const {
    return leaseActive_ || repeater_.isSubscribed();
}

void GatewayController::updateLease(uint32_t nowMs) {
    if (leaseActive_ && elapsedAtLeast(nowMs, leaseUntilMs_, 0)) {
        leaseActive_ = false;
        publishDebug(kMsgLeaseExpired);
        publishStreamState();
    }
}

void GatewayController::updateAlarm() {
    setAlarm(hasDemand() && linkProblem_);
}

void GatewayController::setAlarm(bool active) {
    if (alarm_ == active) return;
    alarm_ = active;
    Serial.printf("[ALARM] Verbindungsalarm %s\n", active ? "AN" : "AUS");
    // Ist MQTT gerade offline, sendet onMqttOnline() den Zustand nach.
    mqtt_.publish(cfg::kTopicDisconnect, active ? "ON" : "OFF", true);
}

// =============================================================================
// Veroeffentlichung
// =============================================================================
void GatewayController::publishDebug(const char* message) {
    Serial.printf("[STATUS] %s\n", message);
    lastDebug_ = message;
    mqtt_.publish(cfg::kTopicDebug, message, true);
}

void GatewayController::publishStreamState() {
    mqtt_.publish(cfg::kTopicCmdState, leaseActive_ ? "ON" : "OFF", true);
}

void GatewayController::publishOfflineValues(uint32_t nowMs) {
    mqtt_.publish(cfg::kTopicPressure, "unknown");
    mqtt_.publish(cfg::kTopicMatRssi, cfg::kOfflineRssi);
    lastOfflineRefreshMs_ = nowMs;
}

void GatewayController::publishPeriodic(uint32_t nowMs) {
    // Heartbeat: haelt "Proxy online" auf dem Tab5 (15-s-Fenster).
    if (elapsedAtLeast(nowMs, lastHeartbeatMs_, cfg::kHeartbeatMs)) {
        lastHeartbeatMs_ = nowMs;
        mqtt_.publish(cfg::kTopicWifiRssi, mqtt_.wifiRssi());
    }

    if (state_ == MatState::Streaming) {
        if (elapsedAtLeast(nowMs, lastMatRssiMs_, cfg::kMatRssiIntervalMs)) {
            lastMatRssiMs_ = nowMs;
            const int rssi = mat_.readRssi();
            // Gemessene Ausreisser (~ -110 dBm bei Parameterwechseln) verwerfen.
            if (rssi >= cfg::kRssiPlausibleMin && rssi < 0) {
                mqtt_.publish(cfg::kTopicMatRssi, rssi);
            }
        }
    } else if (elapsedAtLeast(nowMs, lastOfflineRefreshMs_, cfg::kOfflineRefreshMs)) {
        publishOfflineValues(nowMs);
    }

    int kippyRssi = 0;
    if (kippy_.update(nowMs, kippyRssi)) {
        mqtt_.publish(cfg::kTopicKippyRssi, kippyRssi);
    }
}

void GatewayController::updateLed() {
    led_.setPattern(state_ == MatState::Streaming ? StatusLed::Pattern::Streaming
                                                  : StatusLed::Pattern::Searching);
}

const char* GatewayController::stateName(MatState state) {
    switch (state) {
        case MatState::Connecting:  return "CONNECTING";
        case MatState::Discovering: return "DISCOVERING";
        case MatState::Streaming:   return "STREAMING";
    }
    return "?";
}
