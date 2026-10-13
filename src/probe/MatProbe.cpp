/**
 * @file MatProbe.cpp
 * @brief Diagnose-Firmware fuer die BLE-Katzenmatte (Lola OS).
 *
 * Zweck: Bevor das Gateway (Watchdog, Alarm-Timing, Energiesparen) ausgelegt
 * wird, messen wir, wie sich die Matte tatsaechlich verhaelt.
 *
 * Ablauf (einfache Zustandsmaschine in loop()):
 *
 *   SCAN ──Matte gesehen──▶ CONNECT ──ok──▶ DISCOVER ──abonniert──▶ STREAM
 *    ▲                         │               │                      │
 *    └──────── RETRY ◀─────────┴── Fehler ─────┴──── Verbindung weg ──┘
 *
 *   SCAN      Ist die Matte sichtbar? Advertising-Intervall und RSSI.
 *   CONNECT   Verbindungsaufbau (blockierend – in der Diagnose unkritisch).
 *   DISCOVER  Alle Services/Characteristics/Descriptors mit Properties
 *             ausgeben. Schreibbare Characteristics sind Kandidaten fuer
 *             Einstellungen (z. B. Sendeintervall).
 *   STREAM    Jede Notification mit Zeitabstand ausgeben, alle 30 s
 *             eine Statistik (min / Mittel / max / Jitter).
 *
 * Serielle Befehle (im PlatformIO-Monitor eintippen):
 *   l  laengeres Verbindungsintervall anfragen (Energiespar-Test)
 *   s  kurzes Verbindungsintervall anfragen
 *   r  Statistik zuruecksetzen
 *   q  Rohdaten-Zeilen an/aus (Statistik laeuft weiter)
 *
 * WICHTIG: Die Matte akzeptiert vermutlich nur eine Verbindung.
 *          Das bisherige Gateway vorher vom Strom trennen.
 *
 * Threading: NimBLE-Callbacks laufen im NimBLE-Host-Task. Sie schreiben nur
 * in eine Queue bzw. setzen atomare Flags. Alle Ausgaben passieren in loop().
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <cmath>

#include "secrets.h"

namespace {

// =============================================================================
// Konfiguration
// =============================================================================
constexpr uint32_t kScanDurationMs   = 10000;  ///< Dauer der Advertising-Messung
constexpr uint32_t kConnectTimeoutMs = 10000;  ///< Timeout Verbindungsaufbau
constexpr uint32_t kRetryDelayMs     = 5000;   ///< Pause vor neuem Versuch
constexpr uint32_t kStatsIntervalMs  = 30000;  ///< Abstand der Statistik-Ausgabe
constexpr uint32_t kParamCheckDelayMs = 3000;  ///< Wartezeit nach Param-Anfrage

constexpr size_t kMaxFrameLen = 20;            ///< Maximale Notification-Laenge
constexpr size_t kQueueDepth  = 64;            ///< Puffer Host-Task -> loop()

// Erwartetes Frame-Format der Matte (siehe Lola-OS-Doku):
//   [0]=A5 [1]=5A [2..4]=Meta [5..6]=Druck (BE) [7..8]=Status/Counter [9]=99
constexpr size_t  kFrameLen    = 10;
constexpr uint8_t kFrameHead0  = 0xA5;
constexpr uint8_t kFrameHead1  = 0x5A;
constexpr uint8_t kFrameFooter = 0x99;

// Verbindungsparameter fuer die Tests (Einheiten laut BLE-Spezifikation:
// Intervall 1,25 ms, Supervision-Timeout 10 ms)
constexpr uint16_t kLongInterval   = 80;   ///< 100 ms
constexpr uint16_t kLongLatency    = 4;    ///< Matte darf 4 Events auslassen
constexpr uint16_t kShortInterval  = 24;   ///< 30 ms
constexpr uint16_t kShortLatency   = 0;
constexpr uint16_t kSupervisionTimeout = 400;  ///< 4 s

const NimBLEUUID kMatServiceUuid(static_cast<uint16_t>(0xFFF0));
const NimBLEUUID kMatDataCharUuid(static_cast<uint16_t>(0xFFF1));
const NimBLEUUID kBatteryLevelUuid(static_cast<uint16_t>(0x2A19));
const NimBLEUUID kCharDeclarationUuid(static_cast<uint16_t>(0x2803));

// =============================================================================
// Datentypen
// =============================================================================

/** Eine empfangene Notification inkl. Empfangszeitpunkt. */
struct RawFrame {
    uint32_t timestampUs;
    uint8_t  length;
    uint8_t  bytes[kMaxFrameLen];
};

