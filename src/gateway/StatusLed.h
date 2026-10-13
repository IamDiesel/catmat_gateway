#pragma once
/**
 * @file StatusLed.h
 * @brief Status-LED (wie in der ersten Repeater-Version).
 *
 *   Searching  schnelles Blinken 5 Hz       (Matte suchen / verbinden)
 *   Streaming  dauerhaft an, flackert bei jedem Datenrahmen
 */

#include <Arduino.h>

class StatusLed {
public:
    enum class Pattern : uint8_t { Searching, Streaming };

    void begin(int pin);
    void setPattern(Pattern pattern) { pattern_ = pattern; }

    /** Kurzes Flackern bei Datentransfer (nur im Muster Streaming sichtbar). */
    void pulse(uint32_t nowMs) { lastPulseMs_ = nowMs; }

    void update(uint32_t nowMs);

private:
    int      pin_         = -1;
    Pattern  pattern_     = Pattern::Searching;
    uint32_t lastPulseMs_ = 0;
};
