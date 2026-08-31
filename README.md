# Draeye RC Mini

ESP32-C3 ESP-NOW remote-control firmware based on the STA1 remote protocol.

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

All buttons are active-low and use the internal pull-up. The joystick button on GPIO3 is initialized for future use but is currently ignored. In manual mode, `LB` and `RB` independently control the left and right eyelids; the legacy linked-blink synchronization switch is not present on Mini.

## ESP-NOW protocol

The controller sends the same JSON envelope as STA1 every 10 ms:

```json
{"req":"controller","data":{"light":1.0,"j1PotX":127,"j1PotY":127,"bkl":1.0,"bkr":1.0}}
```

The default peer address is the broadcast address `FF:FF:FF:FF:FF:FF`, and the Wi-Fi channel is `1`. Change both in `include/config.h` when the receiver requires a fixed unicast address or another channel.

The pairing request adds optional device metadata for receivers that support a
named binding list. Legacy C3 receivers continue to use the `req` field only:

```json
{"req":"pairing","type":"remote","name":"Draeye RC Mini","caps":0}
{"req":"change"}
```

## Current controls

- Automatic blink remains available in the automatic modes; manual `LB` controls only the left eyelid and `RB` controls only the right eyelid.
- `R1` triple press cycles manual, full automatic, automatic blink, and locked automatic blink modes.
- `R2` triple press sends the `change` request.
- `R2` long press locks or unlocks joystick control in manual and automatic-blink modes.
- `R1 + R2` long press sends the pairing request.
- Joystick center calibration is stored in EEPROM during first boot; `R1 + R2 + LB + RB` recalibrates the joystick center.

The light value is fixed at `1.0` because the current Mini pin allocation has no light sensor. The R1 long-press dimming branch is therefore unavailable.

On first boot, keep the joystick centered while the firmware performs its initial calibration.

## Build

```powershell
pio run
pio run -t upload
pio device monitor
```
