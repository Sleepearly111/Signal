# Correlation Lock Hardware Verification Test Plan

> **Project:** PLL_Lab_2026 v2.0  
> **Target:** STM32F407ZGT6  
> **Status:** `L1 Build Verified / Flashable`  
> **Phase:** Correlation-lock open-loop hardware characterization  
> **⚠️  This phase is MEASUREMENT ONLY.  Do NOT close the PLL loop.**

---

## 0. Pre-Flight

- [ ] ST-Link connected
- [ ] USART1 (PA9/PA10) USB-TTL → terminal 115200 8N1
- [ ] USART2 (PA2/PA3) USB-TTL → terminal 115200 8N1
- [ ] Signal generator → AD630 SIG IN (set to pure A waveform)
- [ ] DDS_REF (AD9833-A) → AD630 REF IN
- [ ] AD630 VOUT → LPF → ADS8688 CH0
- [ ] Oscilloscope: Ch1 = AD630 VOUT (after LPF), Ch2 = DDS_REF
- [ ] Power supply ±12V for AD630, 3.3V for MCU

---

## Test 1 — Pure A Input, Manual Phase Scan

**Setup:**
- Signal generator: sine, 50 kHz, ~1 Vpp → AD630 SIG IN
- `seed 50000`
- `mode measure`

**Procedure:** For each DDS_REF phase, record AD630 DC output.

