/**
 * @file BleScanner.cpp
 * @brief Implementierung des Kippy-Scanners.
 */

#include "BleScanner.h"

#include <atomic>

#include "Config.h"
#include "Events.h"

namespace {

NimBLEAddress sMatAddress;
NimBLEAddress sKippyAddress;

// Diagnose-Zaehler (Host-Task schreibt, loop() liest und setzt zurueck)
std::atomic<uint32_t> sPacketsTotal{0};
std::atomic<uint32_t> sPacketsMat{0};
std::atomic<uint32_t> sPacketsKippy{0};

/** Vergleicht nur die 6 Adress-Bytes (unabhaengig vom Adresstyp public/random). */
bool sameMac(const NimBLEAddress& a, const NimBLEAddress& b) {
    return memcmp(a.getBase()->val, b.getBase()->val, 6) == 0;
}

/** Laufen im NimBLE-Host-Task: nur Events posten und zaehlen. */
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        ++sPacketsTotal;
        const NimBLEAddress& addr = device->getAddress();
        if (sameMac(addr, sKippyAddress)) {
            ++sPacketsKippy;
            events::post(events::Type::KippySeen, device->getRSSI());
        } else if (sameMac(addr, sMatAddress)) {
            ++sPacketsMat;   // nur Diagnose (verbunden wirbt die Matte nicht)
        }
    }
};

ScanCallbacks sCallbacks;

/** Ueberlaufsicherer Zeitvergleich. */
bool elapsedAtLeast(uint32_t nowMs, uint32_t sinceMs, uint32_t durationMs) {
    return static_cast<int32_t>(nowMs - sinceMs) >= static_cast<int32_t>(durationMs);
}

}  // namespace

void BleScanner::begin(const char* matMac, const char* kippyMac) {
    sMatAddress   = NimBLEAddress(std::string(matMac), BLE_ADDR_PUBLIC);
    sKippyAddress = NimBLEAddress(std::string(kippyMac), BLE_ADDR_PUBLIC);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&sCallbacks, true);  // jedes Paket melden
    scan->setActiveScan(false);                 // passiv: kostet den Kippy nichts
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);                     // nichts speichern, nur Callbacks

    lastWindowMs_ = millis() - cfg::kKippyScanPeriodMs;   // erstes Fenster sofort
    lastStatsMs_  = millis();
}

void BleScanner::stopNow() {
    mode_ = Mode::Off;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
}

void BleScanner::update(uint32_t nowMs) {
    NimBLEScan* scan    = NimBLEDevice::getScan();
    const bool  running = scan->isScanning();

    switch (mode_) {
        case Mode::Off:
            if (running) scan->stop();
            break;

        case Mode::Periodic:
            if (!running && elapsedAtLeast(nowMs, lastWindowMs_, cfg::kKippyScanPeriodMs)) {
                lastWindowMs_ = nowMs;
                if (!startScan(cfg::kKippyScanWindowMs, cfg::kScanWindowPeriodicMs)) {
                    ++startFailures_;
                }
            }
            break;
    }

    logStatistics(nowMs);
}

bool BleScanner::startScan(uint32_t durationMs, uint16_t windowMs) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setInterval(cfg::kScanIntervalMs);
    scan->setWindow(windowMs);
    return scan->start(durationMs, false, true);
}

void BleScanner::logStatistics(uint32_t nowMs) {
    if (!elapsedAtLeast(nowMs, lastStatsMs_, cfg::kScanStatsIntervalMs)) return;
    lastStatsMs_ = nowMs;

    // Ohne Scan-Fenster gibt es nichts zu berichten (Matte wird gerade gesucht).
    const uint32_t total = sPacketsTotal.exchange(0);
    const uint32_t mat   = sPacketsMat.exchange(0);
    const uint32_t kippy = sPacketsKippy.exchange(0);
    if (mode_ == Mode::Off && total == 0) return;

    Serial.printf("[SCAN] Pakete %lu | Kippy %lu | Matte %lu",
                  static_cast<unsigned long>(total), static_cast<unsigned long>(kippy),
                  static_cast<unsigned long>(mat));
    if (startFailures_ > 0) {
        Serial.printf(" | Scan-Startfehler %lu", static_cast<unsigned long>(startFailures_));
        startFailures_ = 0;
    }
    Serial.println();
}
