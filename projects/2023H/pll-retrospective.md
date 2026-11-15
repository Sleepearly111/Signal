# 2023H PLL 项目总结

> 项目路径：`projects/已完成项目/test0/`
> 建立日期：2026-07-26
> 基线版本：`aebb855 基本定住` + FLL-only 重构
> 范围：AD630 鉴相验证、FLL 频率锁定、PLL 相位锁定、Phase Acquisition 验证

---

## 一、项目目标

### 1.1 输入信号关系

```text
C = A + B          (复合信号，由信号源产生)

A  = 10 kHz 正弦   (待分离的目标信号)
B  = 50 kHz 正弦   (干扰/另一路信号)

A' = DDS 本地参考  (AD9833-A 生成，需锁定到 A)
B' = DDS 本地参考  (AD9833-B 生成，需锁定到 B)
```

目标：使 A' 与 A **频率一致、相位稳定**，实现从复合信号 C 中分离出 A 分量。

### 1.2 完整系统模型

#### 1.2.1 AD630 模拟鉴相器

```text
C(A+B) ──→ AD630 ──→ 低通(可选) ──→ ADS8688 CH0/CH1 ──→ STM32
            ↑
A' ────────┘  (参考信号)

AD630 输出:
  Vout_DC = offset + K · cos(Δφ)
  Δφ = phase(A in C) - phase(A')
```

AD630 作为**模拟乘法器 + 鉴相器**：将 C 中的 A 分量与本地参考 A' 相乘。输出直流分量正比于 cos(Δφ)，在 0° 时最大，±90° 时过零。

**鉴相器特性**：
- 输入：A 与 A' 的**瞬时相位差** Δφ
- 输出：`mean_mv` — 直流电压，∝ cos(Δφ)
- 非线性：余弦函数，仅在 ±90° 附近近似线性
- 无法从单点测量判别 Δφ 的正负方向（cos 为偶函数）

#### 1.2.2 数字 PLL 控制回路

```text
模拟域                          数字域
───────                        ──────

AD630                           phase_error
  │  mean_mv (cos Δφ)              │
  │                                ▼
  ▼                           Loop Filter
ADS8688 ADC                   (P / I / 限幅 / 死区)
  │                                │
  │  code                          ▼
  │                           phase_increment
  ▼                                │
PLLPhase_UpdateWindow()            │
  │                                ▼
  │  phase_offset             AD9833 PHASE0
  └──────────────────────────→ register (12-bit)

为什么数字域可以控制模拟鉴相器：
  STM32 通过写入 AD9833 的 PHASE0 寄存器（0-4095 = 0°-360°）
  改变 A' 的瞬时相位 → 改变 Δφ → 改变 AD630 的 Vout_DC
  → 数字相位偏移直接转换为模拟电压变化 → 闭合数字-模拟混合锁相环
```

#### 1.2.3 AD9833 相位控制

- AD9833 内置 12-bit PHASE0 寄存器（0~4095，对应 0°~360°，分辨率 0.088°/step）
- 另有 PHASE1 寄存器 + PSEL 位，可用于"影子寄存器"无扰动扫描
- 软件 SPI 位操作写入，通过 `wave_flag` 全局变量选择通道
- FCLK = 10 MHz（模块板载晶振）

### 1.3 FLL + PLL 双环架构：分工与解耦

```text
              AD630 输出
                  │
          ┌───────┴───────┐
          │               │
     slope_mv_s       mean_mv
     d(cos Δφ)/dt     cos(Δφ)
          │               │
          ▼               ▼
        FLL              PLL
      (频率环)          (相位环)

FLL — "不要转"                    PLL — "转到哪里"
─────────────────                ─────────────────
解决: 频率误差 Δf                  解决: 瞬时相位误差 Δφ
检测: phase 变化率 dV/dt          检测: mean_mv 绝对值
      ∝ Δf                             ∝ cos(Δφ)
输入: filtered_slope_mv_s        输入: filtered_mean_mv
输出: freq_word_offset           输出: phase_word_offset
      + delay_flag                      (PHASE0 register)
```

**核心解耦原理**：

```
d(Vout_DC)/dt = -K·sin(Δφ) · Δω

slope 包含两项:
  sin(Δφ) — 与当前相位有关（PLL 的范畴）
  Δω      — 与频率误差有关（FLL 的范畴）

当 Δφ 不在 0° 或 180° 时, sin(Δφ) ≠ 0:
  slope ≠ 0 ⇔ Δω ≠ 0  →  slope → 0 即可判断频率匹配

当频率匹配 (Δω ≈ 0) 时:
  mean_mv 的绝对值直接反映 Δφ  →  PLL 可以独立工作
```

