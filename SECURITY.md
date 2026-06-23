# Security Policy

## Reporting a vulnerability

If you believe you've found a security issue in this project, **please do
not open a public issue**. Report it privately via GitHub's Security
Advisories:

https://github.com/Kilnside-Technologies/hot-tub-controller/security/advisories/new

We aim to acknowledge reports within a week. Coordinated disclosure once a
fix is available is appreciated.

## Scope

In scope:
- Vulnerabilities in the ESPHome firmware (`esphome/*`) — e.g. anything that
  lets an attacker on the local WiFi compromise the ESP32 or trigger
  unintended writes to the spa board.
- Vulnerabilities in the vendored kgstorm decoder component
  (`esphome/components/inputs/`).
- Anything in the docs that could lead a builder to a meaningfully unsafe
  wiring or configuration.

Out of scope (please report upstream):
- Generic ESPHome platform vulnerabilities — [esphome/esphome](https://github.com/esphome/esphome).
- Generic Home Assistant vulnerabilities — [home-assistant/core](https://github.com/home-assistant/core).
- The Balboa GS501Z hardware/firmware itself — that's the manufacturer.

## Hardware safety, not security

This repo also touches a 5 V topside bus on a hot-tub mainboard. If you've
found a wiring or pulse-width or current-limit issue that could damage the
spa board or the ESP32, that's not a vulnerability in the security sense —
just file an issue, ideally with measurements.
