# PHASE2 Open-Loop Hardware Verification Test Plan

> **Project:** PLL_Lab_2026  
> **Target:** STM32F407ZGT6  
> **Current Status:** `L1 Build Verified / Flashable`  
> **Phase Scope:** Hardware open-loop verification ONLY — **PLL closed-loop is PROHIBITED in this phase.**

---

## ⛔ Phase Boundary

| Allowed | Prohibited |
|---------|-----------|
| Power on, flash, verify boot | `lock start` / `mode closed` |
| Measure MCO2, DDS output, ADC input | PLLPhase_UpdateWindow() |
| UART1 command shell interaction | Modifying PLL algorithm |
| `mode open`, `mode measure` | Connecting AD630 feedback loop |
| `freq`, `phase`, `status`, `reset` commands | Adjusting PLL thresholds |

---

## 1. Pre-Flight Checklist

Before applying power:

- [ ] ST-Link connected to SWD (SWCLK/SWDIO/GND)
- [ ] USB-TTL adapter #1 connected to PA9 (TX) / PA10 (RX) — USART1 command shell
- [ ] USB-TTL adapter #2 connected to PA2 (TX) / PA3 (RX) — USART2 printf output
- [ ] Oscilloscope probe on PC9 (MCO2)
- [ ] Oscilloscope probe on AD9833-A output (参考通道)
- [ ] Oscilloscope probe on AD9833-B output (被控通道)
- [ ] ADS8688 CH0 / CH1 connected to known DC voltage source or GND
- [ ] Power supply: 3.3 V stable, current limit ~200 mA

---

## 2. PC9 MCO2 Measurement

### Setup
1. Power on board
2. Probe PC9 with oscilloscope (×10 probe, AC coupling OK for frequency; DC for amplitude)
3. Expected: HSE / 2 = 8 MHz / 2 = 4 MHz

### Measurement

| Parameter | Expected | Measured | Pass/Fail |
|-----------|----------|----------|-----------|
| Frequency | 4,000,000 Hz | ___________ | ☐ |
| Amplitude (Vpp) | ~3.3 V | ___________ | ☐ |
| Waveform | Square | ___________ | ☐ |
| Jitter (pk-pk) | < 1 ns | ___________ | ☐ |

### Action if FAIL
If MCO2 ≠ 4 MHz:
1. Measure HSE (crystal pins) → confirm 8 MHz
2. Check `set_clock()` in [AD9833.c](../User/Src/AD9833.c): `RCC_MCO2SOURCE_HSE, RCC_MCODIV_2`
3. If different, update `PLL_LAB_AD9833_MCLK_HZ` in [pll_lab_config.h](../User/Inc/PLL/pll_lab_config.h)
4. Rebuild and re-flash

---

## 3. USART Communication Verification

### 3.1 USART2 — printf Status Output (PA2/PA3, 115200 8N1)

| Step | Action | Expected | Pass/Fail |
|------|--------|----------|-----------|
| 1 | Open terminal (115200 8N1) on PA2 | — | ☐ |
| 2 | Reset board | Boot banner with `PLL_Lab_2026 v1.0` | ☐ |
| 3 | Wait 2 seconds | Periodic `[PLL]` status block every ~200 ms | ☐ |
| 4 | Verify fields present | `mode=`, `ref_freq_hz=`, `controlled_freq_hz=`, `ad630_ch0_mv=`, `state=`, `locked=` | ☐ |
| 5 | Verify no garbage | Clean ASCII, no escape sequences | ☐ |

### 3.2 USART1 — Command Shell (PA9/PA10, 115200 8N1)

| Step | Command | Expected Response | Pass/Fail |
|------|---------|-------------------|-----------|
| 1 | `status` | Prints `mode=` and frequencies | ☐ |
| 2 | `mode open` | `[CMD] Mode = OPEN_LOOP` | ☐ |
| 3 | `mode measure` | `[CMD] Mode = MEASURE_ONLY` | ☐ |
| 4 | `mode closed` | `[CMD] Mode = CLOSED_LOOP` | ☐ |
| 5 | `mode open` | Return to open-loop | ☐ |
| 6 | `freq ref 5000` | `[CMD] Ref freq = 5000.00 Hz` | ☐ |
| 7 | `freq ctrl 4999` | `[CMD] Ctrl freq = 4999.00 Hz` | ☐ |
| 8 | `freq ref 10000` | Restore 10 kHz ref | ☐ |
| 9 | `freq ctrl 9999` | Restore 9999 Hz ctrl | ☐ |
| 10 | `phase ref 90` | `[CMD] Ref phase = 90.0 deg` | ☐ |
| 11 | `phase ctrl 180` | `[CMD] Ctrl phase = 180.0 deg` | ☐ |
| 12 | `phase ref 0` | Restore 0° ref | ☐ |
| 13 | `phase ctrl 0` | Restore 0° ctrl | ☐ |
| 14 | `status` | Verify ref=10000 ctrl=9999 phase=0 | ☐ |
| 15 | `reset` | DDS re-inits, output resumes | ☐ |
| 16 | `garbage_cmd` | `[CMD] Unknown: 'garbage_cmd'. Commands: mode, freq, phase, lock, status, reset` | ☐ |
| 17 | Send 100-char line | No crash, no garbled response | ☐ |
| 18 | Press Enter with empty line | No output (silent ignore) | ☐ |