/** Laufende Statistik ueber Zeitabstaende (Welford-frei, reicht hier). */
struct IntervalStats {
    uint32_t count   = 0;
    double   sumMs   = 0.0;
    double   sumSqMs = 0.0;
    double   minMs   = INFINITY;
    double   maxMs   = 0.0;

    void add(double dtMs) {
        ++count;
        sumMs   += dtMs;
        sumSqMs += dtMs * dtMs;
        if (dtMs < minMs) minMs = dtMs;
        if (dtMs > maxMs) maxMs = dtMs;
    }
    double mean() const { return count ? sumMs / count : 0.0; }
    double stddev() const {
        if (count < 2) return 0.0;
        const double m = mean();
        const double var = sumSqMs / count - m * m;
        return var > 0.0 ? std::sqrt(var) : 0.0;
    }
};

enum class Phase { Scan, Connect, Discover, Stream, Retry };

// =============================================================================
// Zustand (geteilt zwischen NimBLE-Host-Task und loop())
// =============================================================================
QueueHandle_t gFrameQueue = nullptr;
std::atomic<uint32_t> gDroppedFrames{0};     ///< Queue voll -> verworfen
std::atomic<bool>     gScanFinished{false};
std::atomic<bool>     gDisconnected{false};
std::atomic<int>      gDisconnectReason{0};

// Advertising-Messung: wird nur im Host-Task geschrieben und erst nach
// gScanFinished in loop() gelesen.
volatile uint32_t gAdvCount     = 0;
volatile uint32_t gAdvFirstUs   = 0;
volatile uint32_t gAdvLastUs    = 0;
volatile int32_t  gAdvRssiSum   = 0;

// Nur in loop() verwendet
NimBLEAddress gMatAddress;
NimBLEClient* gClient = nullptr;
Phase         gPhase  = Phase::Scan;
uint32_t      gPhaseSinceMs   = 0;
uint32_t      gLastStatsMs    = 0;
uint32_t      gParamCheckAtMs = 0;     ///< 0 = keine Pruefung ausstehend
uint32_t      gLastFrameUs    = 0;
bool          gHaveLastFrame  = false;
bool          gPrintRaw       = true;

IntervalStats gStats;
uint32_t gValidFrames   = 0;
uint32_t gInvalidFrames = 0;

// =============================================================================
// Hilfsfunktionen
// =============================================================================
void printHex(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) Serial.printf("%02X ", data[i]);
}

/** Gibt druckbare Bytes zusaetzlich als Text aus (hilfreich bei Namen/Versionen). */
void printAscii(const uint8_t* data, size_t length) {
    Serial.print("\"");
    for (size_t i = 0; i < length; ++i) {
        const char c = static_cast<char>(data[i]);
        Serial.print(isprint(static_cast<unsigned char>(c)) ? c : '.');
    }
    Serial.print("\"");
}

bool isValidFrame(const RawFrame& f) {
    return f.length == kFrameLen &&
           f.bytes[0] == kFrameHead0 &&
           f.bytes[1] == kFrameHead1 &&
           f.bytes[kFrameLen - 1] == kFrameFooter;
}

uint16_t readBigEndian16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void resetStats() {
    gStats = IntervalStats();
    gValidFrames = 0;
    gInvalidFrames = 0;
    gDroppedFrames = 0;
    gHaveLastFrame = false;
    gLastStatsMs = millis();
}