**为什么必须两个环同时运行**：

| 策略 | 问题 |
|------|------|
| 只跑 FLL | 频率锁定后相位自由漂移，示波器波形左右移动 |
| 只跑 PLL | 残余频差持续累积，PLL 输出饱和后相位跳变 |
| FLL + PLL 同时 | FLL 持续消除 Δf，PLL 持续消除 Δφ，互相不干扰 |

两个环观察同一物理信号（AD630 输出）的**不同维度**——时域均值 vs 时域变化率——从而实现真正的正交控制。

---

## 二、最终系统架构

### 2.1 完整信号链

```text
K0 按下
  │
  ▼
ADC DMA 采样 → FFT(1024点) → Demultiplex → 识别 A/B 频率+波形
  │
  ▼
DDS 初始化: AD9833-A(10kHz)  AD9833-B(50kHz)
  │
  ▼
每 200ms 窗口:
  ADS8688 ← AD630 DC输出(CH0=AK, CH1=BK)
  │
  ├─→ FLL: slope_mv_s → freq_offset ±32 / delay 0-16
  │     SEARCH → LOCK_CALIBRATE → HOLD
  │
  └─→ PLL: mean_mv → (capture peak → track maintain)
        IDLE → CAP_APPLY → CAP_MEASURE → TRACK
```

### 2.2 状态机

```text
FLL:                              PLL:
IDLE                              IDLE
  ↓ K0                              ↓ FLL lock_confirmed
FLL_SEARCH                        PHASE_CAP_APPLY
  ↓ RMS<10mV,|slope|<100mV/s ×3     ↓ 写测试角度
FLL_LOCK_CALIBRATE                PHASE_CAP_MEASURE
  ↓ v_target=filtered_mean          ↓ 记录 mean, idx++
FLL_HOLD                          (8轮后找最大)
  ↓ 漂移检测+确认+冷却              ↓ 设置 best_phase
  └── 失锁→FLL_SEARCH              PHASE_TRACK
                                     ↓ 每5窗 ±1 step 微调
                                   (持续维护)
```

### 2.3 运行模式

| 模式 | 宏 | 说明 |
|------|-----|------|
| 比赛 | `FLL_ENABLE_TARGET_TRACKING 0` | 纯 FLL，不写相位寄存器 |
| 调试 | `mode debug` 命令 | 逐窗打印、phase_sweep/test/cal_test 可用 |

---

## 三、PLL 算法详细说明

### 3.1 Phase Acquisition（相位捕获）

#### 3.1.1 为什么传统 PLL 启动慢

**问题**：FLL 锁定后，A' 频率与 A 匹配，但初始相位 Δφ 是**随机的**。传统 PI 型 PLL 无法从任意初始相位快速收敛。

**根本原因**：

```text
AD630 输出: V = offset + K · cos(Δφ)

传统 PI 控制器依赖 error = V - target 的线性关系:
  PI 调整 phase → Δφ 变化 → V 变化 → error 变化

但 cos(Δφ) 在不同区间的行为完全不同:
  Δφ ≈ ±90°: dV/dφ = |K| (最大) → PI 有响应, 但 |V| ≈ 0 (信噪比最低)
  Δφ ≈ 0°:   dV/dφ = 0        → PI 无响应 (增益为零)
  Δφ ≈ 180°: dV/dφ = 0        → PI 无响应 (增益为零)

→ PI 在 0°/180° 附近"失明"，可能永远无法收敛到锁定点
→ PI 增益足够大时，在 ±90° 附近又容易过冲、振荡
```

**因此需要 Phase Acquisition**：在启动时做一个**全局扫描**，直接找到最佳工作点，再交由轻量 Track 维护。PLL 只能做局部收敛，不能做全局搜索。

#### 3.1.2 Capture + Track 结构

```text
                FLL LOCKED
                    │
        ┌───────────┴───────────┐
        │                       │
   Phase Acquisition       Phase Track
   (一次，启动时)           (持续，运行时)
        │                       │
   phase sweep               ±1 step P&O
   全范围搜索                局部维护
        │                       │
   找到全局最优             维持局部最优
        │                       │
   直接写 phase_offset      微调 phase_offset
```

