/**
 * @file main.cpp
 * @brief Einstiegspunkt des CatMat-Gateways (Lola OS).
 *
 * Funktionen:
 *   1. BLE-Repeater "CatMat-EXT" fuer die Katzenmatte
 *   2. Gateway Matte -> MQTT (kompatibel zu Lola OS / Tab5 und Home Assistant)
 *   3. Kippy-Signalstaerke -> MQTT
 *
 * Hier wird nur verdrahtet. Die Logik liegt im GatewayController, die
 * Mechanik in den einzelnen Modulen (siehe Architektur in GatewayController.h).
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_coexist.h>

#include "BleScanner.h"
#include "Config.h"
#include "Events.h"
#include "GatewayController.h"
#include "MatLink.h"
#include "MqttBridge.h"
#include "RepeaterServer.h"
#include "StatusLed.h"
#include "secrets.h"

namespace {

constexpr size_t kEventQueueDepth = 48;   ///< > 30 s Matten-Frames bei 1 Hz

MatLink           sMatLink;
RepeaterServer    sRepeater;
BleScanner        sScanner;
MqttBridge        sMqtt;
StatusLed         sLed;
GatewayController sController(sMatLink, sRepeater, sScanner, sMqtt, sLed);

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n[BOOT] Lola CatMat Gateway %s\n", cfg::kFirmwareVersion);

    events::init(kEventQueueDepth);
    sLed.begin(cfg::kLedPin);

    // BLE: ein Stack fuer Repeater (Peripheral), Matte (Central) und Scan (Matte + Kippy).
    NimBLEDevice::init(cfg::kRepeaterName);
    NimBLEDevice::setPower(cfg::kTxPowerDbm);
    sRepeater.begin();
    sMatLink.begin(SECRET_MAC_MAT);
    sScanner.begin(SECRET_MAC_MAT, SECRET_MAC_KIPPY);

    sMqtt.begin([](bool on) { sController.onStreamCommand(on, millis()); },
                []() { sController.onMqttOnline(millis()); });

    // WLAN und BLE teilen sich ein Funkteil: bei Konflikten bekommt Bluetooth
    // den Vorrang (die Matte ist der zeitkritische Pfad, MQTT verkraftet das).
    if (esp_coex_preference_set(ESP_COEX_PREFER_BT) != ESP_OK) {
        Serial.println("[BOOT] Funk-Prioritaet konnte nicht gesetzt werden.");
    }

    sController.begin(millis());
    Serial.println("[BOOT] Setup fertig.");
}

void loop() {
    const uint32_t now = millis();
    sMqtt.update(now);
    sController.update(now);
    sLed.update(now);
    delay(5);
}
