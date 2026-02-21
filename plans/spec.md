## Full Project Specification (Arduino / ESP32 D1 Mini WROOM-32)
**Project name:** Resonant Mirror Laser Dot Controller (BD + FB Direction, RGB TTL, RMT timed)

### 0. Scope
Firmware for an **ESP32 D1 Mini (WROOM-32)** that:
- Drives a resonant MEMS mirror using **internal DAC (GPIO25)** via I2S sine output.
- Uses **Beam Detect (BD)** edges to locate candidate “windows” in the scan.
- Uses MEMS **feedback (FB)** (piezo analog) to determine scan **direction** so the system can identify **forward sweep** even when BD window timing swaps.
- Outputs high-speed **TTL pulses (idle low, active high)** for **R/G/B lasers** with up to **1 MHz** pulse placement using **RMT** hardware timing.
- Accepts all runtime control via a **serial command/control protocol** (no interactive key commands).
- Supports uploading dot lists to memory with **double-buffered swap**, and generates a **test pattern** when no dots are provided.
- Runs time-critical functions pinned to a dedicated core (core1), host/protocol on the other (core0).

---

## 1. Hardware / Platform
### 1.1 MCU
- ESP32 D1 Mini (WROOM-32), dual-core Xtensa, Arduino framework.

### 1.2 GPIO Assignments
- **Mirror drive DAC:** GPIO25 (DAC1) via I2S DAC mode
- **BD input:** GPIO34 (input-only), interrupt on falling edge
- **FB input:** GPIO35 (input-only), ADC input (attenuation configurable)
- **TTL outputs (idle low, active high):**
  - R: GPIO16
  - G: GPIO17
  - B: GPIO27

### 1.3 Electrical assumptions
- TTL outputs drive external laser drivers that accept 3.3V logic.
- FB signal is low-voltage analog; firmware must support ADC attenuation and thresholding. External analog conditioning may be required (bias/divider).

---

## 2. Mirror Drive (Internal DAC)
### 2.1 Waveform generation
- Continuous sine table streamed via I2S to internal DAC (GPIO25).
- Parameters:
  - `mirror.targetFreq_hz` (float)
  - `mirror.sineSamples` (int, fixed at build or settable)
  - `mirror.amplitude` (0..1 scale or DAC units)
- Sample rate rule:
  - `mirror.sampleRate_hz = mirror.targetFreq_hz * mirror.sineSamples`
- Must support runtime updates to frequency and amplitude without reboot.

### 2.2 Stability requirements
- Continuous output with no blocking operations on the time-critical core.
- Frequency changes must be applied safely (atomic config update + reinit I2S if needed).

---

## 3. Beam Detect (BD) Processing
### 3.1 Input capture
- BD is captured on **falling edge interrupt**.
- ISR requirements:
  - IRAM resident
  - Must only timestamp (`micros()` or cycle counter) and enqueue event
  - No floating point, no printing, no heavy logic

### 3.2 Timing-based window detection
- BD edges are not assumed to arrive in a fixed PRE/START/END order.
- The system must classify inter-edge intervals `dt` into clusters:
  - `short` (~20 µs nominal)
  - `long` (~130 µs nominal)
  - `gap` (reverse/flyback region, much larger)
- Classification uses:
  - bootstrap seeds (20/130) until locked
  - exponential moving averages (integer EMA) to track drift
  - sanity bounds to reject outliers

### 3.3 Candidate window definition
- Any consecutive edge pair whose `dt` is classified as `short` or `long` forms a **window candidate**:
  - `tA = previousEdgeTime`
  - `tB = currentEdgeTime`
  - `window_us = dt`

---

## 4. Feedback (FB) Direction Disambiguation
### 4.1 Purpose
Because forward vs reverse can swap which BD window is usable, FB is used to label each window candidate as **forward** or **reverse**.

### 4.2 FB acquisition mode (initial)
- FB is treated as analog input (ADC on GPIO35).
- Direction is estimated by **slope sign** near the window boundary:
  - Take two ADC samples separated by `fb.slope_dt_us`
  - `slope = v1 - v0`
  - `dir = sign(slope)`
