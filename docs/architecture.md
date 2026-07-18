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

The arc slider uses a finger-sized hit target, converts its displacement from center into discrete
counter-clockwise or clockwise events, and sends an encoder press when selected. A fast drag keeps
all host encoder steps but collapses local audio/haptic work into one feedback pulse per touch sample.
The planar joystick applies a small local dead zone and reaches full host distance before the edge,
then sends normalized direction data through `v.oai.rad` and returns to its center after release.

The serializer retains the reference 4 ms pacing between fragmented reports, and the background HID
worker applies the same inter-message pacing without blocking LVGL. Normal key, joystick, and encoder
events fit in one 63-byte report.

## Runtime scheduling

The ESP-IDF configuration pins the Bluetooth controller, Bluedroid host, and `main_task` to CPU0.
Stopwatch Micro therefore pins its HID TX, audio, microphone, vibration, and battery workers to
CPU0 as well, while LVGL rendering and 8 ms touch sampling run alone on CPU1 at priority 2. The main
loop yields for one RTOS tick on each iteration. This prevents Codex traffic and feedback work from
stealing touch/render time while keeping all host communication off the UI core.

Every accepted user input requests audio and vibration feedback through the HAL. The view keeps
rendering state local so protocol delivery is not coupled to an LVGL redraw.

## Host state path

Incoming JSON-RPC requests update a thread-state snapshot in the BLE service. The Agent page reads
that snapshot during its periodic refresh and maps the six host states to button labels and lights.
The parser validates request shape before publishing a new snapshot and responds to supported RPC
methods over the same GATT transport.

## Microphone boundary

Codex Micro's `ACT10` event controls push-to-talk in ChatGPT Desktop. ChatGPT captures the computer's
selected system microphone; the vendor HID/JSON-RPC protocol has no PCM audio message. Stopwatch
Micro may sample its MEMS microphone for the on-device level meter, but that sample is not host audio
and must not be used as evidence that ChatGPT received speech.

## Dependency boundary

Source dependencies are pinned in `repos.json` and materialized under the ignored `components/`
directory by `fetch_repos.py`. Locally patched dependencies must be either cleanly patchable or
already patched; a mismatched patch is a hard error rather than a silent skip.

The browser prototype in `web/index.html` is a review artifact, not firmware source. UI changes are
implemented independently in HTML/CSS/JavaScript and LVGL, then checked for interaction parity.
