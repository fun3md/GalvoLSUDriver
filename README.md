# GalvoLSUDriver

A high-performance laser scanning system built around an ESP32 D1 Mini that controls a resonant MEMS mirror and RGB laser output. This project provides precise timing control for laser dot patterns, making it ideal for laser displays, 3D scanning, or material processing applications.

## Features

- **Resonant Mirror Control** - Precise sine wave generation via internal DAC
- **Beam Detection (BD)** - Timing-based window detection with clustering
- **Feedback Direction (FB)** - Slope-based forward/reverse sweep detection
- **RGB Laser Output** - Hardware-timed TTL pulses via RMT module
- **Double-buffered Dot Storage** - Seamless pattern updates
- **Serial JSON Protocol** - Easy remote control and configuration
- **Multicore Architecture** - Real-time performance with dedicated cores
- **Test Pattern Generation** - Built-in patterns for testing and calibration

## Hardware Requirements

### Core Components
- **ESP32 D1 Mini (WROOM-32)** - Main controller
- **Resonant MEMS Mirror** - Oscillating mirror for beam steering
- **RGB Lasers** - Three laser diodes (R, G, B channels)
- **Laser Drivers** - 3.3V logic-compatible drivers

### Pin Connections
```
ESP32 D1 Mini    | Component
----------------|----------------
GPIO25 (DAC1)    | MEMS Mirror Drive
GPIO34           | Beam Detect (BD) Input
GPIO35           | Feedback (FB) Input
GPIO16           | Red Laser (RMT Channel 0)
GPIO17           | Green Laser (RMT Channel 1)
GPIO27           | Blue Laser (RMT Channel 2)
```

### Additional Components
- **Power Supply** - Stable 5V/3.3V for ESP32 and lasers
- **Signal Conditioning** - Optional amplification for FB signal
- **Optical Components** - Lenses, mirrors, or scanners as needed

## Software Architecture

### Multicore Design
- **Core 0 (PRO CPU):** Serial protocol, configuration, telemetry
- **Core 1 (APP CPU):** Real-time timing, RMT programming, BD processing

### Key Components
- **Mirror Drive:** I2S-based sine wave generation
- **BD Processing:** Interrupt-driven timing analysis
- **FB Direction:** ADC-based slope detection
- **Laser Output:** RMT-based pulse generation
- **Dot Management:** Double-buffered storage with swap capability

## Installation

### Arduino IDE Setup

1. **Install ESP32 Board Support**
   - Open Arduino IDE
   - Go to `File > Preferences`
   - Add `https://dl.espressif.com/dl/package_esp32_index.json` to Additional Boards Manager URLs
   - Go to `Tools > Board > Boards Manager`
   - Search for "ESP32" and install "esp32 by Espressif Systems"

2. **Install Required Libraries**
   - Open Library Manager: `Sketch > Include Library > Manage Libraries`
   - Install "ArduinoJson" by Benoit Blanchon
   - Ensure ESP32 Arduino Core 2.3.7+ is installed

3. **Configure Board Settings**
   - Select "LOLIN D1 mini Pro" or "ESP32 Dev Module"
   - Set Flash Size to "4MB (32Mb)"
   - Set Partition Scheme to "Default 4MB with spiffs"

### Uploading Firmware

1. **Connect ESP32 D1 Mini** to your computer via USB
2. **Select the correct COM port** in Arduino IDE
3. **Open `GalvoLSUDriver.ino`** in the Arduino IDE
4. **Verify and Upload** the sketch
5. **Monitor Serial Output** to confirm successful boot

### Python Host Application

The project includes a Python GUI for easy control and dot pattern generation:

```bash
# Install dependencies
pip install pyserial gradio pillow numpy

# Run the controller
python mirror_controller.py
```

## Usage

### Basic Operation

1. **Connect to Serial Port**
   - Use the Python GUI or serial monitor
   - Baud rate: 115200
   - Line ending: Newline

2. **Arm the System**
   ```json
   {"cmd":"arm","value":true}
   ```

3. **Monitor Telemetry**
   ```json
   {"cmd":"get","path":"*"}
   ```

### Dot Pattern Upload

1. **Generate Pattern** using the Python GUI
2. **Upload to Inactive Buffer**
   ```json
   {"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, ...]}
   ```
3. **Request Buffer Swap**
   ```json
   {"cmd":"dots.swap","value":true}
   ```

### Configuration Commands