- Forward mapping:
  - `fb.forwardSlopePositive` (bool): if true, positive slope ⇒ forward; else negative slope ⇒ forward

### 4.3 Configurable parameters
- `fb.slope_dt_us` (default 8–10 µs)
- `fb.adcAttenuation` (platform dependent)
- `fb.validThreshold` (p2p or absolute slope threshold, optional)

### 4.4 Telemetry outputs
- last `fb.v0`, `fb.v1`, `fb.slope`
- inferred `fb.dir` and `fb.forward`

---

## 5. Forward Window Selection and Timing Filter
### 5.1 Forward window selection
For each detected window candidate (short/long):
- Determine `forward` using FB slope sign.
- If forward:
  - treat the candidate as the active forward sweep:
    - `t_start = tA`
    - `t_end = tB`
    - `lastSweep_us = window_us`
  - schedule dots relative to `t_start`
- If reverse: do not emit dots for that window.

### 5.2 Sweep duration filtering
Maintain `filteredSweep_us` with integer EMA updated only when a **forward** window is accepted:
- `filteredSweep_us = filteredSweep_us + ((lastSweep_us - filteredSweep_us) >> emaShift)`
- `emaShift` settable (e.g. 3..6)

---

## 6. RGB TTL Dot Output (RMT, 1 MHz capable)
### 6.1 Outputs
- Three independent TTL channels: R, G, B.
- Idle low, active high.

### 6.2 Timing engine
- Use **RMT** to generate pulses with sub-microsecond tick resolution.
- Pulses are prepared as sequences (“items”) per channel for each forward window and started at the forward `t_start`.

### 6.3 Dot pulse parameters
- Global pulse width:
  - `laser.pixelWidth_us` (integer, ≥1)
- Optional global offset:
  - `laser.extraOffset_us` (signed int)

### 6.4 TTL frequency constraint / minimum spacing
- Config:
  - `ttl.ttlFreq_hz` (default 1,000,000)
- Derived:
  - `ttl.minSpacing_us = ceil(1e6 / ttl.ttlFreq_hz)` (1 µs at 1 MHz)
- Firmware must ensure pulses are not scheduled closer than `minSpacing_us` unless explicitly allowed (default: enforce).
- If mapping produces collisions (multiple dots map to same offset_us), firmware either:
  - drops extras (default), or
  - merges by OR’ing rgb masks (optional mode)

---

## 7. Dots: Memory, Double Buffering, Test Pattern
### 7.1 Dot coordinate system (normalized index)
Dots are specified by normalized index across the currently accepted forward sweep:
- `idxNorm ∈ [0..65535]`

Mapping to time (per accepted forward sweep):
- `offset_us = (uint32_t)idxNorm * (filteredSweep_us - 1) / 65535`
- `offset_us += laser.extraOffset_us` (clamped ≥0 and < sweep)

### 7.2 Dot representation
Each dot:
- `idxNorm` (uint16)
- `rgbMask` (uint8, bit0=R bit1=G bit2=B)
Optional future extension: per-dot width.

### 7.3 Double-buffer behavior
- Two dot buffers: `buffer0`, `buffer1`
- Host writes only to inactive buffer.
- Swap request `dots.swap=1` flips active buffer at a safe boundary (after processing a window / during gap).
- Active buffer is read only by RT core.

### 7.4 Test pattern
If active dot buffer is empty OR `dots.testPatternEnable=1`:
- generate deterministic test pattern:
  - configurable count `dots.testCount`
  - evenly distributed `idxNorm`
  - RGB cycle or fixed mask

---

## 8. Maximum Dots Computation
Reported max useful dots for the current forward sweep:
- `dotMaxBySpacing = floor(filteredSweep_us / ttl.minSpacing_us)`
- `dotMax = min(DOT_CAP, dotMaxBySpacing)`

Notes:
- Because indices are normalized, host does not need to change dot count when `dotMax` changes; `dotMax` is primarily for diagnostics and for truncation policy.

---

