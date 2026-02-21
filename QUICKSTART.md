# Quick Start Guide

## Prerequisites

- **Hardware:** ESP32 D1 Mini, resonant MEMS mirror, RGB lasers, laser drivers
- **Software:** Arduino IDE with ESP32 support, Python 3.8+
- **Dependencies:** ArduinoJson library, pyserial, gradio, pillow, numpy

## Step 1: Hardware Setup

### Connect Components

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

### Power Connections
- **ESP32:** 5V USB power
- **Lasers:** Separate 5V supply with current limiting
- **Signal Conditioning:** Optional amplification for FB signal

## Step 2: Software Installation

### Arduino IDE Setup

1. **Install ESP32 Board Support**
   ```
   File > Preferences > Additional Boards Manager URLs
   Add: https://dl.espressif.com/dl/package_esp32_index.json
   ```

2. **Install Libraries**
   ```
   Sketch > Include Library > Manage Libraries
   Search: "ArduinoJson" → Install
   ```

3. **Configure Board**
   ```
   Tools > Board > ESP32 Arduino > LOLIN D1 mini Pro
   Tools > Flash Size > 4MB (32Mb)
   ```

### Python Host Application

```bash
# Clone the repository
git clone https://github.com/yourusername/GalvoLSUDriver.git
cd GalvoLSUDriver

# Install Python dependencies
pip install pyserial gradio pillow numpy

# Run the controller
python mirror_controller.py
```

## Step 3: Upload Firmware

1. **Open Arduino IDE**
2. **Open `GalvoLSUDriver.ino`**
3. **Select the correct COM port**
4. **Click Verify and Upload**
5. **Open Serial Monitor** to confirm boot message

## Step 4: Basic Operation

### Using the Python GUI

1. **Launch the Application**
   ```bash
   python mirror_controller.py
   ```

2. **Connect to Serial Port**
   - Select the correct COM port
   - Click "Connect"

3. **Arm the System**
   - Toggle the "ARM" checkbox
   - Click "Apply Arm"

4. **Monitor Telemetry**
   - Click "Telemetry (*)" to view system status

### Using Serial Commands

```bash
# Connect with serial monitor
# Baud rate: 115200, Newline ending

# Arm the system
{"cmd":"arm","value":true}

# Get telemetry
{"cmd":"get","path":"*"}

# Get status
{"cmd":"status"}
```

## Step 5: Create and Upload Patterns

### Using the Python GUI

1. **Generate Pattern**
   - Select pattern type: "single", "line", or "color_bars"
   - Configure parameters (number of dots, colors, etc.)
   - Click "Generate"

2. **Upload Pattern**
   - Copy the generated JSON to the "Dots JSON" box
   - Toggle "Swap after upload" if desired
   - Click "Upload Inactive (and optional swap)"

3. **Monitor Results**
   - Check the response box for upload confirmation
   - Monitor telemetry for system status

### Using Serial Commands

```bash
# Upload custom dots
{"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, {"idxNorm":32768,"rgbMask":2}, ...]}

# Request buffer swap
{"cmd":"dots.swap","value":true}
```

## Step 6: Configuration

### Adjust Laser Settings

```bash
# Set laser pulse width (1-10 μs)
{"cmd":"set","path":"ttl.pixelWidth_us","value":2}

# Set frequency for minimum spacing (1-1000000 Hz)
{"cmd":"set","path":"ttl.ttlFreq_hz","value":500000}
```

### Adjust Mirror Settings

```bash
# Set mirror frequency (1000-3000 Hz)
{"cmd":"set","path":"mirror.targetFreq_hz","value":2500.0}

# Set mirror amplitude (0.1-1.0)
{"cmd":"set","path":"mirror.amplitude","value":0.8}
```

### Adjust Test Pattern

```bash
# Enable/disable test pattern
{"cmd":"set","path":"dots.testPatternEnable","value":true}

# Set test pattern count
{"cmd":"set","path":"dots.testCount","value":100}
```

## Step 7: Advanced Features

### Image to Dots Conversion

1. **Load Image**
   - Use the "Image Line → Dots" section in the GUI
   - Select image path on the server
   - Configure line index, threshold, and gamma

2. **Generate and Send**
   - Click "Generate (no send)" to preview
   - Click "Send line now" to upload

### Auto Image Scanning

1. **Configure Auto Scan**
   - Set start and end line indices
   - Configure interval and loop settings
   - Enable serpentine scanning if needed

2. **Start Auto Scan**
   - Click "Start Auto Scan"
   - Monitor progress in the response box
   - Click "Stop Auto Scan" to halt

## Troubleshooting

### Common Issues

#### Connection Problems
- **No COM ports found:** Check USB connection, install drivers
- **Connection timeout:** Verify baud rate, check power supply

#### Laser Output Issues
- **No laser output:** Check arming, verify TTL signals
- **Incorrect timing:** Verify RMT configuration, check clock settings

#### Pattern Issues
- **Distorted patterns:** Check mirror frequency, verify timing
- **Color issues:** Check RGB channel wiring, verify laser drivers

#### Performance Issues
- **System lag:** Check CPU usage, verify buffer sizes
- **Timing errors:** Check interrupt priorities, verify clock settings

### Diagnostic Commands

```bash
# Check system status
{"cmd":"status"}

# Get detailed telemetry
{"cmd":"get","path":"*"}

# Check current configuration
{"cmd":"get","path":"ttl.pixelWidth_us"}
{"cmd":"get","path":"dots.testPatternEnable"}
```

## Safety Notes

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

## Next Steps

### Explore Advanced Features
- **Custom Pattern Generation:** Create your own dot patterns
- **Image Processing:** Convert images to laser patterns
- **Real-time Control:** Implement live pattern updates
- **Performance Optimization:** Fine-tune timing and throughput

### Development
- **Modify Firmware:** Customize for your specific application
- **Add Features:** Implement new functionality
- **Optimize Performance:** Improve timing accuracy and throughput
- **Contribute:** Share your improvements with the community

### Documentation
- **Review Architecture:** Read `docs/ARCHITECTURE.md` for technical details
- **Check Specification:** Review `plans/spec.md` for complete project details
- **Explore Code:** Study the source code for implementation details

---

**Congratulations!** You've successfully set up and started using the GalvoLSUDriver system. Enjoy creating amazing laser patterns and exploring the possibilities of precision laser scanning!