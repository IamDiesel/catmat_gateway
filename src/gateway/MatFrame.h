#pragma once
/**
 * @file MatFrame.h
 * @brief Pruefen und Dekodieren der Datenrahmen der Katzenmatte.
 *
 * Reine Logik ohne Hardwarezugriff (testbar).
 *
 * Rahmen (10 Byte, gemessen 2026-10-11):
 *   [0]=A5 [1]=5A [2..4]=Meta (F0 0F 00) [5..6]=Druck big-endian
 *   [7]=Belegt-Flag (00/01) [8]=unbekannt (konstant 3C) [9]=99
 */

#include <stddef.h>
#include <stdint.h>

namespace matframe {

constexpr size_t kLength = 10;

struct Decoded {
    uint16_t pressure;  ///< Rohdruck (Tara/Schwelle wertet Lola OS aus)
    bool     occupied;  ///< Belegt-Flag der Matte (Byte 7)
};

/** @return true, wenn Laenge, Header und Footer stimmen. */
bool isValid(const uint8_t* data, size_t length);

/** Dekodiert einen gueltigen Rahmen. Vorher isValid() pruefen! */
Decoded decode(const uint8_t* data);

}  // namespace matframe
