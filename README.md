# Draeyes RCs1

ESP32-C3 ESP-NOW RCs1 remote-control firmware for the current Draeyes control
network. The discontinued RC STA / STA1 and C6RC remotes are not compatibility
targets.

## Hardware defaults

| Function | GPIO |
| --- | --- |
| Joystick X ADC | 0 |
| Joystick Y ADC | 1 |
| Joystick button | 3 |
| Left blink (`LB`) | 4 |
| Right blink (`RB`) | 5 |
| Function `R1` | 6 |
| Function `R2` | 7 |

All buttons are active-low and use the internal pull-up. The RCs1 has
no display or status LED. In manual mode, `LB` and `RB` independently control
the left and right eyelids by default; the legacy physical linked-blink switch
is not present on the RCs1.

## ESP-NOW protocol

The controller sends this JSON envelope every 10 ms on fixed Wi-Fi channel 1:

```json
{"req":"controller","data":{"light":1.0,"j1PotX":127,"j1PotY":127,"bkl":1.0,"bkr":1.0}}
```

The peer address is broadcast (`FF:FF:FF:FF:FF:FF`) so one RCs1 can control
multiple bound devices. Discovery, pairing, normal control, and S3 Host traffic
all use channel 1. No channel negotiation or channel persistence is used.

Pairing is sent on channel 1 and is considered complete immediately. The
receiver only records the sender MAC in runtime memory; no acceptance response
or follow-up handshake is required:

```json
{"req":"pairing","type":"remote","name":"Draeyes RCs1","channel":1,"nonce":123456}
{"req":"cp"}
{"req":"cs"}
```

## Current controls

- Automatic blink remains available in the automatic-blink and full-automatic modes.
- `LB` and `RB` independently control the two eyelids by default. `R2` long
  press toggles linked mode, where `LB` controls both eyelids; the setting is
  saved in EEPROM.
- Holding the joystick button and pressing `LB` sends `cp` (previous eye
  piece). Holding it and pressing `RB` sends `cs` (next eye piece).
- `R1` triple press cycles manual, full automatic, and automatic blink modes.
- `R1` long press locks or unlocks joystick control in manual and
  automatic-blink modes. Locking retains the last transmitted position.
- `R1 + R2` long press sends the first pairing request after 3 seconds, then
  repeats it every 500ms until either button is released.
- Joystick calibration is stored in EEPROM. On first boot, and when
  `R1 + R2 + LB + RB` is held for three seconds, the current filtered position
  is saved as the center immediately. The endpoint bounds are then learned
  continuously from normal operation and persisted in the background; there is
  no separate five-second capture period and control traffic is not paused.

The light value is fixed at `1.0` because the current RCs1 pin allocation has
no light sensor.

On first boot, keep the joystick centered briefly while the firmware records its
initial center. After that, use the full physical range normally; the firmware
gradually learns and saves the endpoint bounds. If the center needs to be reset,
hold the four calibration buttons for three seconds while the stick is centered.

For a 3.2 V joystick supply on ESP32-C3, place a 4.7 kOhm resistor in series
between `3V3` and the joystick `VCC` pin. Remove the two ADC `10 kOhm` pull-ups.
The joystick remains connected directly to GPIO0/GPIO1; its supply should be
about 1.6-1.8 V under load.

## Build

```powershell
pio run
pio run -t upload
pio device monitor
```

## License

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
Third-party build dependencies are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For security issues, see [SECURITY.md](SECURITY.md).
