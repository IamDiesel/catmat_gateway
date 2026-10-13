# CatMat Gateway (Lola OS)

Firmware für den ESP32-WROOM-32U DevKit C, der zwischen der BLE-Katzenmatte und Lola OS (Tab5) bzw. Home Assistant vermittelt.

## Funktionen
1. **BLE-Repeater "CatMat-EXT":** spiegelt die Matte (Service `FFF0`, Characteristic `FFF1`) für ein Tablet oder einen Dongle.
2. **Gateway Matte → MQTT:** gibt jeden gültigen Rahmen sofort als Druckwert weiter, kompatibel zu Lola OS und mit HA-Discovery.
3. **Kippy → MQTT:** passiver Scan nach dem Halsband und Ausgabe der gemittelten Signalstärke.

## Environments
| Env | Zweck |
|---|---|
| `gateway` (Standard) | Produktiv-Firmware |
| `mat_probe` | Diagnose: misst Sendeintervall, GATT-Aufbau und Verbindungsparameter der Matte |

## Einrichtung
1. `src/config/secrets_dummy.h` nach `src/config/secrets.h` kopieren und die Werte eintragen. Die Datei `secrets.h` ist per `.gitignore` vom Repo ausgeschlossen.
2. In PlatformIO das Env `gateway` wählen und **Upload and Monitor** ausführen.

## Architektur
```
NimBLE-Host-Task                         Arduino loop() – einziger Besitzer
─────────────────                        ──────────────────────────────────
MatLink (Matte)          ─┐              ┌─▶ GatewayController (Bedarf, Zustandsautomat, Alarm)
RepeaterServer (Tablet)   ┼─▶ EventQueue ┼─▶   └─ KippyTracker (Mittelwert, Abwesenheit)
BleScanner (Kippy)       ─┘              ├─▶ MqttBridge (WLAN/MQTT, Discovery)
                                         └─▶ StatusLed
```
- BLE-Callbacks legen nur Events in die Queue. Logik, MQTT und Zustandswechsel laufen ausschließlich in `loop()`.
- Den Zustandsautomaten der Matte und die Bedarfsregel beschreibt `src/gateway/GatewayController.h`.
- Alle Zeiten und Topics stehen zentral in `src/gateway/Config.h`.

