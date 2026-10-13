#pragma once
/**
 * @file Events.h
 * @brief Ereignisse vom NimBLE-Host-Task an die Hauptschleife.
 *
 * Architekturregel: BLE-Callbacks laufen im NimBLE-Host-Task und duerfen
 * KEINE Logik ausfuehren (kein MQTT, keine Zustandswechsel). Sie legen nur ein
 * Event in die Queue. Die loop() ist alleiniger Besitzer aller Zustaende.
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace events {

constexpr size_t kMaxPayload = 20;  ///< maximale Notification-Laenge der Matte

enum class Type : uint8_t {
    MatConnected,            ///< Verbindung zur Matte steht (async connect)
    MatConnectFailed,        ///< Verbindungsaufbau fehlgeschlagen, value = Grund
    MatDisconnected,         ///< Verbindung getrennt, value = Grund
    MatFrame,                ///< Notification der Matte (payload)
    RepeaterConnected,       ///< Client (Tablet) am Repeater verbunden
    RepeaterDisconnected,    ///< Client am Repeater getrennt
    RepeaterSubscription,    ///< Client hat FFF1 (ab)abonniert, value = 1/0
    KippySeen,               ///< Kippy-Advertising empfangen, value = RSSI
};

struct Event {
    Type     type;
    int32_t  value;
    uint8_t  length;
    uint8_t  payload[kMaxPayload];
};

/** Legt die Queue an. Einmalig in setup() vor allen BLE-Modulen aufrufen. */
void init(size_t depth);

/** Aus dem NimBLE-Host-Task: Event ohne Nutzdaten senden (nicht blockierend). */
void post(Type type, int32_t value = 0);

/** Aus dem NimBLE-Host-Task: Event mit Nutzdaten senden (nicht blockierend). */
void postPayload(Type type, const uint8_t* data, size_t length);

/** Aus loop(): naechstes Event holen. @return false, wenn Queue leer. */
bool receive(Event& out);

/** Anzahl verworfener Events (Queue voll) – Diagnose. */
uint32_t droppedCount();

}  // namespace events
