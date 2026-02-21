# GalvoLSUDriver Architecture

## Overview

GalvoLSUDriver is a high-performance laser scanning system built around an ESP32 D1 Mini that controls a resonant MEMS mirror and RGB laser output. The system uses precise timing mechanisms to generate laser dot patterns for applications like laser displays, 3D scanning, or material processing.

## System Architecture

### Hardware Components

#### Microcontroller
- **ESP32 D1 Mini (WROOM-32)** - Dual-core Xtensa processor
- **Clock Speed:** 240MHz
- **Memory:** 520KB SRAM, 4MB Flash

#### Mirror Drive System
- **Resonant MEMS Mirror** - Oscillates at high frequency (typically 2-3kHz)
- **Internal DAC (GPIO25)** - Generates sine wave for mirror drive via I2S
- **Sample Rate:** `targetFreq_hz × sineSamples`

#### Beam Detection (BD)
- **GPIO34** - Input-only pin for beam detection
- **Interrupt-driven** - Falling edge detection for timing reference
- **Timing Clusters:** Short (~20μs), Long (~130μs), Gap (reverse/flyback)

#### Feedback (FB)
- **GPIO35** - Analog input for mirror position feedback
- **ADC Sampling** - Determines scan direction via slope detection
- **Slope-based Direction** - Positive/negative slope indicates forward/reverse sweep

#### Laser Output
- **RMT (Remote Control) Module** - Hardware-based pulse generation
- **Three Channels:** R (GPIO16), G (GPIO17), B (GPIO27)
- **Timing Resolution:** 1μs (80MHz clock / 80 divider)
- **Maximum Frequency:** 1MHz pulse placement

### Software Architecture

#### Multicore Design
- **Core 0 (PRO CPU):** Serial protocol, configuration, telemetry
- **Core 1 (APP CPU):** Real-time timing, RMT programming, BD processing

#### Real-time Task
- **Priority:** High
- **Function:** `rtTask()` - Handles time-critical operations
- **Pinned:** Core 1

#### Serial Protocol Task
- **Priority:** Medium
- **Function:** JSON command processing, configuration updates
- **Pinned:** Core 0

## Data Flow

### 1. Mirror Drive
```
Configuration → MirrorConfig → I2S DAC → GPIO25 → MEMS Mirror
```

### 2. Beam Detection Processing
```
GPIO34 (BD) → Interrupt → ISR → Timestamp Queue → RT Task → Cluster Analysis
```

### 3. Feedback Direction Detection
```nGPIO35 (FB) → ADC → Slope Calculation → Direction Classification
```

### 4. Laser Output Generation
```
Dot Buffer → RMT Items → RMT Channels → GPIO16/17/27 → Lasers
```

## Key Components

### Mirror Configuration
```c
struct MirrorConfig {
  float targetFreq_hz;    // Target oscillation frequency
  float amplitude;        // Sine wave amplitude (0.0-1.0)
  int sineSamples;        // Samples per sine wave cycle
  int sampleRate_hz;      // Computed: targetFreq_hz × sineSamples
  int16_t *sineTable;     // Precomputed sine values
  size_t sineTableSize;   // Table size in samples
};
```

### Beam Detection Timing
```c
struct BDTimingConfig {
  uint32_t shortWindow_us;    // Short cluster threshold
  uint32_t longWindow_us;     // Long cluster threshold
  uint32_t gapWindow_us;      // Gap cluster threshold
  uint8_t emaShift;           // EMA smoothing factor
  uint32_t noSignalTimeout_ms; // BD loss timeout
};
```

### Feedback Configuration
```c
struct FBConfig {
  bool forwardSlopePositive;  // Slope polarity for forward detection
  uint32_t slope_dt_us;       // Time between ADC samples
  int validThreshold;         // Slope validation threshold
};
```

### Laser Configuration
```c
struct TTLConfig {
  uint32_t ttlFreq_hz;        // Maximum pulse frequency (1MHz default)
  uint32_t pixelWidth_us;     // Laser pulse width
  int32_t extraOffset_us;     // Time offset for dot placement
  uint32_t minSpacing_us;     // Minimum spacing between pulses
};
```

### Dot Buffer Management
```c
struct DotBufferConfig {
  uint16_t bufferSize;        // Maximum dots (1024)
  uint8_t activeBuffer;       // Currently active buffer (0 or 1)
  bool testPatternEnable;      // Generate test pattern when empty
  uint16_t testCount;         // Number of test pattern dots
};

struct Dot {
  uint16_t idxNorm;          // Normalized position (0-65535)
  uint8_t rgbMask;           // RGB bitmask (bit0=R, bit1=G, bit2=B)
};
```

## Timing and Synchronization

### Forward Window Detection
1. **BD Edge Detection:** Falling edge on GPIO34 triggers ISR
2. **Time Interval Analysis:** Calculate dt between consecutive edges
3. **Cluster Classification:** Short/long/gap based on EMA tracking
4. **Direction Determination:** FB slope analysis for forward/reverse
5. **Window Selection:** Use forward windows for dot output

