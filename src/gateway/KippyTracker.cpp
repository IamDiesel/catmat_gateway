/**
 * @file KippyTracker.cpp
 * @brief Implementierung der Kippy-Auswertung.
 */

#include "KippyTracker.h"

#include "Config.h"

namespace {
bool elapsedAtLeast(uint32_t nowMs, uint32_t sinceMs, uint32_t durationMs) {
    return static_cast<int32_t>(nowMs - sinceMs) >= static_cast<int32_t>(durationMs);
}
}  // namespace

void KippyTracker::begin(uint32_t nowMs) {
    lastReportMs_ = nowMs;
    lastSeenMs_   = nowMs;   // Abwesenheit fruehestens nach kKippyAbsentMs melden
}

void KippyTracker::onSample(int rssi, uint32_t nowMs) {
    if (rssi < cfg::kRssiPlausibleMin || rssi >= 0) return;   // Messfehler
    rssiSum_ += rssi;
    ++rssiSamples_;
    lastSeenMs_     = nowMs;
    reportedAbsent_ = false;
}

bool KippyTracker::update(uint32_t nowMs, int& rssiOut) {
    if (!elapsedAtLeast(nowMs, lastReportMs_, cfg::kKippyReportMs)) return false;
    lastReportMs_ = nowMs;

    if (rssiSamples_ > 0) {
        rssiOut      = static_cast<int>(rssiSum_ / rssiSamples_);
        rssiSum_     = 0;
        rssiSamples_ = 0;
        return true;
    }

    if (!reportedAbsent_ && elapsedAtLeast(nowMs, lastSeenMs_, cfg::kKippyAbsentMs)) {
        rssiOut         = cfg::kOfflineRssi;
        reportedAbsent_ = true;
        return true;
    }
    return false;
}
