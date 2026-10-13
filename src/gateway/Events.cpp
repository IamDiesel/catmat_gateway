/**
 * @file Events.cpp
 * @brief FreeRTOS-Queue als Bruecke NimBLE-Host-Task -> loop().
 */

#include "Events.h"

#include <atomic>

namespace events {
namespace {

QueueHandle_t         sQueue = nullptr;
std::atomic<uint32_t> sDropped{0};

void enqueue(const Event& ev) {
    if (sQueue == nullptr || xQueueSend(sQueue, &ev, 0) != pdTRUE) {
        ++sDropped;
    }
}

}  // namespace

void init(size_t depth) {
    if (sQueue == nullptr) sQueue = xQueueCreate(depth, sizeof(Event));
}

void post(Type type, int32_t value) {
    Event ev{};
    ev.type  = type;
    ev.value = value;
    enqueue(ev);
}

void postPayload(Type type, const uint8_t* data, size_t length) {
    Event ev{};
    ev.type   = type;
    ev.length = static_cast<uint8_t>(length > kMaxPayload ? kMaxPayload : length);
    memcpy(ev.payload, data, ev.length);
    enqueue(ev);
}

bool receive(Event& out) {
    return sQueue != nullptr && xQueueReceive(sQueue, &out, 0) == pdTRUE;
}

uint32_t droppedCount() {
    return sDropped.load();
}

}  // namespace events
