# PLL_Lab_2026

**Status:** `L1 Build Verified / Flashable` (2026-07-24)

Correlation-lock platform for the 2023H signal separation system:
detect and track the A component inside C = A + B using AD630 + local DDS.

**Target MCU:** STM32F407ZGT6  
**Toolchain:** ARM GNU Toolchain (arm-none-eabi-gcc 14.3.1) + CMake + Ninja  
**ELF:** `build/Debug/PLL_Lab_2026` (55 KB flash / 3 KB RAM)

---

## Signal Chain

```
C = A + B  ──> AD630 SIG IN
DDS_REF     ──> AD630 REF IN   (AD9833-A)
AD630 VOUT  ──> LPF ──> ADS8688 CH0 ──> STM32 (PI controller)
                                          │
                             ┌────────────┘
                             ▼
                     DDS_REF freq/phase updated
                     DDS_OUT (AD9833-B) ──> A' (reconstructed A)
```

**Important:** The AD630 DC output represents the correlation between the
A component and the local reference — it is NOT the A waveform itself.
It is used ONLY for frequency/phase tracking. A's amplitude and waveform
type must come from the 2023H FFT/recognition pipeline or manual UART input.

---

## Operating Modes

| Mode | UART Command | DDS | AD630 | Loop |
|------|-------------|-----|-------|------|
| Self-Test | `mode selftest` | Both fixed | Not used | Open |
| Correlation Measure | `mode measure` | Fixed | Sampled, displayed | Open |
| Correlation Lock | `mode lock` | Adjusted by PI | Drives correction | Closed |

Default boot mode: **CORRELATION_MEASURE** (safe — never auto-locks).

---

## Quick Start

```bash
# Configure
cd PLL_Lab_2026
cmake --preset Debug -G "Ninja" \
  -DCMAKE_MAKE_PROGRAM="<path-to-ninja>"

# Build
cmake --build build/Debug

# Flash
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/Debug/PLL_Lab_2026 verify reset exit"
```

---

## UART Commands (USART1, 115200 8N1)

| Command | Args | Effect |
|---------|------|--------|
| `mode selftest` | — | Legacy: two DDS test each other |
| `mode measure` | — | Sample AD630 DC, no DDS changes |
| `mode lock` | — | Enable correlation lock (requires `lock start`) |
| `seed <hz>` | float | Set nominal A frequency |
| `wave sine` `wave triangle` `wave square` | — | Set DDS_OUT (A') waveform |
| `amp <value>` | float | **Reserved** — amplitude not auto-controlled |
| `output_phase <deg>` | float | Extra phase offset on DDS_OUT only |
| `lock start` | — | Start PI controller |
| `lock stop` | — | Stop PI controller |
| `lock reset` | — | Re-init lock state machine |
| `cal detector_zero` | — | Calibrate quadrature null target |
| `polarity normal` | — | Set control polarity +1 |
| `polarity invert` | — | Set control polarity -1 |
| `dds_ref_phase <deg>` | float | **Manual scan only** — override DDS_REF phase |
| `status` | — | Print current lock status |
| `reset` | — | Re-init DDS + signal_lock |
| `freq ref <hz>` | float | **Self-test only** — set DDS ch A |
| `freq ctrl <hz>` | float | **Self-test only** — set DDS ch B |

### Self-Test Legacy Commands

The `freq ref/ctrl` and `phase ref/ctrl` commands only affect MODE_DDS_SELF_TEST.
For correlation lock, use `seed`, `output_phase`.

---

## UART Status Output (USART2, ~200 ms)

```
[LOCK]
mode=<DISABLED|MEASURE_ONLY|CLOSED_LOOP>
state=<IDLE|ACQUIRE|TRACK|LOCKED|FAULT>
seed_freq_hz=<float>
corrected_freq_hz=<float>
freq_correction_hz=<float>
freq_word_correction=<int>
dds_ref_phase_deg=<float>
detector_mean_mv=<float>
detector_target_mv=<float>
phase_error_mv=<float>           ★ primary signed error signal
detector_rms_mv=<float>          ★ noise indicator only
detector_slope_mv_s=<float>      ★ drift indicator only
polarity=<1|-1>
locked=<0|1>
```

**phase_error_mv** is the primary control signal: `mean_mv - target_mv`.
It crosses zero at the 90° quadrature operating point and has opposite
sign on either side — providing directional information for the PI controller.

**detector_rms_mv** and **detector_slope_mv_s** are NOT used as the primary
error signal. They serve only as lock-quality and stability indicators.

---

## DDS Channel Assignment

| Channel | AD9833 | Role | Waveform | Phase |
|---------|--------|------|----------|-------|
| DDS_REF | A (PB12/13/15) | AD630 reference carrier | Sine (fixed) | PLL correction |
| DDS_OUT | B (PD12/13/15) | Reconstructed A' output | User-selectable | PLL correction + output offset |

Both channels share the same `seed_freq_hz` and `freq_correction_hz`.

---

## Amplitude Handling Boundary

The current version does NOT auto-control A' amplitude from AD630 DC output.

Reason: a single AD630 DC value is a function of A-amplitude × cos(Δφ) × gain × offset.
Without a second quadrature channel (I/Q), amplitude and phase cannot be
disambiguated from a single scalar.

The amplitude is set by the 2023H recognition pipeline or manually by the user.
AD630 only provides frequency and phase tracking.

Future: I/Q dual-AD630 configuration reserved for simultaneous amplitude + phase.

---

## Build

```bash
cmake --preset Debug -G "Ninja" -DCMAKE_MAKE_PROGRAM="<ninja-path>"
cmake --build build/Debug
# ELF: build/Debug/PLL_Lab_2026
# HEX: build/Debug/PLL_Lab_2026.hex
# BIN: build/Debug/PLL_Lab_2026.bin
```

Toolchain: GNU 14.3.1 (STM32Cube bundle)  
HAL: STM32Cube_FW_F4 v1.28.3

---

## Project Structure

```
PLL_Lab_2026/
├── Core/Inc/                   # HAL peripheral headers
├── Core/Src/                   # HAL sources + main.c
├── User/Inc/
│   ├── AD9833.h / ADS8688.h    # DDS / ADC drivers
│   ├── PLL/
│   │   ├── pid_phase.h         # AD630 window measurement (wrapped by signal_lock)
│   │   └── pll_lab_config.h    # Unified MCLK + range constants
│   └── LOCK/
│       ├── signal_lock.h       # Correlation lock public interface
│       └── signal_lock_config.h# Tunable PI / lock / timing parameters
├── User/Src/
│   ├── AD9833.c / ADS8688.c
│   ├── PLL/pid_phase.c
│   └── LOCK/signal_lock.c      # Correlation lock implementation
├── docs/
│   ├── PLL_LAB_HANDOFF.md
│   ├── 2023H_PLL模块提取分析报告.md
│   ├── PHASE2_OPEN_LOOP_TEST.md
│   └── CORRELATION_LOCK_TEST.md
├── cmake/                      # Toolchain + HAL library build
├── CMakeLists.txt
└── PLL_Lab_2026.ioc            # CubeMX config (archival, not used for build)
```

---

## Key Documents

| Document | Purpose |
|----------|---------|
| [PLL_LAB_HANDOFF.md](docs/PLL_LAB_HANDOFF.md) | Full context for continuing work |
| [PHASE2_OPEN_LOOP_TEST.md](docs/PHASE2_OPEN_LOOP_TEST.md) | DDS/ADC/UART open-loop test plan |
| [CORRELATION_LOCK_TEST.md](docs/CORRELATION_LOCK_TEST.md) | Correlation detector characterization |
