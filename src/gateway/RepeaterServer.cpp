/**
 * @file RepeaterServer.cpp
 * @brief Implementierung des BLE-Repeaters "CatMat-EXT".
 */

#include "RepeaterServer.h"

#include "Config.h"
#include "Events.h"

namespace {

const NimBLEUUID kServiceUuid(static_cast<uint16_t>(0xFFF0));
const NimBLEUUID kDataCharUuid(static_cast<uint16_t>(0xFFF1));

/** Laufen im NimBLE-Host-Task: nur Events posten. */
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*info*/) override {
        events::post(events::Type::RepeaterConnected);
    }
    void onDisconnect(NimBLEServer* /*server*/, NimBLEConnInfo& /*info*/, int reason) override {
        events::post(events::Type::RepeaterDisconnected, reason);
    }
};

class DataCharCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* /*chr*/, NimBLEConnInfo& /*info*/, uint16_t subValue) override {
        events::post(events::Type::RepeaterSubscription, subValue != 0 ? 1 : 0);
    }
};

ServerCallbacks   sServerCallbacks;
DataCharCallbacks sCharCallbacks;

}  // namespace

void RepeaterServer::begin() {
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&sServerCallbacks, false);
    // Advertising wird selbst gesteuert (setAvailable / Client getrennt),
    // nicht automatisch von NimBLE nach jeder Trennung.
    server->advertiseOnDisconnect(false);

    NimBLEService* service = server->createService(kServiceUuid);
    dataChar_ = service->createCharacteristic(kDataCharUuid,
                                              NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    dataChar_->setCallbacks(&sCharCallbacks);

    // GATT-Server JETZT starten, solange noch keine Verbindung besteht.
    // NimBLE startet ihn sonst erst beim ersten Advertising – dann ist die
    // Matte bereits verbunden, und ble_gatts_reset() schlaegt mit einer
    // Assertion fehl (Absturz in ble_svc_gap_init, gesehen in v2.4.0).
    if (!server->start()) {
        Serial.println("[REP] GATT-Server konnte nicht gestartet werden.");
    }

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(kServiceUuid);
    adv->setName(cfg::kRepeaterName);

    state_ = State::Advertising;   // Advertising selbst erst per setAvailable()
}

void RepeaterServer::setAvailable(bool available) {
    if (available_ == available) return;
    available_ = available;
    applyAdvertising();
}

void RepeaterServer::onClientConnected() {
    state_ = State::Connected;
}

void RepeaterServer::onClientDisconnected() {
    state_ = State::Advertising;
    applyAdvertising();   // wieder sichtbar, falls die Matte verbunden ist
}

void RepeaterServer::onSubscriptionChanged(bool subscribed) {
    if (state_ == State::Advertising) return;   // verspaetetes Event nach Trennung
    state_ = subscribed ? State::Subscribed : State::Connected;
}

void RepeaterServer::forward(const uint8_t* data, size_t length) {
    if (!isSubscribed() || dataChar_ == nullptr) return;
    dataChar_->setValue(data, length);   // auch fuer READ aktuell halten
    dataChar_->notify();
}

void RepeaterServer::applyAdvertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    const bool shouldAdvertise = available_ && state_ == State::Advertising;

    if (shouldAdvertise && !adv->isAdvertising()) {
        adv->start();
    } else if (!shouldAdvertise && adv->isAdvertising()) {
        adv->stop();
    }
}