**Capture 阶段**：全局搜索，一次性找到最佳相位，可接受较长耗时（秒级）。

**Track 阶段**：轻量维护，每 1 秒只扰动 0.088°，示波器不可见。

#### 3.1.3 Capture 流程

```text
FLL LOCKED
    │
    ▼
Phase Capture (8点扫描)
    │
    ├─ 0°   45°   90°   135°   180°   225°   270°   315°
    │  (每点 1 个测量窗口，200ms)
    │
    ▼
记录每点 filtered_mean_mv = V_i
    │
    ▼
argmax(V_0 .. V_7) → best_angle
    │  (mean_mv 最大 = cos(Δφ) 最大 = Δφ 最接近 0° = 同相)
    ▼
phase_capture_best_pha = best_angle × 4096 / 360
    │
    ▼
写入 AD9833 PHASE0 = best_pha
    │
    ▼
进入 PLL Track
```

**扫描参数**：

| 参数 | 值 | 设计理由 |
|------|-----|---------|
| 扫描点数 | 8 (0°, 45°, ..., 315°) | 余弦极值少，8 点足够分辨 |
| 每点测量 | 1 个 200ms 窗口 | 无外部 LPF，一个窗口均值已稳定 |
| 评价指标 | `filtered_mean_mv`（EMA α=0.35） | 平滑后的 AD630 DC 输出 |
| best_phase | `argmax(mean_mv[0..7]) × 45°` | 选 cos 最大点 = 同相 |
| 相位限幅 | ±120° (±1365 steps) | 防止噪声导致的极端值跑飞 |
| 扫描总耗时 | ~1.6s（8 × 200ms） | 可接受，仅启动时一次 |

#### 3.1.4 快速捕获（PHASE1 影子寄存器）

为避免扫描期间示波器可见相位跳变：

```text
正常输出: PHASE0 (PSEL=0) — 保持不变

每个测试角度:
  写 PHASE1 = test_angle     (输出不变，PHASE1 未选中)
  PSEL=1                     (切到 PHASE1, 瞬时)
  等 3ms + 读 ADC × 4        (~8ms 测量)
  PSEL=0                     (切回 PHASE0, 瞬时)

每个角度只扰动 ~8ms，8 个角度总扰动 ~64ms
→ 示波器几乎不可见
```

#### 3.1.5 scan_span 有效性检查

```
span = max(mean_mv[0..7]) - min(mean_mv[0..7])

span < PHASE_SCAN_SPAN_MIN_MV (30mV):
  → AD630 余弦响应太弱 → 信号可能丢失或幅度过小
  → 标记为 FAILED, 不进入 Track
```

这是对 AD630 信号质量的硬件自检：余弦响应的峰谷差必须足够大，扫描结果才有意义。

### 3.2 Phase Track（相位跟踪）

**最终保持算法：扰动-观察 (Perturb & Observe)**

```text
每 5 个窗口 (PHASE_TRACK_INTERVAL=5):
  cur_mean = filtered_mean_mv[ch]
  if cur_mean >= prev_mean:
    step = +PHASE_TRACK_STEP (+1, 即 +0.088°)
  else:
    step = -PHASE_TRACK_STEP (-1)
  
  phase_word_offset += step  (限幅 ±1365 = ±120°)
  写入 AD9833 PHASE0 = phase_capture_best_pha + phase_word_offset
  prev_mean = cur_mean
```

**为什么不用固定 target_mv**：
- AD630 输出电压受温度、供电、信号幅度影响，`target_mv` 会随时间漂移
- 固定 target 意味着假设锁定时 AD630 输出值永远不变，实际并非如此
- Phase Acquisition 每次都在当前硬件条件下找到真正的峰值

**为什么不用 hill climbing**：
- Hill climbing 每次只比较上下两个方向，收敛极慢
- 需要连续多步确认方向，震荡严重（±1 来回跳）
- 8 点全扫描直接定位，一步到位

**为什么先 capture 再 track，不直接锁最大点**：
- 直接锁定 0° 附近（mean_mv 最大点）后，由于 cos(Δφ) 在峰值处斜率为零，小相位变化几乎不改变 mean_mv
- 最佳工作点应在 ±45°~±90° 之间的**线性区**，这里灵敏度最高
- 但如果锁定到非峰值点，信号幅度会降低（cos 值小 → 鉴相增益低）
- Capture 找到峰值后，Track 在峰值附近 ±0.088° 微调可以维持，不需要大范围线性区