| `dds_ref_phase` | Expected AD630 DC | Measured CH0 (mV) | Notes |
|-----------------|-------------------|--------------------|-------|
| 0 | ~+max (in-phase) | ___________ | |
| 45 | ~+0.7×max | ___________ | |
| 90 | ~0 (quadrature null) | ___________ | ★ target |
| 135 | ~-0.7×max | ___________ | |
| 180 | ~-max | ___________ | |
| 225 | ~-0.7×max | ___________ | |
| 270 | ~0 (quadrature null #2) | ___________ | |
| 315 | ~+0.7×max | ___________ | |

**Pass criteria:**
- Clear sinusoidal relationship between DDS_REF phase and AD630 DC
- Two zero-crossings at ~90° and ~270°
- Polarity consistent (positive slope through 90° zero-crossing, or documented inversion)

**Determine polarity:**
At the 90° null, increase DDS_REF phase slightly (e.g. 90° → 95°):
- If CH0 voltage increases → polarity = +1 (normal)
- If CH0 voltage decreases → polarity = -1 (inverted)
- Record: polarity = ___________ (use `polarity normal` or `polarity invert`)

---

## Test 2 — A + B Input

**Setup:**
- Signal generator Ch1: A = 50 kHz, 1 Vpp sine → summed with B
- Signal generator Ch2: B = 53 kHz, 0.5 Vpp sine → summed with A
- Combined C = A + B → AD630 SIG IN
- `seed 50000`
- `mode measure`

| `dds_ref_phase` | CH0 mean (mV) | CH0 RMS (mV) | Notes |
|-----------------|---------------|--------------|-------|
| 0 | ___________ | ___________ | |
| 90 | ___________ | ___________ | ★ target null |
| 180 | ___________ | ___________ | |
| 270 | ___________ | ___________ | |

**Compare to Test 1 at same phases:**

| Phase | Pure A CH0 (mV) | A+B CH0 (mV) | Δ (mV) |
|-------|-----------------|---------------|--------|
| 0 | ___________ | ___________ | ___________ |
| 90 | ___________ | ___________ | ___________ |
| 180 | ___________ | ___________ | ___________ |

**Pass criteria:**
- CH0 DC at 90° null remains close to the pure-A value (within ±20% or ±50 mV)
- RMS noise increases but DC mean is dominated by A component correlation
- B does not pull the null point more than 50 mV from pure-A

---

## Test 3 — Frequency Small Offset (MEASURE ONLY, NO LOCK)

**Setup:**
- Signal generator: A = 50,000 Hz, 1 Vpp → AD630 SIG IN
- `seed 50000` (exact)
- `mode measure`

| `seed` setting | DDS_REF actual | A actual | Δf (Hz) | CH0 behavior |
|---------------|----------------|----------|---------|--------------|
| 50000 | 50,000 Hz | 50,000 Hz | 0 | ___________ |
| 49990 | 49,990 Hz | 50,000 Hz | -10 | ___________ |
| 50010 | 50,010 Hz | 50,000 Hz | +10 | ___________ |
| 49900 | 49,900 Hz | 50,000 Hz | -100 | ___________ |
| 50100 | 50,100 Hz | 50,000 Hz | +100 | ___________ |

**Pass criteria:**
- At Δf = 0, AD630 DC is stable (drift < 10 mV/s after settling)
- At Δf ≠ 0, AD630 DC oscillates at Δf rate (beat frequency visible on scope)
- Beat amplitude decreases with larger Δf (LPF attenuation)

---

## Test 4 — 100 kHz Target

**Setup:**
- Signal generator: A = 100,000 Hz, 1 Vpp → AD630 SIG IN
- `seed 100000`
- `mode measure`

| `dds_ref_phase` | CH0 mean (mV) | CH0 RMS (mV) | Notes |
|-----------------|---------------|--------------|-------|
| 0 | ___________ | ___________ | |
| 90 | ___________ | ___________ | ★ null |
| 180 | ___________ | ___________ | |
| 270 | ___________ | ___________ | |

**Compare 50 kHz vs 100 kHz at 90° null:**

| Frequency | CH0 mean (mV) | CH0 RMS (mV) | AD630 gain roll-off? |
|-----------|---------------|--------------|----------------------|
| 50 kHz | ___________ | ___________ | |
| 100 kHz | ___________ | ___________ | |

**Pass criteria:**
- DC response at 90° still shows clear null
- RMS noise acceptable (< 100 mV)
- AD630 output amplitude at 0°/180° may be reduced vs 50 kHz (gain-bandwidth)

---

## Test 5 — A' Output Verification (Open-Loop)

**Setup:**
- Signal generator: A = 50 kHz, 1 Vpp sine
- `seed 50000`
- `wave sine` (or `wave triangle` if A is triangular)
- `output_phase 0`
- `mode measure`

**Oscilloscope:** Ch1 = true A (signal gen), Ch2 = DDS_OUT (AD9833-B)

| Check | Expected | Measured | Pass/Fail |
|-------|----------|----------|-----------|
| DDS_OUT frequency | 50,000 Hz | ___________ | ☐ |
| DDS_OUT waveform | Matches `wave` command | ___________ | ☐ |
| Phase relationship | Match within few deg (open-loop, no PLL correction) | ___________ | ☐ |

**Note:** Amplitude is NOT auto-controlled by this phase. Set via signal generator or future amplitude module.

---

## Test 6 — Calibration

**Procedure:**
1. A = 50 kHz pure sine → AD630 SIG IN
2. `seed 50000`
3. `mode measure`
4. Manually scan DDS_REF phase to find the cleanest 90° null (CH0 closest to 0 mV)
5. `cal detector_zero`
6. Verify: `status` shows `detector_target_mv` ≈ measured null voltage

| Step | Result |
|------|--------|
| Phase at null | ___________ deg |
| CH0 voltage at null before cal | ___________ mV |
| `detector_target_mv` after cal | ___________ mV |
| `phase_error_mv` after cal at null | ___________ mV (should be near 0) |

---

## 7. Overall Pass/Fail Summary

| Test | Description | Pass/Fail | Notes |
|------|-------------|-----------|-------|
| 1 | Pure A phase scan | ☐ | |
| 2 | A+B correlation | ☐ | |
| 3 | Frequency offset beat | ☐ | |
| 4 | 100 kHz target | ☐ | |
| 5 | A' output (open-loop) | ☐ | |
| 6 | Zero calibration | ☐ | |

**All tests must PASS before advancing to Test 3 (Frequency Small Offset with PLL closed).**

**⚠️  PLL closed-loop (`mode lock` + `lock start`) is PROHIBITED until all 6 tests pass and sign-off below is complete.**

---

## Sign-Off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Tester | ___________ | ___________ | ___________ |
| Reviewer | ___________ | ___________ | ___________ |