void enterPhase(Phase next) {
    gPhase = next;
    gPhaseSinceMs = millis();
}

void printConnParams() {
    if (!gClient || !gClient->isConnected()) return;
    const NimBLEConnInfo info = gClient->getConnInfo();
    Serial.printf("[CONN] Intervall %.2f ms | Latenz %u | Supervision %u ms | MTU %u | RSSI %d dBm\n",
                  info.getConnInterval() * 1.25f,
                  info.getConnLatency(),
                  info.getConnTimeout() * 10u,
                  info.getMTU(),
                  gClient->getRssi());
}

// =============================================================================
// NimBLE-Callbacks (laufen im Host-Task – nur Queue/Flags beschreiben!)
// =============================================================================
class ProbeScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* device) override {
        if (device->getAddress() != gMatAddress) return;
        const uint32_t now = micros();
        if (gAdvCount == 0) gAdvFirstUs = now;
        gAdvLastUs = now;
        gAdvRssiSum += device->getRSSI();
        ++gAdvCount;
    }
    void onScanEnd(const NimBLEScanResults& /*results*/, int /*reason*/) override {
        gScanFinished = true;
    }
};

class ProbeClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* /*client*/, int reason) override {
        gDisconnectReason = reason;
        gDisconnected = true;
    }
};

void onMatNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t length, bool /*isNotify*/) {
    RawFrame frame;
    frame.timestampUs = micros();
    frame.length = static_cast<uint8_t>(length > kMaxFrameLen ? kMaxFrameLen : length);
    memcpy(frame.bytes, data, frame.length);
    if (xQueueSend(gFrameQueue, &frame, 0) != pdTRUE) ++gDroppedFrames;
}

ProbeScanCallbacks   gScanCallbacks;
ProbeClientCallbacks gClientCallbacks;

// =============================================================================
// Phasen
// =============================================================================

/** SCAN: Advertising der Matte 10 s lang mitschneiden. */
void startScan() {
    Serial.printf("\n[SCAN] Suche Matte %s fuer %lu s ...\n",
                  gMatAddress.toString().c_str(),
                  static_cast<unsigned long>(kScanDurationMs / 1000));
    gAdvCount = 0;
    gAdvRssiSum = 0;
    gScanFinished = false;

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&gScanCallbacks, true);  // true = jedes Paket melden
    scan->setActiveScan(false);                     // passiv: kostet die Matte nichts
    scan->setDuplicateFilter(false);
    scan->setInterval(100);
    scan->setWindow(100);                           // durchgehend lauschen
    scan->setMaxResults(0);                         // keine Ergebnisse speichern
    scan->start(kScanDurationMs, false, true);
}

void finishScan() {
    if (gAdvCount == 0) {
        Serial.println("[SCAN] Matte NICHT gefunden. (Akku? Reichweite? Schon mit anderem Geraet verbunden?)");
        enterPhase(Phase::Retry);
        return;
    }
    const double spanMs = (gAdvLastUs - gAdvFirstUs) / 1000.0;
    const double advIntervalMs = gAdvCount > 1 ? spanMs / (gAdvCount - 1) : 0.0;
    Serial.printf("[SCAN] Matte gefunden: %lu Advertising-Pakete, Ø-Abstand ca. %.0f ms, Ø-RSSI %ld dBm\n",
                  static_cast<unsigned long>(gAdvCount), advIntervalMs,
                  static_cast<long>(gAdvRssiSum / static_cast<int32_t>(gAdvCount)));
    enterPhase(Phase::Connect);
}

/** CONNECT: blockierender Verbindungsaufbau. */
void connectToMat() {
    Serial.println("[CONN] Verbinde ...");
    gDisconnected = false;
    const uint32_t t0 = millis();
    if (!gClient->connect(gMatAddress)) {
        Serial.printf("[CONN] Fehlgeschlagen nach %lu ms.\n", millis() - t0);
        enterPhase(Phase::Retry);
        return;
    }
    Serial.printf("[CONN] Verbunden nach %lu ms.\n", millis() - t0);
    printConnParams();
    enterPhase(Phase::Discover);
}

