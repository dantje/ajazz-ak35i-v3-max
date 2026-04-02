# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.3.0] - 2026-04-02

### Added
- **`ajazz keys` subcommand** for per-key RGB lighting control.
  - Read live per-key colour state (CMD 0xF5).
  - Write per-key colours with read-modify-write semantics (CMD 0x23).
  - `--set KEY COLOR` with CSS color names (148 names), `#RRGGBB` hex,
    or `R G B` integers.
  - `--hsv KEY H S V` for QMK-style 8-bit HSV input.
  - `--clear` and `--base COLOR` for building layouts from scratch.
  - `--brightness 0-100%` for per-key brightness without re-uploading data.
  - 14 named key groups: `frow`, `numrow`, `qrow`, `homerow`, `shiftrow`,
    `bottom`, `arrows`, `nav`, `syskeys`, `numpad`, `wasd`, `alphas`,
    `mods`, `all`.
  - Key specifiers: single name, comma list, named group, or numeric index.
- Lighting mode `custom` / `perkey` (0x80) for per-key custom RGB.
- Read commands in the protocol layer: `CMD_READ_ID` (0x05),
  `CMD_READ_LIGHTING` (0x12), `CMD_READ_PERKEY` (0xF5).
- `CMD_FINISH` (0xF0) for state machine cleanup after reads.
- Manpage (`ajazz.1`) with full `keys` documentation and examples.

### Changed
- PROTOCOL.md: corrected ACK flags for lighting and per-key data packets
  to match working code. Removed incorrect CMD_START from lighting sequence.
- PROTOCOL.md: added USB descriptor strings, interface 3 report-ID details,
  read command protocol, per-key custom lighting sections.

## [0.2.0] - 2026-03-21

Initial public release.

### Added
- `ajazz list` — discover and list HID interfaces.
- `ajazz time` — sync the display RTC to system time, with `--at` override.
- `ajazz light` — set RGB lighting mode, colour, brightness, speed, direction.
  20 animation modes from `off` to `shuttle`.
- `ajazz solid` — fill the 240x135 display with a solid colour.
- `ajazz image` — upload a static image (PNG, JPG, BMP, GIF first frame)
  with letterbox or fill scaling.
- `ajazz animation` — upload animated GIFs (up to 141 frames) with progress bar.
- HID protocol reference (`PROTOCOL.md`).
- Debian/Ubuntu `.deb` packaging via CPack.
- Udev rules for unprivileged access.
- Argument-parsing test suite (`tests/test_args.sh`).
