/**
 * @file MatFrame.cpp
 * @brief Implementierung der Rahmenpruefung der Katzenmatte.
 */

#include "MatFrame.h"

namespace matframe {
namespace {
constexpr uint8_t kHead0  = 0xA5;
constexpr uint8_t kHead1  = 0x5A;
constexpr uint8_t kFooter = 0x99;
}  // namespace

bool isValid(const uint8_t* data, size_t length) {
    return data != nullptr &&
           length == kLength &&
           data[0] == kHead0 &&
           data[1] == kHead1 &&
           data[kLength - 1] == kFooter;
}

Decoded decode(const uint8_t* data) {
    Decoded d;
    d.pressure = static_cast<uint16_t>((data[5] << 8) | data[6]);
    d.occupied = data[7] != 0;
    return d;
}

}  // namespace matframe
