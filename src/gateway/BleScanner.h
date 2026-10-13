#pragma once
/**
 * @file BleScanner.h
 * @brief Passiver BLE-Scan nach dem Kippy-Halsband (nur Mechanik).
 *
 * Die Matte wird NICHT ueber diesen Scanner gesucht: sie wirbt nach einiger
 * Zeit ohne Verbindung nur noch sehr selten. Der Verbindungsaufbau lauscht
 * deshalb selbst dauerhaft (Initiator in MatLink). NimBLE kann nicht
 * gleichzeitig scannen und initiieren – gescannt wird daher nur, solange die
 * Matte verbunden ist.
 *
 * Betriebsarten (vom GatewayController je Matten-Zustand gesetzt):
 *
 *   OFF         kein Scan (Matte wird gesucht/verbunden)
 *   PERIODIC    3-s-Fenster alle 15 s, geringe Funklast (Matte verbunden)
 *
 * Diagnose: Pakete der Matte werden mitgezaehlt und mit der Statistik
 * ausgegeben.
 *
 * Hinweis: NimBLEScan::stop() loest KEIN onScanEnd aus. Der Zustand wird
 * daher immer direkt ueber isScanning() abgefragt, nie ueber Callbacks.
 */

#include <NimBLEDevice.h>

class BleScanner {
public:
    enum class Mode : uint8_t { Off, Periodic };

    void begin(const char* matMac, const char* kippyMac);

    /** Gewuenschte Betriebsart; wirksam mit dem naechsten update(). */
    void setMode(Mode mode) { mode_ = mode; }

    /** Stoppt den Scan sofort (vor einem Verbindungsaufbau) und setzt Mode OFF. */
    void stopNow();

    /** Zyklisch aus loop() aufrufen. */
    void update(uint32_t nowMs);

    Mode mode() const { return mode_; }

private:
    bool startScan(uint32_t durationMs, uint16_t windowMs);

    /** Diagnose: alle kScanStatsIntervalMs Paketzaehler ins serielle Log. */
    void logStatistics(uint32_t nowMs);

    Mode     mode_          = Mode::Off;
    uint32_t lastWindowMs_  = 0;       ///< Start des letzten periodischen Fensters
    uint32_t lastStatsMs_   = 0;
    uint32_t startFailures_ = 0;
};
