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

## Device and host

After flashing, capture serial evidence for a clean boot with no panic or LVGL stack warning, then
verify the following against ChatGPT Settings:

1. The device is discovered as `Codex Micro`, connects, reconnects, and returns to Pairing after a
   disconnect.
2. `AG00`–`AG05`, `ACT06`–`ACT10`, and `ACT12` each trigger their mapped host action.
3. Agent labels and lights follow host thread updates.
4. The arc slider produces `ENC_CC`, `ENC`, and `ENC_CW`; the planar joystick produces
   `v.oai.rad` direction updates.
5. Holding physical A starts Mic, releasing A stops it, physical B sends, and A+B only toggles pages.
6. Every accepted input produces audible and haptic feedback without making touch or page changes
   sluggish.
7. Holding the bottom touch sensor for 3 seconds erases stored bonds, restarts, and advertises for a
   fresh pairing.

The Mic meter is intentionally not an amplitude test: the current compatibility transport does not
carry live microphone-level samples to the display.
