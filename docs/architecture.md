# Architecture

Stopwatch Micro is a single-purpose ESP-IDF firmware. It keeps hardware setup, the Codex Micro
compatibility transport, and the LVGL application separated so connection state and input handling
have one owner each.

## Startup and ownership

1. `main/main.cpp` initializes the StopWatch hardware abstraction and starts the BLE service.
2. `app_codex_micro` creates the only Mooncake application and polls physical inputs.
3. `CodexMicroView` owns the two LVGL pages, Pairing overlay, Mic overlay, touch controls, and their
   local animation state.
4. `CodexMicroBle` owns advertising, bonding, HID/GATT services, JSON-RPC input messages, and host
   state updates.

The BLE service outlives UI page changes. Connection state flows from `CodexMicroBle` to the view;
the view cannot dismiss Pairing without a live connection.

## Input path

Touch controls are handled by LVGL event callbacks. Physical A/B transitions are debounced by
`KeyManager` and consumed by `CodexMicroApp`. Host-facing inputs are encoded with the constants in
`main/hal/ble/codex_micro_protocol.h`; A+B is deliberately local and only toggles the active page.

The arc slider converts its displacement from center into discrete counter-clockwise or clockwise
events and sends an encoder press when selected. The planar joystick sends normalized direction data
through `v.oai.rad` and returns to its center after release.

Every accepted user input requests audio and vibration feedback through the HAL. The view keeps
rendering state local so protocol delivery is not coupled to an LVGL redraw.

## Host state path

Incoming JSON-RPC requests update a thread-state snapshot in the BLE service. The Agent page reads
that snapshot during its periodic refresh and maps the six host states to button labels and lights.
The parser validates request shape before publishing a new snapshot and responds to supported RPC
methods over the same GATT transport.

## Dependency boundary

Source dependencies are pinned in `repos.json` and materialized under the ignored `components/`
directory by `fetch_repos.py`. Locally patched dependencies must be either cleanly patchable or
already patched; a mismatched patch is a hard error rather than a silent skip.

The browser prototype in `web/index.html` is a review artifact, not firmware source. UI changes are
implemented independently in HTML/CSS/JavaScript and LVGL, then checked for interaction parity.
