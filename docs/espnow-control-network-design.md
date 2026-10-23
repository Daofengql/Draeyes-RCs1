# ESP-NOW Control Network Design

Status: current implementation baseline for RCs1, C3, and S3 firmware.

## Product Model

`Draeyes RCs1` is the baseline remote for all new remotes. It provides one
joystick and independent left and right eyelid control. The RCs1 has
no display or status LED.

One RCs1 can control multiple bound devices through a broadcast control stream.
S3 is currently a controlled receiver only; ESP-NOW Host and downstream-device
control are outside the current implementation.

The legacy RC STA / STA1 and C6RC remotes are discontinued and outside this
model.

## Radio Rules

- Every ESP-NOW frame uses Wi-Fi channel 1.
- Discovery, pairing, and normal control all share channel 1.
- There is no channel negotiation, channel survey, channel persistence, or
  channel authority decision.
- Normal control is fire-and-forget; no application-level ACK is required.
- A controlled device accepts ordinary requests only from its bound controller
  MAC. Permanent and temporary bindings retain their existing meanings.
- Pairing uses clear-text metadata, an explicit Pair new action, and MAC
  binding. It does not require a secret key exchange.

```text
RCs1  --->  C3 / S3 receiver
       all on Wi-Fi channel 1
```

## Coordinate Contract

RCs1 is the only side that calibrates the joystick. It stores each axis' raw
center and physical minimum/maximum, then sends calibrated X/Y values as
integers from `0` to `255`, where `127` is neutral. C3 and S3 map them
linearly to their screen offsets and do not capture another center.

The normalized `type:"preset"` payload remains a separate compatibility path
with coordinates from `-1.0` to `1.0`.

## Binding State

RCs1 always starts and operates on channel 1. For `SAVE`, S3 persists the
remote MAC and restores it on the next boot. For `ONCE`, a remote MAC exists
only while ESP-NOW is running. C3 controlled devices use runtime-only bindings.

S3 stores temporary and permanent remote MAC bindings. Both binding types use
channel 1.

## Pairing Flow

### RCs1 to Receiver

1. RCs1 normally transmits control data on channel 1.
2. Holding `R1 + R2` for 3 seconds starts pairing. RCs1 sends a `pairing`
   request immediately at the threshold, then repeats the same pairing session
   every 500ms while both buttons remain pressed.
3. Releasing either button stops pairing retries. Normal control packets are
   otherwise independent of the pairing request stream.
4. The receiver records the MAC according to its ONCE or SAVE policy. No
   `pair_accept`, `link_hello`, or `link_ok` response is required.

## Normal Operation

RCs1, C3, and S3 all start directly on channel 1. S3 restores permanent MAC
bindings at boot; temporary bindings are cleared when ESP-NOW is stopped or the
device restarts. Unknown MACs cannot execute normal control requests.

## RCs1 Input Design

- Joystick axes continuously control outgoing gaze position.
- The joystick button is a modifier: while held, `LB` sends `cp` and `RB` sends
  `cs`, suppressing normal eyelid action for that press.
- Independent eyelid mode is the default: `LB` controls left and `RB` controls
  right. `R2` long press toggles persistent linked mode, where `LB` controls
  both eyelids.
- `R1` triple press cycles manual, full automatic, and automatic blink modes.
- `R1` long press locks or unlocks joystick output in manual and automatic
  blink modes. Locking retains the last outgoing X/Y position.
- `R1 + R2` long press (3 seconds) starts pairing; the pairing request repeats
  every 500ms until either button is released.
- `R1 + R2 + LB + RB` long press starts a five-second joystick range capture;
  keep the stick centered at trigger time, then move both axes through all
  physical edges.

The removed locked automatic-blink mode is not part of the current behavior.
