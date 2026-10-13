#pragma once
/**
 * @file KippyTracker.h
 * @brief Auswertung der Kippy-Sichtungen (reine Logik, testbar).
 *
 * Sammelt RSSI-Werte aus dem Scanner und liefert alle kKippyReportMs einen
 * Mittelwert. Wird der Kippy laenger als kKippyAbsentMs nicht gesehen, wird
 * einmalig kOfflineRssi (-130) gemeldet.
 */

#include <stdint.h>

class KippyTracker {
public:
    void begin(uint32_t nowMs);

    /** Neue Sichtung (Event KippySeen). Unplausible Werte werden verworfen. */
    void onSample(int rssi, uint32_t nowMs);

    /**
     * Zyklisch aufrufen.
     * @return true, wenn ein Wert veroeffentlicht werden soll (in rssiOut).
     */
    bool update(uint32_t nowMs, int& rssiOut);

private:
    int32_t  rssiSum_        = 0;
    uint16_t rssiSamples_    = 0;
    uint32_t lastReportMs_   = 0;
    uint32_t lastSeenMs_     = 0;
    bool     reportedAbsent_ = false;
};
