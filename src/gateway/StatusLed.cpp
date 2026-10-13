/**
 * @file StatusLed.cpp
 * @brief Implementierung der Status-LED.
 */

#include "StatusLed.h"

namespace {
constexpr uint32_t kSearchPeriodMs = 200;
constexpr uint32_t kPulseOffMs     = 50;
}  // namespace

void StatusLed::begin(int pin) {
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);
}

void StatusLed::update(uint32_t nowMs) {
    if (pin_ < 0) return;

    bool on = false;
    switch (pattern_) {
        case Pattern::Searching:
            on = (nowMs % kSearchPeriodMs) < (kSearchPeriodMs / 2);
            break;
        case Pattern::Streaming:
            on = (nowMs - lastPulseMs_) >= kPulseOffMs;
            break;
    }
    digitalWrite(pin_, on ? HIGH : LOW);
}
