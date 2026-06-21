# CLAUDE.md — Satellite1-ESPHome

Quick-reference context for Claude sessions working in this repo. Read this first.

## What this is

Open-source **ESPHome firmware** for the [FutureProofHomes Satellite1 Core Board](https://futureproofhomes.net/products/satellite1-core-board) — a private, AI-powered Home Assistant voice assistant + multisensor. The firmware runs on an **ESP32-S3** and pairs with an **XMOS** audio co-processor (on the optional HAT board) for echo cancellation and audio processing.

- Upstream repo: `FutureProofHomes/Satellite1-ESPHome` (origin)
- Docs: https://docs.futureproofhomes.net
- License: ESPHome License (GPL-based) — see `LICENSE`
- Distributed/consumed as an **ESPHome external package** (dashboard import), not a standalone flashable app.

## Architecture at a glance

This repo is **two layers**:

1. **YAML config** (`config/`) — the ESPHome device definition: packages, substitutions, automations, hardware wiring. This is what users import via the ESPHome Device Builder dashboard.
2. **Native C++/Python external components** (`esphome/components/`) — custom ESPHome components implementing the hardware drivers and the ESP32↔XMOS protocol.

### The ESP32 ↔ XMOS relationship (most important concept)

- The **ESP32-S3** runs ESPHome (WiFi, HA API, voice pipeline, LEDs, sensors).
- The **XMOS** chip (on the HAT) does mic capture + acoustic echo cancellation (AEC) + audio DSP.
- They communicate over **SPI** (`spi_0`, MODE3, 8 MHz, CS=GPIO10, XMOS reset=GPIO4). See `esphome/components/satellite1/satellite1.h` / `.cpp` for the wire protocol (resource IDs, DFU controller, GPIO servicer, status registers).
- The ESP32 can **flash the XMOS firmware over SPI** at runtime (`memory_flasher` component). XMOS firmware images are downloaded from the FutureProofHomes Documentation repo and the version is pinned via `xmos_fw_version` (currently `v1.0.3`). On boot, if the embedded image doesn't match the connected XMOS version, it re-flashes automatically (see `satellite1.yaml` `on_xmos_connected` / `on_xmos_no_response`).

## Directory map

```
config/                       # ESPHome YAML (the device definition)
  satellite1.yaml             # Top-level build entrypoint (terminal builds, GHA)
  satellite1.base.yaml        # Core device config; pulls in all common/ packages
  satellite1.dashboard.yaml   # Entrypoint for ESPHome dashboard imports (remote package)
  satellite1.ld2410.yaml      # Variant: LD2410 mmWave radar
  satellite1.ld2450.yaml      # Variant: LD2450 mmWave radar
  common/                     # Modular package includes
    core_board.yaml           # Hardware buses: esp32/psram/i2c/spi/uart/i2s + sdkconfig
    voice_assistant.yaml      # HA voice pipeline + microWakeWord
    media_player.yaml         # Speaker-based media player (TTS, music streaming)
    speaker.yaml, timer.yaml
    led_ring.yaml             # 360° LED ring animations
    hat_sensors.yaml          # temp/humidity/light sensors (HAT board)
    mmwave*.yaml              # mmWave radar presence detection (ld2410/ld2450)
    buttons.yaml              # volume/action/mute buttons
    home_assistant.yaml, wifi_improv.yaml, wifi_credentials.yaml
    sendspin.yaml             # snapcast/multi-room audio
    debug.yaml, developer.yaml, dashboard_build.yaml, components.external.yaml

esphome/components/           # Custom native components (C++ + Python codegen)
  satellite1/                 # Core ESP32↔XMOS SPI service + sub-platforms:
    audio_dac/ (dac_proxy)    #   - DAC selection proxy (speaker vs line-out)
    light/ (led_ring)         #   - LED ring driver
    memory_flasher/           #   - XMOS firmware flashing over SPI
    microphone/               #   - SPI-fed microphone source
    runtime_testing/          #   - SPI error-rate diagnostics
  satellite1_radar/           # mmWave radar (LD2410/LD2450) + tuner UI web server
  fusb302b/                   # USB-C Power Delivery negotiation (PD contract)
  tas2780/                    # Class-D speaker amplifier driver
  pcm5122/                    # PCM5122 DAC + GPIO
  i2s_audio/                  # I2S mic + speaker (shared duplex bus)
  memory_flasher/             # Base memory_flasher platform

scripts/setup_build_env.sh    # Creates .venv + installs requirements.txt
docs/                         # pr-review-merge-and-release-workflow.md
.github/workflows/            # build_latest, build_release, build, lint, docs dispatch
```

## Key features (firmware capabilities)

- Home Assistant voice assistant pipeline with **on-device wake word** (microWakeWord, default `hey_jarvis`)
- XMOS-based **acoustic echo cancellation** and audio DSP
- Music streaming (HA Media Browser / Music Assistant), TTS announcements
- Multi-room audio via snapcast (`sendspin`)
- Temp/humidity/light sensors; optional **mmWave radar** presence detection (LD2410 or LD2450)
- 360° LED ring with notification animations
- Volume/action buttons, hardware + software mute
- USB-C Power Delivery (FUSB302B) — negotiates voltage to drive the amplifier
- OTA updates with a **Beta firmware** toggle (switches OTA manifest source)
- Improv-over-BLE onboarding; safe mode

## Build & dev workflow

Terminal builds (from project root):
```bash
source scripts/setup_build_env.sh      # create/activate .venv, install deps
esphome compile config/satellite1.yaml
esphome upload  config/satellite1.yaml
esphome logs    config/satellite1.yaml
```

- **ESPHome version is pinned in `requirements.txt`** (currently `esphome==2026.4.5`) and is the single source of truth — CI derives the build version from it.
- Most users build via the **ESPHome Device Builder dashboard**, which imports `config/satellite1.dashboard.yaml` from the repo (default ref `staging`). The dashboard ref/version must stay compatible with the ESPHome version — breaking changes between releases can break dashboard builds.
- Native components are loaded as `external_components` (local path `../esphome/components`) in `satellite1.yaml`; the dashboard path pulls them from git instead.

## Branch & release model

Three-tier promotion lane (see `docs/pr-review-merge-and-release-workflow.md`):

- `develop` — feature/fix intake (current working branch). **Release category labels are set here on the original PR.**
- `staging` — beta lane; **dashboard default ref**. Beta releases tagged `vX.Y.Z-beta.N` from here.
- `main` — production lane. Production releases tagged `vX.Y.Z`.

Flow: `develop → staging → main` via promotion PRs (labeled `promotion`/`sync`, often `skip-changelog`). **Releases are tag-driven**, not PR-label driven — `build_release` runs on pushed `v*` tags and creates draft GitHub releases. Release notes are generated from tag commit ranges and resolve commit SHAs back to original PR labels — so PR titles should be customer-facing and PRs to `develop` need a release category label (`feature`/`enhancement`/`improvement`/`fix`/`breaking`/`internal`).

## Things to note / gotchas

- **Don't bump ESPHome casually** — version compatibility between `requirements.txt`, the codebase, and the dashboard ref is tightly coupled. Verify the dashboard/firmware combination.
- **XMOS firmware version** (`xmos_fw_version`, currently `v1.0.3`) is pinned in several YAMLs (`satellite1.base.yaml`, `satellite1.dashboard.yaml`) and points at external assets in the Documentation repo. Keep them in sync if bumping.
- Hardware revisions exist (rev2/rev4) and there are many long-lived feature/experiment branches — confirm which board revision and branch you're targeting before debugging hardware issues.
- The repo targets **ESP32-S3 with octal PSRAM @ 80MHz**, esp-idf framework, 16MB flash — see `config/common/core_board.yaml` for the authoritative pin map and sdkconfig.
- Python files in `esphome/components/*/__init__.py` (and `*.py`) are ESPHome **codegen** (config schema → C++), not runtime code. Edit these to change YAML config options.
- C++ style is enforced via `.clang-format`; YAML via `.yamllint`; both wired into `.pre-commit-config.yaml`.
- Pin map lives in `core_board.yaml`: I2C (SDA=5/SCL=6), SPI2 (CLK=12/MOSI=11/MISO=13), I2S shared duplex (LRCLK=7/BCLK=8/MCLK=16), mmWave UART (TX=43/RX=44 @ 256000 baud).