### Dot Position Mapping
```c
// Normalized index to time conversion
offset_us = (uint32_t)idxNorm * (filteredSweep_us - 1) / 65535
offset_us += laser.extraOffset_us  // Apply offset
```

### RMT Pulse Generation
- **Clock:** 80MHz / 80 = 1MHz (1μs resolution)
- **Items:** Pre-built sequences for each channel
- **Non-blocking:** RMT writes are asynchronous
- **Maximum:** 256 items per channel

## Communication Protocol

### Serial Interface
- **Baud Rate:** 115200
- **Format:** JSON Lines (JSONL)
- **Commands:** get, set, arm, dots.inactive, dots.swap, status

### Command Examples
```json
// Get telemetry
{"cmd":"get","path":"*"}

// Set laser parameters
{"cmd":"set","path":"ttl.pixelWidth_us","value":2}

// Upload dots
{"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, ...]}

// Request buffer swap
{"cmd":"dots.swap","value":true}
```

### Telemetry Structure
```json
{
  "telemetry": {
    "bd": {"shortEMA": 18, "longEMA": 128, "gapEMA": 512, "cluster": 1, "locked": true, "sweep_us": 130},
    "fb": {"v0": 2048, "v1": 2100, "slope": 52, "forward": true},
    "ttl": {"pixelWidth_us": 1, "extraOffset_us": 0, "minSpacing_us": 1, "armed": true},
    "dots": {"testPatternEnable": true, "testCount": 100, "activeBuf": 0, "dotCount": 50},
    "system": {"arm": true}
  }
}
```

## Safety and Recovery

### Fault Detection
- **BD Loss Detection:** Timeout-based recovery
- **No Signal Timeout:** 1000ms default
- **Recovery Mode:** Configurable pulse pattern

### Safety Features
- **TTL Default State:** Low (off) on boot
- **Disarm on Loss:** Immediate laser shutdown
- **Timeout Protection:** Prevents stuck high states

## Performance Characteristics

### Timing Precision
- **Mirror Drive:** Sub-microsecond stability
- **BD Processing:** Interrupt latency < 10μs
- **Laser Output:** 1μs resolution, 1MHz maximum

### Throughput
- **Maximum Dots:** Limited by sweep duration and min spacing
- **Buffer Capacity:** 1024 dots per buffer
- **Update Rate:** Real-time with no blocking operations

### Resource Usage
- **Memory:** ~50KB for sine tables, buffers, and state
- **CPU:** Core 1 dedicated to RT task, Core 0 for protocol
- **DMA:** I2S and RMT use hardware DMA for efficiency

## Configuration Parameters

### Mirror Settings
- `mirror.targetFreq_hz`: 2000.0 Hz (default)
- `mirror.amplitude`: 0.8 (default)
- `mirror.sineSamples`: 1024 (default)

### BD Timing
- `bd.shortWindow_us`: 20 μs
- `bd.longWindow_us`: 130 μs
- `bd.gapWindow_us`: 500 μs
- `bd.emaShift`: 4
- `bd.noSignalTimeout_ms`: 1000 ms

### FB Settings
- `fb.forwardSlopePositive`: true
- `fb.slope_dt_us`: 8 μs

### TTL Settings
- `ttl.ttlFreq_hz`: 1000000 Hz
- `ttl.pixelWidth_us`: 1 μs
- `ttl.extraOffset_us`: 0 μs

### Dot Buffer
- `dots.testPatternEnable`: true
- `dots.testCount`: 100
- `dots.activeBuf`: 0

### System Control
- `system.arm`: false (default)

## Development Notes

### Build Requirements
- **Arduino IDE** with ESP32 board support
- **ArduinoJson** library
- **ESP32 Arduino Core** 2.3.7+

### Testing Considerations
- **BD Signal Quality:** Critical for timing accuracy
- **FB Signal Conditioning:** May require external amplification
- **Laser Safety:** Always use appropriate protective equipment
- **Power Supply:** Stable voltage for analog circuits

### Debugging Tools
- **Serial Monitor:** JSON protocol for configuration
- **Telemetry:** Real-time system monitoring
- **Python Host:** GUI for dot pattern generation and upload

## Future Enhancements

### Hardware Improvements
- **Higher Resolution DAC:** External 16-bit DAC for better mirror control
- **Optical Feedback:** Direct position sensing instead of FB slope
- **Multiple Mirrors:** Synchronized scanning for 2D/3D patterns

### Software Features
- **Advanced Patterns:** Vector graphics, text rendering
- **Color Blending:** PWM-based color mixing
- **Motion Planning:** Smooth trajectory generation
- **Network Control:** WiFi-based remote operation

### Performance Optimizations
- **DMA Improvements:** Larger buffer sizes for continuous operation
- **Interrupt Optimization:** Lower latency BD processing
- **Power Management:** Dynamic frequency scaling

This architecture provides a solid foundation for high-performance laser scanning applications while maintaining flexibility for future enhancements and optimizations.