/** DISCOVER: kompletten GATT-Baum ausgeben und FFF1 abonnieren. */
void discoverAndSubscribe() {
    Serial.println("[GATT] Services / Characteristics / Descriptors:");
    const std::vector<NimBLERemoteService*>& services = gClient->getServices(true);
    for (NimBLERemoteService* svc : services) {
        Serial.printf("  Service %s\n", svc->getUUID().toString().c_str());

        const std::vector<NimBLERemoteCharacteristic*>& chars = svc->getCharacteristics(true);
        for (NimBLERemoteCharacteristic* chr : chars) {
            String props;
            if (chr->canRead())             props += "READ ";
            if (chr->canWrite())            props += "WRITE ";
            if (chr->canWriteNoResponse())  props += "WRITE_NR ";
            if (chr->canNotify())           props += "NOTIFY ";
            if (chr->canIndicate())         props += "INDICATE ";
            Serial.printf("    Char %s  handle=0x%04X  [%s]\n",
                          chr->getUUID().toString().c_str(), chr->getHandle(), props.c_str());

            if (chr->canRead()) {
                const NimBLEAttValue value = chr->readValue();
                Serial.print("      Wert: ");
                printHex(value.data(), value.length());
                printAscii(value.data(), value.length());
                Serial.println();
                if (chr->getUUID() == kBatteryLevelUuid && value.length() >= 1) {
                    Serial.printf("      -> Batteriestand: %u %%\n", value.data()[0]);
                }
            }

            const std::vector<NimBLERemoteDescriptor*>& descs = chr->getDescriptors(true);
            for (NimBLERemoteDescriptor* desc : descs) {
                // 0x2803 ist die Deklaration der naechsten Characteristic,
                // kein echter Descriptor (Artefakt der Handle-Bereichssuche).
                if (desc->getUUID() == kCharDeclarationUuid) continue;
                Serial.printf("      Desc %s", desc->getUUID().toString().c_str());
                const NimBLEAttValue dv = desc->readValue();
                if (dv.length() > 0) {
                    Serial.print("  Wert: ");
                    printHex(dv.data(), dv.length());
                    printAscii(dv.data(), dv.length());
                }
                Serial.println();
            }
        }
    }

    NimBLERemoteService* matService = gClient->getService(kMatServiceUuid);
    NimBLERemoteCharacteristic* dataChar =
        matService ? matService->getCharacteristic(kMatDataCharUuid) : nullptr;
    if (!dataChar || !dataChar->canNotify() || !dataChar->subscribe(true, onMatNotify)) {
        Serial.println("[GATT] FFF0/FFF1 nicht gefunden oder Abo fehlgeschlagen.");
        gClient->disconnect();
        enterPhase(Phase::Retry);
        return;
    }

    Serial.println("[GATT] FFF1 abonniert. Messung laeuft (Befehle: l / s / r / q).\n");
    xQueueReset(gFrameQueue);
    resetStats();
    enterPhase(Phase::Stream);
}

/** STREAM: eine empfangene Notification auswerten und ausgeben. */
void handleFrame(const RawFrame& frame) {
    double dtMs = 0.0;
    if (gHaveLastFrame) {
        dtMs = (frame.timestampUs - gLastFrameUs) / 1000.0;
        gStats.add(dtMs);
    }
    gLastFrameUs = frame.timestampUs;
    gHaveLastFrame = true;

    const bool valid = isValidFrame(frame);
    valid ? ++gValidFrames : ++gInvalidFrames;

    if (!gPrintRaw) return;
    Serial.printf("[DATA] %+8.1f ms | len %2u | ", dtMs, frame.length);
    printHex(frame.bytes, frame.length);
    if (valid) {
        Serial.printf("| Druck %5u | Status 0x%04X\n",
                      readBigEndian16(&frame.bytes[5]), readBigEndian16(&frame.bytes[7]));
    } else {
        Serial.println("| UNGUELTIGES FORMAT");
    }
}

