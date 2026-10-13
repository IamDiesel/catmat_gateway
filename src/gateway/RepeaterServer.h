#pragma once
/**
 * @file RepeaterServer.h
 * @brief BLE-Peripheral "CatMat-EXT": spiegelt die Matte fuer ein Tablet/Dongle.
 *
 * Bietet dieselbe Schnittstelle wie die echte Matte (Service FFF0,
 * Characteristic FFF1 READ/NOTIFY) und leitet empfangene Rahmen 1:1 weiter.
 *
 * Zustandsautomat (wird ueber Events aus loop() gefuehrt):
 *
 *   ADVERTISING ──Client verbindet──▶ CONNECTED ──FFF1 abonniert──▶ SUBSCRIBED
 *        ▲                                │  ▲                          │
 *        │                                │  └──── Abo beendet ─────────┘
 *        └──────────────── Client getrennt ─────────────────────────────┘
 *
 * Verfuegbarkeit: Geworben wird nur, solange die echte Matte verbunden ist
 * (setAvailable). Ohne Matte gibt es nichts weiterzuleiten, und das eigene
 * Advertising wuerde dem Verbindungsaufbau zur Matte Funkzeit nehmen.
 *
 * Nur SUBSCRIBED erzeugt Bedarf – ein bloss verbundener Client zaehlt nicht.
 */

#include <NimBLEDevice.h>

class RepeaterServer {
public:
    enum class State : uint8_t { Advertising, Connected, Subscribed };

    /** Legt Server, Service und Characteristic an (Advertising erst per setAvailable). */
    void begin();

    /** Matte verbunden -> werben; Matte weg -> Advertising pausieren. */
    void setAvailable(bool available);

    // --- Zustandsuebergaenge (aus loop(), ausgeloest durch Events) ---
    void onClientConnected();
    void onClientDisconnected();
    void onSubscriptionChanged(bool subscribed);

    /** Leitet einen Rahmen weiter, falls ein Client abonniert hat. */
    void forward(const uint8_t* data, size_t length);

    State state() const { return state_; }
    bool  isSubscribed() const { return state_ == State::Subscribed; }
    bool  isConnected() const { return state_ != State::Advertising; }

private:
    void applyAdvertising();

    NimBLECharacteristic* dataChar_  = nullptr;
    State                 state_     = State::Advertising;
    bool                  available_ = false;
};