## 9. Serial Control Protocol (command/control only)
### 9.1 Transport
- UART over USB serial.
- Line-delimited JSON (JSONL). Each request on one line, each response on one line.

### 9.2 Required commands
- `get`: read variables
  - `{"cmd":"get","path":"*"}`
- `set`: write variables
  - `{"cmd":"set","path":"mirror.targetFreq_hz","value":2300.0}`
- `arm`: master enable
  - `{"cmd":"arm","value":1|0}`
- `dots.inactive`: upload dot list to inactive buffer
- `dots.swap`: request buffer swap
- `status`: optional shortcut to return telemetry snapshot

### 9.3 Variable accessibility
All configuration variables must be reachable by `get/set`, except hard-pinned compile-time constants (reported read-only).

### 9.4 Telemetry
Periodic unsolicited telemetry (configurable rate), containing at minimum:
- BD cluster EMAs (short/long/gap)
- last window type detected
- FB slope and forward decision
- lastSweep_us, filteredSweep_us
- dot buffer active, dot count
- computed `dotMax`, `minSpacing_us`
- recovery state and counters

---

## 10. Recovery / Fault Handling
### 10.1 No-BD watchdog
If no BD edges for `bd.noSignalTimeout_ms`:
- disarm dot output immediately
- reset classifier lock state
- enter recovery mode

### 10.2 Recovery mode
- Periodically emit a configurable recovery pulse pattern on one or more TTL channels to help reacquire BD (if needed):
  - `recovery.period_us`
  - `recovery.pulse_us`
  - `recovery.rgbMask`
- Recovery stops immediately upon receiving BD edges again.

### 10.3 Safety
- TTL outputs must default to OFF (low) on boot and when disarmed.
- Timeouts prevent any channel from being stuck high.

---

## 11. Multicore / Tasking Requirements (Arduino)
### 11.1 Core allocation
- **Core 1 (APP CPU):** time-critical RT task + RMT programming + BD event processing
- **Core 0 (PRO CPU):** serial protocol + registry + telemetry + dot uploads

### 11.2 Task requirements
- RT task pinned to core 1, high priority.
- Serial task pinned to core 0, medium priority.
- Shared data only via:
  - RT-owned active buffers
  - atomic config snapshots
  - swap flags
  - queues/ring buffers for ISR→RT communication

### 11.3 ISR constraints
- BD ISR must be minimal, IRAM safe, and never call blocking APIs.

---

## 12. Performance Targets / Acceptance Criteria
1. Mirror DAC output stable at configured `targetFreq_hz` and adjustable at runtime.
2. BD edges are clustered into short/long/gap and remain locked over time.
3. FB slope method consistently labels forward vs reverse; direction toggle fixes polarity.
4. On every forward window, RGB TTL pulses occur at dot positions derived from `idxNorm`.
5. Supports up to **1 MHz min spacing**, with correct max-dot computation and collision handling.
6. Dot lists can be uploaded to inactive buffer and swapped without disrupting timing.
7. If dot list is empty, test pattern is output on forward windows.
8. On BD loss, system enters recovery mode and exits immediately when BD returns.
9. All variables are accessible via serial protocol; no interactive single-key commands remain.
10. Time-critical functions operate on dedicated core with no serial/logging-induced jitter.

---

## 13. Configuration Parameters (initial required set)
- `mirror.targetFreq_hz`, `mirror.amplitude`, `mirror.sineSamples`
- `bd.noSignalTimeout_ms`, `bd.emaShift`, classifier sanity windows
- `fb.forwardSlopePositive`, `fb.slope_dt_us`, `fb.adcAttenuation`
- `ttl.ttlFreq_hz`
- `laser.pixelWidth_us`, `laser.extraOffset_us`
- `dots.testPatternEnable`, `dots.testCount`, `dots.activeBuf`, `dots.swap`
- `recovery.enable`, `recovery.period_us`, `recovery.pulse_us`, `recovery.rgbMask`
- `system.arm`

This is the complete specification for the Arduino-based implementation with BD timing classification, FB-based direction selection, multicore separation, and high-speed RGB TTL via RMT.