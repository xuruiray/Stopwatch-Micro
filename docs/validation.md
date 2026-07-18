# Validation

Use evidence from all applicable layers before treating a release as complete.

## Source and dependency checks

```bash
python3 -m py_compile fetch_repos.py
git diff --check
```

Confirm that `repos.json` contains pinned versions and that `fetch_repos.py` fails when a local patch
matches neither the clean nor already-applied state.

## Browser prototype

Open `web/index.html` in a browser and check:

- Pairing is the only interactive screen before connection.
- Pairing success opens Command; disconnect returns to Pairing.
- Fast, Approve, Decline, and Fork have large independent hit targets.
- The joystick respects its invisible circular limit and returns to center.
- The arc slider sends discrete feedback and returns to its midpoint.
- Holding A opens Mic, its visualization animates, and release returns to the previous page.
- B simulates Send and A+B toggles Command/Agent.

## Firmware build

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py build
```

The build must complete without compile/link errors and fit in the configured application partition.

## USB serial diagnostics

Flash the firmware, keep ChatGPT connected for the transport/UI checks, and run:

```bash
source "$HOME/esp/esp-idf/export.sh"
python3 -u tools/serial_debug_test.py --port /dev/cu.usbmodem21301
```

The automated runner verifies the USB receive/transmit handshake, eight HAL readiness signals,
heap integrity and PSRAM, display geometry, battery telemetry, audio configuration, BLE service,
all 12 physical Codex control codes, the three encoder codes, report framing, an actual neutral HID
transmission, Command/Agent/Mic UI construction, and live microphone samples. A zero exit code means
no command returned `FAIL`; `OBSERVE` and `SKIP` are reported separately and must not be promoted to
`PASS` without the missing evidence.

Use `--interactive` for tests requiring a human observer. During `debug inputs`, normal UI/host
actions are suppressed so A, B, and touch can be exercised without sending Approve, Decline, or
other Codex actions. Audio, vibration, and display commands prove that the firmware requested the
actuation; audible, physical, and visual effects still require observation. The bond-erasing
`debug pairing-reset CONFIRM` command is destructive and is not part of the default suite.

The default suite also runs a 3-second 50 Hz neutral-HID performance test. A valid run must keep
`dropped=0`, `failures=0`, `queue_high` bounded, `touch_gap_max_us` below 50000, and report
`lvgl_core=1` with `tx_core=0`. Capture real physical interaction separately with:

```bash
python3 -u tools/serial_debug_test.py --port /dev/cu.usbmodem21301 --trace-seconds 30
```

The trace is expected to fail when no physical control is operated; that is an absence of test
input, not evidence that the input path passed.

## Device and host

After flashing, capture serial evidence for a clean boot with no panic or LVGL stack warning, then
verify the following against ChatGPT Settings:

1. The device is discovered as `Codex Micro`, connects, reconnects, and returns to Pairing after a
   disconnect.
2. `AG00`–`AG05`, `ACT06`–`ACT10`, and `ACT12` each trigger their mapped host action.
3. Agent labels and lights follow host thread updates.
4. The enlarged arc-slider target produces `ENC_CC`, `ENC`, and `ENC_CW` without stalling during a
   fast drag; the planar joystick crosses the host action threshold before reaching the visual edge
   and produces `v.oai.rad` direction updates.
5. Holding physical A starts host push-to-talk, the meter reacts to sound captured by the built-in
   MEMS microphone, releasing A stops push-to-talk, physical B sends, and A+B only toggles pages.
6. Every accepted input produces audible and haptic feedback without making touch or page changes
   sluggish.
7. Holding the bottom touch sensor for 3 seconds erases stored bonds, restarts, and advertises for a
   fresh pairing.

The Mic meter reads the StopWatch's local MEMS microphone. It does not prove that host audio is being
captured or transported: the compatibility protocol only triggers host push-to-talk, ChatGPT uses the
computer's selected microphone, and the BLE vendor HID channel does not stream StopWatch PCM audio.
