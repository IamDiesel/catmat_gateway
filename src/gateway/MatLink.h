#pragma once
/**
 * @file MatLink.h
 * @brief BLE-Central-Verbindung zur Katzenmatte (NimBLE-Client).
 *
 * Kapselt nur die Mechanik (verbinden, abonnieren, trennen). Wann verbunden
 * wird, entscheidet der GatewayController. Ein Verbindungsaufbau ist ein
 * NimBLE-Initiator: er lauscht bis kConnectTimeoutMs auf das naechste
 * Advertising der Matte und verbindet sofort beim ersten Paket.
 * Alle Rueckmeldungen kommen als Events (MatConnected, MatConnectFailed,
 * MatDisconnected, MatFrame).
 *
 * Verbindungsparameter: Es werden bewusst KEINE eigenen angefordert. Die
 * Matte verlangt selbst 225 ms / Latenz 4, NimBLE akzeptiert das standardmaessig.
 */

#include <NimBLEDevice.h>

class MatLink {
public:
    /** Erzeugt den Client. NimBLEDevice::init() muss vorher gelaufen sein. */
    void begin(const char* macAddress);

    /** Startet einen asynchronen Verbindungsaufbau. @return false bei Sofortfehler. */
    bool startConnect();

    /** Bricht einen laufenden Verbindungsaufbau ab. */
    void cancelConnect();

    /**
     * Sucht FFF0/FFF1 und abonniert die Notifications.
     * Blockiert kurz (GATT-Discovery, typ. < 1 s) – nur aus loop() aufrufen.
     */
    bool subscribe();

    /** Trennt die Verbindung (Event MatDisconnected folgt). */
    void disconnect();

    bool isConnected() const;

    /** Aktueller RSSI der Verbindung (ungefiltert). */
    int readRssi() const;

    /** Klartext zu einem NimBLE/HCI-Fehlercode (fuer das Log). */
    static const char* reasonText(int reason);

    /** true, wenn ein Verbindungsaufbau nur mangels Advertising abgelaufen ist. */
    static bool isTimeout(int reason);

private:
    NimBLEClient* client_ = nullptr;
    NimBLEAddress address_;
};