---

## 4. AD9833 DDS Frequency Verification

> **Prerequisite:** MCO2 confirmed at 4 MHz (§2).  
> **Mode:** `MODE_OPEN_LOOP` (`mode open` from USART1).  
> **Waveform:** Sine (default). Both channels.

### 4.1 AD9833-A (参考通道, PB12/PB13/PB15)

| UART Command | Nominal Hz | Measured Hz | Error (Hz) | Error (ppm) | Pass/Fail |
|-------------|------------|-------------|------------|-------------|-----------|
| `freq ref 1000` | 1,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ref 5000` | 5,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ref 10000` | 10,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ref 50000` | 50,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ref 100000` | 100,000.00 | ___________ | ___________ | ___________ | ☐ |

**Pass criteria:** Error < 0.1% (1000 ppm) at all frequencies. If systematic offset exists (e.g., all frequencies ×0.4 or ×2.5), MCLK is wrong — update `PLL_LAB_AD9833_MCLK_HZ`.

### 4.2 AD9833-B (被控通道, PD12/PD13/PD15)

| UART Command | Nominal Hz | Measured Hz | Error (Hz) | Error (ppm) | Pass/Fail |
|-------------|------------|-------------|------------|-------------|-----------|
| `freq ctrl 1000` | 1,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ctrl 5000` | 5,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ctrl 9999` | 9,999.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ctrl 10000` | 10,000.00 | ___________ | ___________ | ___________ | ☐ |
| `freq ctrl 50000` | 50,000.00 | ___________ | ___________ | ___________ | ☐ |

### 4.3 Two-Channel Simultaneous Output

| UART Command | Expected | Measured | Pass/Fail |
|-------------|----------|----------|-----------|
| `freq ref 10000` + `freq ctrl 10000` | Both 10 kHz, in sync | ___________ | ☐ |
| `freq ref 10000` + `freq ctrl 9999` | 1 Hz beat visible | ___________ | ☐ |

---

## 5. AD9833 Phase Verification

> **Prerequisite:** Both channels set to 10,000 Hz sine.  
> **Mode:** `MODE_OPEN_LOOP`.  
> **Measurement:** Dual-channel oscilloscope, ch1 on AD9833-A, ch2 on AD9833-B.  
> **Pass criteria:** Phase error < ±2° (for 10 kHz, 2° ≈ 555 ns time offset).

### 5.1 Phase Test Table

| UART Commands | Expected Δφ | Measured Δt | Measured Δφ | Error (°) | Pass/Fail |
|---------------|-------------|-------------|-------------|-----------|-----------|
| `phase ref 0` + `phase ctrl 0` | 0° | ___________ | ___________ | ___________ | ☐ |
| `phase ref 0` + `phase ctrl 90` | -90° (ctrl lags ref) | ___________ | ___________ | ___________ | ☐ |
| `phase ref 0` + `phase ctrl 180` | -180° | ___________ | ___________ | ___________ | ☐ |
| `phase ref 0` + `phase ctrl 270` | -270° (+90°) | ___________ | ___________ | ___________ | ☐ |
| `phase ref 90` + `phase ctrl 90` | 0° | ___________ | ___________ | ___________ | ☐ |
| `phase ref 180` + `phase ctrl 0` | +180° (ref lags ctrl) | ___________ | ___________ | ___________ | ☐ |

**Δφ formula:** `Δφ(°) = Δt(s) × f(Hz) × 360`

---

## 6. Phase Repeatability After Reset

> **Procedure:** Set both channels to 10 kHz, 0° phase. Observe phase difference on scope. Issue `reset` command. Wait for DDS re-init. Measure phase difference again. Repeat 5 times.

| Reset # | Δt (ns) | Δφ (°) | Drift from mean (°) | Pass/Fail |
|---------|---------|--------|---------------------|-----------|
| 1 (initial) | ___________ | ___________ | ___________ | ☐ |
| 2 | ___________ | ___________ | ___________ | ☐ |
| 3 | ___________ | ___________ | ___________ | ☐ |
| 4 | ___________ | ___________ | ___________ | ☐ |
| 5 | ___________ | ___________ | ___________ | ☐ |

**Mean Δφ:** ___________  
**Max deviation from mean:** ___________  

**Pass criteria:** Max deviation < ±2°.

---

## 7. ADS8688 DC Voltage Measurement

> **Prerequisite:** ADS8688 CH0 and CH1 connected to adjustable DC voltage source.  
> **Mode:** `MODE_MEASURE_ONLY` (`mode measure` from USART1).  
> **Observe:** `ad630_ch0_mv=` and `ad630_ch1_mv=` in periodic status output.  
> **Range:** ±10.24 V (`PLL_LAB_ADS8688_RANGE_MV = 10240.0f`).

### 7.1 CH0

| Input Voltage | Expected Code | Expected mV | Measured mV | Error (mV) | Pass/Fail |
|---------------|---------------|-------------|-------------|------------|-----------|
| GND (0 V) | ~32768 | 0 ± 50 | ___________ | ___________ | ☐ |
| +1.000 V | ~35966 | 1000 ± 50 | ___________ | ___________ | ☐ |
| -1.000 V | ~29570 | -1000 ± 50 | ___________ | ___________ | ☐ |
| +5.000 V | ~48767 | 5000 ± 100 | ___________ | ___________ | ☐ |
| -5.000 V | ~16769 | -5000 ± 100 | ___________ | ___________ | ☐ |

### 7.2 CH1

| Input Voltage | Expected Code | Expected mV | Measured mV | Error (mV) | Pass/Fail |
|---------------|---------------|-------------|-------------|------------|-----------|
| GND (0 V) | ~32768 | 0 ± 50 | ___________ | ___________ | ☐ |
| +1.000 V | ~35966 | 1000 ± 50 | ___________ | ___________ | ☐ |
| -1.000 V | ~29570 | -1000 ± 50 | ___________ | ___________ | ☐ |
| +5.000 V | ~48767 | 5000 ± 100 | ___________ | ___________ | ☐ |
| -5.000 V | ~16769 | -5000 ± 100 | ___________ | ___________ | ☐ |

**Pass criteria:** Error < 1% of reading + 20 mV offset at 0 V.

### 7.3 ADS8688 Status Check

| Check | Pass/Fail |
|-------|-----------|
| `ads8688_fault == ADS8688_OK` at boot | ☐ |
| No `[FATAL] ADS8688 init failed` in USART2 output | ☐ |
| `ad630_ch0_mv` updates every ~200 ms | ☐ |
| `ad630_ch1_mv` updates every ~200 ms | ☐ |

---

## 8. System-Level Checks

| Check | Procedure | Pass/Fail |
|-------|-----------|-----------|
| No HardFault | Run for 10 minutes, check LED0/LED1 solid on | ☐ |
| TIM7 alive | `[PLL]` blocks print every ~200 ms (count 30 blocks ≈ 6 s) | ☐ |
| No malloc/sbrk dependency | `syscalls.c` only provides stubs | ☐ |
| Stack not overflowed | Observe no crash after `reset` × 10 loops | ☐ |

---

## 9. Overall Pass/Fail Summary

| Subsystem | Pass/Fail | Notes |
|-----------|-----------|-------|
| MCO2 Clock | ☐ | §2 |
| USART2 printf | ☐ | §3.1 |
| USART1 Commands | ☐ | §3.2 |
| AD9833-A Frequency | ☐ | §4.1 |
| AD9833-B Frequency | ☐ | §4.2 |
| AD9833 Phase | ☐ | §5 |
| Phase Repeatability | ☐ | §6 |
| ADS8688 CH0 | ☐ | §7.1 |
| ADS8688 CH1 | ☐ | §7.2 |
| System Stability | ☐ | §8 |

**All subsystems must PASS before advancing to Phase 3 (PLL closed-loop).**

---

## 10. Phase 2 Sign-Off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Tester | ___________ | ___________ | ___________ |
| Reviewer | ___________ | ___________ | ___________ |

**Next phase:** Phase 3 — Closed-Loop PLL Verification (`PHASE3_CLOSED_LOOP_TEST.md`).  
**Prerequisite:** All Phase 2 items must be marked PASS.

---

*Do NOT start Phase 3 until this document is fully signed off.*