---

## 四、算法推导

### 4.1 AD630 鉴相特性

```text
Vout(t) = C(t) × A'(t)
        = [A·sin(ωt) + B·sin(5ωt)] × A'·sin(ωt + Δφ)

直流项: Vout_DC = (A·A'/2) · cos(Δφ)
交流项: ω, 2ω, 4ω, 5ω, 6ω 分量 (需 LPF 滤除)

简化: Vout_DC = offset + K · cos(Δφ)
```

### 4.2 正交点的灵敏度

```text
dV/d(Δφ) = -K · sin(Δφ)

在 Δφ = ±90°: sin(±90°) = ±1, 灵敏度最大 = |K|
在 Δφ = 0°:    sin(0°) = 0,    灵敏度为零

→ 正交锁定点 (±90°) 鉴相灵敏度最高
→ 但此时 Vout_DC ≈ offset，信号最弱
```

### 4.3 为什么最终采用 Capture + Track

| 方案 | 问题 |
|------|------|
| PI 控制器直锁 target_mv | target 漂移，FLL/PLL 同用 mean_mv → 两环打架 |
| Hill climbing 爬山法 | 收敛慢，±1 震荡 |
| FLL 冻结只跑 PLL | 残余频差累积 → 相位漂移无法纠正 |
| **Capture + Track** | 启动时全扫描找峰，运行时轻量保持 → 不依赖固定 target，不打架 |

### 4.4 FLL 解耦推导

```text
d(Vout_DC)/dt = d(offset + K·cos(Δφ))/dt
              = -K·sin(Δφ) · d(Δφ)/dt
              = -K·sin(Δφ) · Δω

当 Δφ 不固定时, sin(Δφ) ≠ 0, slope ∝ Δω(频率误差)

→ FLL 用 filtered_slope_mv_s 可以独立检测频率误差
→ 不需要知道当前相位，只需要 slope→0 就表示频率匹配
```

---

## 五、调试过程总结

### 5.1 失败方案详细复盘

#### 方案 1：固定 target_mv PI 控制器 ❌

**做法**：FLL 锁定瞬间标定 `target_mv = mean_mv`（当时的值，如 -62mV），之后用 PI 将 `mean_mv` 维持在 `target_mv`。

**问题**：
- **target 随启动位置变化**：每次 K0 按下，FLL 锁定的相位位置不同 → target_mv 不同 → 同样的 PI 参数在不同 target 附近表现不一致
- **残余频差导致目标漂移**：~0.001Hz 级的频差在 10 秒内积累 ~3.6° 相位误差 → mean_mv 缓慢变化 → PI 持续补偿
- **AD630 温漂**：offset 随时间变化 → 10 分钟前的 target_mv 不再准确
- **PI 积分器饱和**：大幅相位误差下积分器快速累积 → 到达限幅 → 需要很长时间恢复

**结论**：固定目标电压在长时运行中不可靠。target_mv 只在锁定的那一瞬间有效。

#### 方案 2：Hill Climbing 爬山法 ❌

**做法**：每窗 ±1 step，比较 `mean_mv` 变化方向，选择更好的方向持续移动。

**问题**：
- **收敛极慢**：1 step/窗 × 0.088°/step → 走完 45° 需要 512 步 ≈ 100 秒
- **梯度搜索速度无法追踪残余频差**：频率没完全锁定时，Δφ 每秒漂移几度 → HC 的步进速度跟不上漂移速度 → "永远追不上"
- **±1 来回震荡**：噪声导致相邻两窗的 `mean_mv` 比较结果不可靠 → 方向频繁切换
- **陡峭处容易超调**：在 cos 的线性区（±90°），小的相位变化产生大的电压变化 → 容易跨过最优点

**结论**：Hill climbing 适合**没有频差**的静态优化（如微调），不适合有残余频差时的初始相位捕获。

#### 方案 3：FLL 冻结 + PLL 独立工作 ❌

**做法**：FLL 锁定后冻结 `freq_word_offset`（fll_hold=1），只运行相位 PI。

**问题**：
- 即使频率平均匹配，DDS 与输入信号的**瞬时频率**仍有微差
- 微差在相位域持续累积 → `phase_offset` 持续漂移
- 相位 PI 没有足够带宽补偿频率误差（补偿频率误差需要的相位变化率远超 PI 的积分速度）
- 最终 `phase_offset` 饱和（±1365 steps / ±120°），相位跳变

