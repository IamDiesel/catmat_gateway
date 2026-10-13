/**
 * @file MatLink.cpp
 * @brief Implementierung der BLE-Verbindung zur Katzenmatte.
 */

#include "MatLink.h"

#include "Config.h"
#include "Events.h"

namespace {

const NimBLEUUID kServiceUuid(static_cast<uint16_t>(0xFFF0));
const NimBLEUUID kDataCharUuid(static_cast<uint16_t>(0xFFF1));

/** Callbacks laufen im NimBLE-Host-Task: nur Events posten. */
class MatClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* /*client*/) override {
        events::post(events::Type::MatConnected);
    }
    void onConnectFail(NimBLEClient* /*client*/, int reason) override {
        events::post(events::Type::MatConnectFailed, reason);
    }
    void onDisconnect(NimBLEClient* /*client*/, int reason) override {
        events::post(events::Type::MatDisconnected, reason);
    }
};

MatClientCallbacks sCallbacks;

void onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t length, bool /*isNotify*/) {
    events::postPayload(events::Type::MatFrame, data, length);
}

}  // namespace

void MatLink::begin(const char* macAddress) {
    address_ = NimBLEAddress(std::string(macAddress), BLE_ADDR_PUBLIC);
    client_  = NimBLEDevice::createClient();
    client_->setClientCallbacks(&sCallbacks, false);
    client_->setConnectTimeout(cfg::kConnectTimeoutMs);
}

bool MatLink::startConnect() {
    // deleteAttributes = true: GATT-Cache bei jedem Aufbau neu lesen.
    // asyncConnect = true: kehrt sofort zurueck, Ergebnis kommt als Event.
    return client_->connect(address_, true, true, true);
}

void MatLink::cancelConnect() {
    client_->cancelConnect();
}

bool MatLink::subscribe() {
    NimBLERemoteService* service = client_->getService(kServiceUuid);
    if (service == nullptr) return false;

    NimBLERemoteCharacteristic* dataChar = service->getCharacteristic(kDataCharUuid);
    if (dataChar == nullptr || !dataChar->canNotify()) return false;

    return dataChar->subscribe(true, onNotify);
}

void MatLink::disconnect() {
    if (client_->isConnected()) client_->disconnect();
}

bool MatLink::isConnected() const {
    return client_ != nullptr && client_->isConnected();
}

int MatLink::readRssi() const {
    return isConnected() ? client_->getRssi() : cfg::kOfflineRssi;
}

const char* MatLink::reasonText(int reason) {
    return NimBLEUtils::returnCodeToString(reason);
}

bool MatLink::isTimeout(int reason) {
    return reason == BLE_HS_ETIMEOUT;
}
