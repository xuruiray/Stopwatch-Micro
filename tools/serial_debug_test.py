#!/usr/bin/env python3
"""Run Stopwatch Micro's machine-readable USB serial diagnostics."""

from __future__ import annotations

import argparse
import glob
import re
import sys
import time
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on the ESP-IDF environment
    raise SystemExit("pyserial is required; source the ESP-IDF export script first") from exc


RESULT_RE = re.compile(r"DBG RESULT command=([^ ]+) status=([^ ]+)(?: (.*))?")


@dataclass
class Result:
    command: str
    status: str
    details: str


def discover_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(candidates) != 1:
        raise SystemExit(
            "expected one /dev/cu.usbmodem* device; pass --port explicitly. "
            f"Found: {candidates or 'none'}"
        )
    return candidates[0]


class DebugClient:
    def __init__(self, port: str) -> None:
        # Configure control lines before opening so attaching diagnostics does
        # not create an avoidable DTR/RTS reset pulse.
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=1)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()

    def handshake(self, timeout: float = 12.0) -> None:
        deadline = time.monotonic() + timeout
        next_ping = time.monotonic()
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_ping:
                self.serial.write(b"debug ping\n")
                next_ping = now + 1.0
            raw = self.serial.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            print(line)
            if "DBG READY " in line:
                next_ping = time.monotonic()
            match = RESULT_RE.search(line)
            if match and match.group(1) == "ping" and match.group(2) == "PASS":
                return
        raise TimeoutError("serial debug handshake timed out")

    def close(self) -> None:
        self.serial.close()

    def command(self, text: str, expected: str, timeout: float = 5.0) -> Result:
        self.serial.write((text + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            print(line)
            match = RESULT_RE.search(line)
            if match and match.group(1) == expected and match.group(2) not in {"RUNNING"}:
                return Result(match.group(1), match.group(2), match.group(3) or "")
        raise TimeoutError(f"timed out waiting for {expected!r} after {text!r}")


def run_automated(client: DebugClient) -> tuple[list[Result], list[str]]:
    cases = [
        ("debug help", "help", 3.0),
        ("debug status", "status", 3.0),
        ("debug selftest", "selftest", 8.0),
        ("debug controls", "controls", 5.0),
        ("debug protocol", "protocol", 3.0),
        ("debug ui cycle", "ui-cycle", 5.0),
        ("debug transport", "transport", 5.0),
        ("debug perf 3000", "perf", 6.0),
        ("debug mic 2500", "mic", 5.0),
        ("debug status", "status", 3.0),
        ("debug pairing-reset", "pairing-reset", 3.0),
    ]
    results: list[Result] = []
    failures: list[str] = []
    for command, expected, timeout in cases:
        result = client.command(command, expected, timeout)
        results.append(result)
        if result.status == "FAIL":
            failures.append(f"{expected}: {result.details}")
    return results, failures


def run_interactive(client: DebugClient, failures: list[str]) -> None:
    print("\nInteractive checks: watch the screen and feel/listen to the device.")
    tone = client.command("debug tone 880 350", "tone")
    if input("Did you hear one tone? [y/N] ").strip().lower() != "y":
        failures.append(f"tone: not confirmed ({tone.details})")

    vibration = client.command("debug vibrate 500 80", "vibrate")
    if input("Did you feel one vibration? [y/N] ").strip().lower() != "y":
        failures.append(f"vibrate: not confirmed ({vibration.details})")

    print("Within 20 seconds press yellow A, press blue B, then touch/drag the display.")
    inputs = client.command("debug inputs 20000", "inputs", timeout=23.0)
    if inputs.status != "PASS":
        failures.append(f"inputs: {inputs.status} {inputs.details}")

    if input("Did the UI cycle Command -> Agent -> Mic -> Command without artifacts? [y/N] ").strip().lower() != "y":
        failures.append("display: UI cycle not visually confirmed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="USB Serial/JTAG port; auto-detected when unique")
    parser.add_argument("--interactive", action="store_true", help="also run physical observation checks")
    parser.add_argument(
        "--trace-seconds",
        type=int,
        help="capture real A/B/touch/joystick/slider transport performance instead of the automated suite",
    )
    args = parser.parse_args()
    port = discover_port(args.port)
    print(f"HOST port={port}")

    client = DebugClient(port)
    try:
        client.handshake()
        if args.trace_seconds is not None:
            duration = max(1, min(args.trace_seconds, 60))
            print(f"HOST TRACE duration={duration}s; operate the physical controls now")
            time.sleep(1.0)
            trace = client.command(f"debug trace {duration * 1000}", "trace", timeout=duration + 4.0)
            results = [trace]
            failures = [] if trace.status == "PASS" else [f"trace: {trace.status} {trace.details}"]
        else:
            results, failures = run_automated(client)
            if args.interactive:
                run_interactive(client, failures)
    except (TimeoutError, serial.SerialException) as exc:
        print(f"HOST ERROR {exc}")
        return 2
    finally:
        client.close()

    counts: dict[str, int] = {}
    for result in results:
        counts[result.status] = counts.get(result.status, 0) + 1
    summary = " ".join(f"{key.lower()}={counts[key]}" for key in sorted(counts))
    print(f"HOST SUMMARY {summary} failures={len(failures)}")
    for failure in failures:
        print(f"HOST FAILURE {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