```json
// Set laser pulse width
{"cmd":"set","path":"ttl.pixelWidth_us","value":2}

// Set frequency for minimum spacing
{"cmd":"set","path":"ttl.ttlFreq_hz","value":500000}

// Enable/disable test pattern
{"cmd":"set","path":"dots.testPatternEnable","value":false}

// Set test pattern count
{"cmd":"set","path":"dots.testCount","value":200}
```

## Safety Considerations

### Laser Safety
- **Always wear appropriate laser safety glasses**
- **Never look directly into laser beams**
- **Use beam blocks when not in use**
- **Ensure proper enclosure for all laser operations**

### Electrical Safety
- **Use proper power supply ratings**
- **Ensure good grounding**
- **Use appropriate current limiting for lasers**
- **Avoid static discharge on sensitive components**

### System Safety
- **Start with low power settings**
- **Test with safe patterns first**
- **Monitor system temperature**
- **Have emergency stop procedures ready**

## Troubleshooting

### Common Issues

#### BD Signal Problems
- **No BD edges detected:** Check wiring, signal levels
- **Inconsistent timing:** Verify signal conditioning, add filtering
- **Cluster lock failure:** Adjust timing thresholds, check signal quality

#### FB Direction Issues
- **Incorrect forward/reverse detection:** Adjust slope polarity, check FB signal
- **No valid slope:** Verify FB signal amplitude, add amplification

#### Laser Output Problems
- **No laser output:** Check arming, verify TTL signals with oscilloscope
- **Incorrect timing:** Verify RMT configuration, check clock settings
- **Color issues:** Check RGB channel wiring, verify laser drivers

#### Serial Communication
- **No response:** Check baud rate, verify COM port
- **JSON parse errors:** Check message formatting, verify line endings
- **Timeout errors:** Check connection stability, verify power supply

### Diagnostic Commands

```json
// Get system status
{"cmd":"status"}

// Get detailed telemetry
{"cmd":"get","path":"*"}

// Check current configuration
{"cmd":"get","path":"ttl.pixelWidth_us"}
{"cmd":"get","path":"dots.testPatternEnable"}
```

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

## Development

### Code Structure

```
GalvoLSUDriver/
├── GalvoLSUDriver.ino          # Main Arduino sketch
├── mirror_controller.py        # Python host application
├── README.md                  # This file
├── docs/
│   └── ARCHITECTURE.md        # Detailed system architecture
└── plans/
    └── spec.md                # Full project specification
```

### Key Files

- **`GalvoLSUDriver.ino`:** Main firmware with all core functionality
- **`mirror_controller.py`:** Python GUI for control and pattern generation
- **`docs/ARCHITECTURE.md`:** Detailed technical documentation
- **`plans/spec.md`:** Complete project specification

### Contributing

1. **Fork the repository**
2. **Create a feature branch**
3. **Make your changes**
4. **Test thoroughly**
5. **Submit a pull request**

### Testing

- **Unit Tests:** Verify individual components
- **Integration Tests:** Test system-level functionality
- **Performance Tests:** Measure timing accuracy and throughput
- **Safety Tests:** Verify all safety mechanisms

## Applications

### Laser Displays
- **Laser Light Shows:** Create complex patterns and animations
- **Laser Projectors:** Project images and text
- **Laser Art:** Create laser-based artwork

### 3D Scanning
- **Laser Triangulation:** Measure distances and shapes
- **3D Reconstruction:** Build 3D models from laser scans
- **Quality Control:** Inspect manufactured parts

### Material Processing
- **Laser Cutting:** Precise material cutting
- **Laser Engraving:** Mark surfaces with patterns
- **Laser Welding:** Join materials with precision

### Scientific Applications
- **Optical Tweezers:** Manipulate microscopic particles
- **Laser Spectroscopy:** Analyze material properties
- **Particle Tracking:** Monitor particle movement

## License

This project is provided for educational and research purposes. Please ensure compliance with all applicable laws and regulations regarding laser use and safety.

## Acknowledgments

- **ESP32 Community** - Excellent hardware and software support
- **Arduino Framework** - Accessible development platform
- **Open Source Libraries** - ArduinoJson and other dependencies

## Support

For questions, issues, or contributions:
- **Documentation:** Check the detailed docs in the `docs/` folder
- **Specification:** Review the complete spec in `plans/spec.md`
- **Community:** Engage with the ESP32 and laser enthusiast communities

---

**Note:** This project involves high-power lasers and requires proper safety precautions. Always follow laser safety guidelines and local regulations.