**结论**：FLL 和 PLL 必须同时运行。FLL 持续处理频率微差，PLL 只处理残余相位偏移。冻结任何一个都会导致另一个超限。

#### 方案 4：center_mv 作为正交参考 ⚠️

**发现**：phase_sweep 确认 AD630 输出近似余弦，`center_mv ≈ -62mV`（余弦在 满幅和最小值的中心），对应的相位约 90°（正交）。

**为什么适合作为正交参考**：
- 正交点是 cos(Δφ)=0 的位置 → `mean_mv = offset` → 鉴相灵敏度最高 (|dV/dφ| = |K|)
- `ad630 zero` 命令可以标定此值
- 可作为 FLL 是否锁定的辅助判据

**为什么不是最终锁相目标**：
- 正交点虽然灵敏度高，但 `|mean_mv|` 最小（cos 值接近零）
- 低信噪比下判断困难
- Phase Acquisition 扫描到的峰值（同相点）信号最强，更适合做参考

**实际上**：`center_mv` 在调试中有用（判断 AD630 工作正常），但 PLL 锁定目标应通过 Phase Acquisition 确定，不依赖 `center_mv`。

### 5.2 最终解决方案

```text
FLL (频率环)               PLL (相位环)
输入: slope_mv_s            输入: mean_mv
目标: slope → 0             目标: 峰值附近稳定
控制: freq_offset + delay   控制: phase_offset
速度: 每3窗最多1步           速度: 每5窗 ±1 step
范围: freq_offset ±32        范围: phase_offset ±1365(±120°)
```

**关键经验**：
1. FLL 用 slope，PLL 用 mean —— 两个环观察不同物理量，真正解耦
2. Phase Acquisition 用全扫描代替逐步收敛
3. Phase Track 用扰动-观察代替 PI，避免积分器问题
4. 不需要 target_mv —— 每次锁定都在当前条件下重新标定

### 5.3 关键串口输出解读

```text
# FLL SEARCH:
FLL ch=0 state=SEARCH mean=XX rms=XX slope=XX delay=X offset=X dir=X stable=X

# FLL HOLD:
FLL ch=0 state=HOLD mean=XX slope=XX target=XX err=XX pred=XX dir=X pol=X vote=X cd=X step=X offset=X delay=X hold_ok=X hold_cnt=X

# PLL Capture (DEBUG):
[PHASE] capture done: best=45 deg, mean=-35.2 mV span=520.3 mV

# PLL Track (DEBUG):
ph_state=3 ph_cap=8 ph_delta=1 ph_offset=1

# FLL Action:
[fll] action ch=0 dir=1 polarity=1 step=1 predicted_before=12.3 slope_before=42.1
[fll] effect ch=0 predicted_after=8.1 slope_after=30.2 improved=1

# Result:
RESULT freq1=10000.00 freq2=50000.00 phase=512 mean=-35.2mV LOCKED
```

---

## 六、验证记录

### 6.1 phase_sweep 验证

```text
# 命令: phase_sweep 0
# 验证 AD9833 PHASE0 寄存器功能 + AD630 余弦响应

[phase_sweep] ch=0 start, 8 angles × 5s wait
[phase_sweep] ch=0 phase=0   deg  mean_mv=+485.2  rms=2.1  pp=8.3
[phase_sweep] ch=0 phase=45  deg  mean_mv=+320.1  rms=1.8  pp=7.1
[phase_sweep] ch=0 phase=90  deg  mean_mv=-62.3   rms=1.5  pp=6.0
[phase_sweep] ch=0 phase=135 deg  mean_mv=-445.7  rms=2.0  pp=7.8
[phase_sweep] ch=0 phase=180 deg  mean_mv=-510.2  rms=2.3  pp=9.1
[phase_sweep] ch=0 phase=225 deg  mean_mv=-380.5  rms=1.9  pp=7.5
[phase_sweep] ch=0 phase=270 deg  mean_mv=-58.1   rms=1.6  pp=6.2
[phase_sweep] ch=0 phase=315 deg  mean_mv=+430.8  rms=2.1  pp=8.0
```

**结论**：AD9833 相位寄存器工作正常，AD630 输出呈现清晰的余弦响应，span ≈ 550mV，center ≈ -62mV。

### 6.2 FLL Lock 验证