void printStats() {
    Serial.printf("\n[STAT] %lu gueltig, %lu ungueltig, %lu verworfen | "
                  "Abstand min %.1f / Ø %.1f / max %.1f ms | Jitter σ %.1f ms\n",
                  static_cast<unsigned long>(gValidFrames),
                  static_cast<unsigned long>(gInvalidFrames),
                  static_cast<unsigned long>(gDroppedFrames.load()),
                  gStats.count ? gStats.minMs : 0.0, gStats.mean(), gStats.maxMs, gStats.stddev());
    printConnParams();
    Serial.println();
}

void requestConnParams(uint16_t interval, uint16_t latency) {
    Serial.printf("[CONN] Frage Intervall %.2f ms, Latenz %u an ...\n", interval * 1.25f, latency);
    if (!gClient->updateConnParams(interval, interval, latency, kSupervisionTimeout)) {
        Serial.println("[CONN] Anfrage wurde lokal abgelehnt.");
        return;
    }
    gParamCheckAtMs = millis() + kParamCheckDelayMs;   // Ergebnis spaeter pruefen
}

void handleSerialCommands() {
    while (Serial.available()) {
        const char c = static_cast<char>(Serial.read());
        if (gPhase != Phase::Stream && c != 'q') continue;
        switch (c) {
            case 'l': requestConnParams(kLongInterval, kLongLatency);   break;
            case 's': requestConnParams(kShortInterval, kShortLatency); break;
            case 'r': resetStats(); Serial.println("[STAT] Zurueckgesetzt."); break;
            case 'q': gPrintRaw = !gPrintRaw;
                      Serial.printf("[INFO] Rohdaten %s\n", gPrintRaw ? "an" : "aus"); break;
            default: break;
        }
    }
}

}  // namespace

// =============================================================================
// Arduino-Einstiegspunkte
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n===== Lola CatMat – Mess-Firmware (mat_probe) =====");

    gFrameQueue = xQueueCreate(kQueueDepth, sizeof(RawFrame));
    gMatAddress = NimBLEAddress(std::string(SECRET_MAC_MAT), BLE_ADDR_PUBLIC);

    NimBLEDevice::init("CatMat-Probe");
    NimBLEDevice::setPower(9);   // NimBLE 2.x erwartet dBm (nicht ESP_PWR_LVL_*)

    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCallbacks, false);
    gClient->setConnectTimeout(kConnectTimeoutMs);

    startScan();
    enterPhase(Phase::Scan);
}

void loop() {
    handleSerialCommands();

    // Verbindungsabbruch hat Vorrang vor allen Phasen
    if (gDisconnected && (gPhase == Phase::Discover || gPhase == Phase::Stream)) {
        gDisconnected = false;
        printStats();
        Serial.printf("[CONN] Verbindung getrennt, Grund 0x%02X.\n", gDisconnectReason.load());
        enterPhase(Phase::Retry);
    }

    switch (gPhase) {
        case Phase::Scan:
            if (gScanFinished) finishScan();
            break;

        case Phase::Connect:
            connectToMat();
            break;

        case Phase::Discover:
            discoverAndSubscribe();
            break;

        case Phase::Stream: {
            RawFrame frame;
            while (xQueueReceive(gFrameQueue, &frame, 0) == pdTRUE) handleFrame(frame);

            const uint32_t now = millis();
            if (now - gLastStatsMs >= kStatsIntervalMs) {
                gLastStatsMs = now;
                printStats();
            }
            if (gParamCheckAtMs != 0 && static_cast<int32_t>(now - gParamCheckAtMs) >= 0) {
                gParamCheckAtMs = 0;
                Serial.print("[CONN] Nach Anfrage -> ");
                printConnParams();
            }
            break;
        }

        case Phase::Retry:
            if (millis() - gPhaseSinceMs >= kRetryDelayMs) {
                startScan();
                enterPhase(Phase::Scan);
            }
            break;
    }

    delay(5);
}