## Hardware der Katzenmatte
- **Basis:** handelsübliche Drucksensormatte mit BLE-Modul (BLE 4.2), [Original-Matte inkl. BLE-Modul](https://www.amazon.de/dp/B0F33254XQ). Für den Austausch gibt es eine [Ersatzmatte](https://www.amazon.de/dp/B000UAE1NM).
- **Akku-Umbau:** Ab Werk läuft das BLE-Modul mit einer LIR2440-Knopfzelle, die sich schnell entlädt. Für den Dauerbetrieb wurde es auf einen wiederaufladbaren Li-Ion-Akku umgelötet: **3,7 V, 800 mAh, Formfaktor E35** ([verwendeter Akku](https://www.amazon.de/dp/B09M3T234C)).
- **Bezug zum Funkverhalten:** Die Firmware des BLE-Moduls ist auf eine Knopfzelle ausgelegt. Das passt zu den beobachteten Energiespar-Maßnahmen der Matte: Sie fordert selbst lange Verbindungsintervalle an und wirbt unverbunden vermutlich nur selten. Der größere Akku ändert an diesem Verhalten nichts, er hält nur länger.
- **Batteriestand:** Über BLE ist **keiner** abrufbar (siehe Datenrahmen). Auch der Tab5 wertet keinen Batteriestand der Matte aus. Die Warnung bei „niedrigem Batteriestand der Sensormatte“ aus der früheren Lola-OS-Beschreibung (ESP32-S3-Hub) ist damit derzeit nicht umsetzbar.

## BLE-Protokoll der Katzenmatte
Ermittelt mit der Diagnose-Firmware (`mat_probe`) am 2026-10-11/13 und abgeglichen mit der früheren Lola-OS-Dokumentation. Ein offizielles Datenblatt gibt es nicht. Alle Angaben sind beobachtet, nicht spezifiziert.

### Gerät und GATT-Struktur
| Eigenschaft | Wert |
|---|---|
| Adresse | `20:24:08:29:13:F0`, Typ *public* (in `secrets.h`; Standard-MAC laut früherer Lola-OS-Doku) |
| BLE-Version | 4.2 (laut früherer Lola-OS-Doku) |
| Rolle | Peripheral, nimmt **nur eine** Verbindung gleichzeitig an |
| Service | `0xFFF0` |
| Characteristic `0xFFF1` (Handle `0x0005`) | READ, WRITE, WRITE_NR, **NOTIFY**, also der Datenkanal. CCCD `0x2902`, den die Matte über Verbindungen hinweg behält. Die frühere Doku nennt nur READ und NOTIFY, gemessen wurden zusätzlich WRITE und WRITE_NR. |
| Characteristic `0xFFF2` (Handle `0x0003`) | WRITE, WRITE_NR, vermutlich ein Befehlskanal. Das Protokoll ist unbekannt, deshalb **nicht blind beschreiben**. |
| Weitere Services | keine (kein Battery-, kein Device-Information-Service) |

### Verbindungsparameter
- **Beim Verbindungsaufbau:** 50 ms Intervall, Latenz 0, Supervision-Timeout 2,56 s.
- **Kurz danach fordert die Matte selbst** 225 ms Intervall, Latenz 4 und 6 s Supervision-Timeout an. Das ist ihre eigene Energiespar-Einstellung. Das Gateway akzeptiert sie und fordert nichts anderes an.
- **Andere Werte vom Central** (getestet: 100 ms/Latenz 4, 30 ms/Latenz 0) nimmt sie an. Das Sendeintervall bleibt trotzdem 1 s, nur das Ankunftsraster ändert sich.

### Datenrahmen (Notification auf `FFF1`)
Die Matte sendet **1 Rahmen pro Sekunde**, immer genau **10 Byte**:

```
Byte:   0    1    2    3    4    5    6    7    8    9
       A5   5A   F0   0F   00  P_hi P_lo  S1   S2   99
       └ Header ┘ └─ Meta ──┘  └ Druck ┘  └Status┘  Footer
```

| Byte | Bedeutung | Beobachtete Werte |
|---|---|---|
| 0–1 | Header | immer `A5 5A` |
| 2–4 | Meta (Bedeutung unbekannt) | immer `F0 0F 00` |
| 5–6 | **Druck**, 16 Bit big-endian | `00 00` unbelastet, z. B. `11 B3` = 4531 bei Belastung |
| 7 | vermutlich Belegt-Flag | `00` unbelastet, `01` belastet |
| 8 | unbekannt | immer `3C` (60). **Kein Batteriestand**, der Wert bleibt auch über lange Zeit konstant. |

Die frühere Lola-OS-Doku bezeichnet Byte 7–8 als „Status/Counter“. Hochzählen wurde nicht beobachtet: Byte 7 folgt der Belastung, Byte 8 ist konstant.
| 9 | Footer | immer `99` |

Beispiele:
```
A5 5A F0 0F 00 00 00 00 3C 99   -> Druck 0,    nicht belegt
A5 5A F0 0F 00 11 B3 01 3C 99   -> Druck 4531, belegt
```

### Parsing im Gateway (`src/gateway/MatFrame.*`)
Ein Rahmen gilt nur als gültig, wenn **Länge, Header und Footer** stimmen. Ungültige Rahmen werden verworfen, nicht weitergeleitet und setzen den Daten-Watchdog nicht zurück.

```cpp
// Gueltig: genau 10 Byte, Header A5 5A, Footer 99
bool isValid(const uint8_t* data, size_t length) {
    return data != nullptr && length == 10 &&
           data[0] == 0xA5 && data[1] == 0x5A && data[9] == 0x99;
}

// Nur nach isValid() aufrufen
Decoded decode(const uint8_t* data) {
    Decoded d;
    d.pressure = static_cast<uint16_t>((data[5] << 8) | data[6]);  // big-endian
    d.occupied = data[7] != 0;
    return d;
}
```

- **Weitergabe:** Der Rohdruck `pressure` geht unverändert als Dezimalzahl an `lolacatmat/sensor/pressure`. Tara-Offset und Schwellenwert wertet Lola OS (Tab5) aus, nicht das Gateway.
- **Repeater:** Er leitet gültige Rahmen **byte-genau** über seinen eigenen Service `FFF0`/`FFF1` weiter. Ein Client sieht damit dasselbe Protokoll wie bei der echten Matte.
- **Watchdog:** Kommt 3 s lang kein gültiger Rahmen, gilt die Verbindung als verloren. Das entspricht etwa 3 fehlenden Rahmen und ist schneller als der Supervision-Timeout der Matte von 6 s.

## Verhalten der Matte und Energiesparen
**Beobachtungen, nicht abschließend gesichert:**
- Direkt nach dem Einschalten und nach einem Verbindungsabbruch wirbt die Matte etwa alle 5 s.
- Nach einiger Zeit ohne Verbindung kamen nur noch selten einzelne Werbepakete, im Abstand von etwa 20 s bis über 2 min.

> **Einschränkung der Messungen:** Während der Messungen lässt sich nicht mit Gewissheit ausschließen, dass zeitweise ein anderes Gerät mit der Matte verbunden war oder um die Verbindung konkurriert hat. In Frage kommen etwa ein zweiter ESP32, der Tab5-Dongle oder ein Handy. Die Matte nimmt nur eine Verbindung an und wirbt nicht, solange sie verbunden ist. Ein solches „Rennen“ würde genauso aussehen wie seltenes Werben. Die Angaben zum langsamen Werbemodus sind deshalb als wahrscheinliches, aber nicht bewiesenes Verhalten zu verstehen. Für eine saubere Wiederholung müssen alle anderen BLE-Geräte, die die Matte kennen, aus sein.

Das Gateway ist so ausgelegt, dass es in **beiden** Fällen funktioniert:
- **Nie freiwillig trennen.** Im verbundenen Zustand spart die Matte selbst Strom (225 ms Intervall, Latenz 4).
- **Dauerhaft lauschender Verbindungsaufbau.** Ein NimBLE-Initiator läuft jeweils 30 s, danach startet sofort der nächste. Er verbindet beim ersten Werbepaket, egal ob die Matte selten wirbt oder gerade von einem anderen Gerät freigegeben wurde. Ein vorheriges „Sehen“ per Scan ist nicht nötig.
- **Nach einem Abbruch** ist die Matte typischerweise in ≤ 5 s wieder verbunden. Im langsamen Modus dauert es bis zum nächsten Werbepaket (beobachtet: bis ca. 3 min).
- **Hinweis nach 2 min:** Kommt so lange keine Verbindung zustande, meldet das Gateway „Matte antwortet nicht - ggf. Matte aus- und wieder einschalten.“

**Bedarf und Alarm:**
- **Bedarf** besteht, wenn ein Repeater-Client `FFF1` abonniert hat **oder** eine MQTT-Lease gültig ist. Er steuert nur den Verbindungsalarm, nicht die Verbindung.
- **Lease:** `ON` auf `stream/set` gilt 7 min, der Tab5 erneuert sie alle 3 min. `OFF` beendet sie sofort. Nach dem Booten gilt eine Lease als erteilt.
- **Alarm:** Er setzt Bedarf **und** ein Verbindungsproblem voraus. Bei einem Abbruch kommt er sofort, nach dem Booten erst, wenn nach 15 s noch keine Verbindung steht.

## Funk (WLAN und BLE)
Der ESP32 hat nur **ein** 2,4-GHz-Funkteil für WLAN und Bluetooth. Bei Konflikten hat Bluetooth Vorrang (`esp_coex_preference_set`). Kippy-Scan und Repeater-Advertising laufen nur, solange die Matte verbunden ist. Während des Verbindungsaufbaus hat der Initiator damit die volle Bluetooth-Funkzeit. Ein Zeitmultiplex mit abgeschaltetem WLAN (v2.2.0) erzeugte WLAN-Fehler und wurde wieder entfernt.

## MQTT-Schnittstelle
| Topic | Richtung | Inhalt |
|---|---|---|
| `lolacatmat/switch/stream/set` | → Gateway | `ON` (Lease erneuern) / `OFF` |
| `lolacatmat/switch/stream/state` | Gateway → | `ON`/`OFF` (retained) |
| `lolacatmat/sensor/pressure` | Gateway → | Druck (1 Hz) bzw. `unknown` |
| `lolacatmat/switch/disconnect/state` | Gateway → | `ON` bei Bedarf und Verbindungsproblem: sofort bei Abbruch, nach dem Booten nach 15 s ohne Verbindung (retained) |
| `lolacatmat/sensor/mat_rssi` | Gateway → | dBm alle 5 s, `-130` wenn nicht verbunden |
| `lolacatmat/sensor/kippy_rssi` | Gateway → | Mittelwert alle 15 s, `-130` nach 90 s ohne Sichtung (Scan nur bei verbundener Matte) |
| `lolacatmat/sensor/wifi_rssi` | Gateway → | Heartbeat alle 10 s |
| `lolacatmat/sensor/debug` | Gateway → | Status-Text (retained) |
| `lolacatmat/status` | Gateway → | `online` / `offline` (LWT, retained) |

## Status-LED
| Muster | Bedeutung |
|---|---|
| schnelles Blinken | Verbindungsaufbau zur Matte |
| dauerhaft an, flackert bei Daten | Matte streamt |

## Altes Projekt
Vorgänger siehe: https://github.com/IamDiesel/CatMat