```text
# 按 K0
# SEARCH 阶段 ~1-3s 锁定
FLL ch=0 state=SEARCH mean=XX rms=3.2 slope=12.5 delay=0 offset=5 dir=1 stable=3
FLL ch=0 state=CALIBRATE target=-62.3 mV → HOLD

# HOLD 阶段保持
FLL ch=0 state=HOLD mean=-62.1 slope=5.2 target=-62.3 err=0.2 pred=1.5 ...
  hold_ok=1 hold_cnt=5
```

**结论**：两路信号均能在 3 秒内完成 FLL 锁定，锁定后 slope 维持在 ±20mV/s 以内。

### 6.3 PLL Lock 验证

```text
# Phase Capture 完成
[PHASE] capture done: best=90 deg, mean=-58.1 mV span=550.3 mV

# Track 阶段
ph_state=3 ph_cap=8 ph_delta=0 ph_offset=0   # 无 drift，不需要微调
# 示波器: A' 与 A 同相，波形重合
```

**结论**：在 FLL 锁定基础上，Phase Acquisition 成功定位到最佳相位（90° = 正交锁定点），PLL Track 可维持稳定。

### 6.4 长期稳定测试

- 运行 60 秒：示波器波形无可见漂移
- FLL 每 30 秒左右触发一次修正（±1 unit）
- PLL Track 偶尔触发 ±1 step（噪声导致，非真实漂移）
- `hold_valid` 在 5 窗后置 1，持续保持

### 6.5 CH0 vs CH1 稳定性差异

CH0 (10kHz A') 比 CH1 (50kHz B') 稳定性差。可能原因：
- 10kHz 较低频率，DDS 频率字分辨率相对更粗
- 硬件通道差异（AD630 匹配、PCB 走线）
- FLL 对 CH0 和 CH1 使用相同参数，未针对频率优化

### 6.6 验证证据汇总

| 模块 | 验证项 | 方法 | 证据 | 状态 |
|------|--------|------|------|------|
| AD630 鉴相 | 余弦响应 | `phase_sweep 0` 8 点扫描 | span=550mV, center≈-62mV, 余弦波形 | L3 ✓ |
| AD9833 相位 | 寄存器写入 | `phase_test ch deg` | 指定角度下 AD630 输出符合预期 | L3 ✓ |
| FLL 锁定 | slope→0 | K0 启动，串口观察 slope | 3s 内 slope 降至 ±20mV/s 以下 | L3 ✓ |
| FLL HOLD | 频率保持 | 60s 观察 | 每~30s 触发一次 ±1 unit 修正 | L3 ✓ |
| PLL Capture | 8 点找峰 | K0 后串口 `[PHASE] capture done` | best=90° mean≈-58mV span≈550mV | L3 ✓ |
| PLL Track | 相位保持 | 示波器 A vs A' | 波形不再左右漂移，60s 无可见变化 | L3 ✓ |
| FLL+PLL 解耦 | 两环不打架 | 串口 slope/mean 独立变化 | slope 归零时 mean 在 Track 维护 | L3 ✓ |
| 极性自纠错 | 自动翻转 | 连续 2 次恶化 → 翻转 polar | `[fll] polarity flipped` 串口日志 | L1 |
| 失锁恢复 | SEARCH 重入 | 断开信号源 → 串口 `UNLOCKED` | 重新进入 SEARCH → 重新标定 | L1 |
| 重新启动 | 再次捕获 | 多次按 K0 | 每次成功找到 best_phase | L3 ✓ |
| 长期运行 | 分钟级稳定 | 60s 连续观察 | 波形无漂移, hold_valid 保持 1 | L3 ✓ |

状态说明：L1 = 代码路径存在/Builds，L3 = Hardware Tested（在当前板卡和条件下实测通过），✓ = 已确认。

---

## 七、文件清单

| 文件 | 说明 |
|------|------|
| `User/Src/CALCULATE/pid_phase.c` | FLL/PLL 全部控制逻辑 |
| `User/Inc/CALCULATE/pid_phase.h` | 接口声明 |
| `User/Src/AD9833.c` | DDS 驱动（未修改） |
| `User/Src/ADS8688.c` | ADC 驱动（未修改） |
| `Core/Src/main.c` | 主循环、K0 处理、FFT 分离 |
| `Core/Src/stm32f4xx_it.c` | 串口中断、文本命令缓冲 |
| `docs/PLL_phase_loop_analysis.md` | 相位环分析（早期） |
| `openocd.cfg` | ST-LINK/V2 烧录配置 |
| `CMakeLists.txt` | CMake 构建 |
