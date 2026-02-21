# Changelog

All notable changes to the GalvoLSUDriver project will be documented in this file.

## [1.0.0] - 2026-02-21
### Initial Release

#### Features
- **Resonant Mirror Control** - Precise sine wave generation via internal DAC
- **Beam Detection (BD)** - Timing-based window detection with clustering
- **Feedback Direction (FB)** - Slope-based forward/reverse sweep detection
- **RGB Laser Output** - Hardware-timed TTL pulses via RMT module
- **Double-buffered Dot Storage** - Seamless pattern updates
- **Serial JSON Protocol** - Easy remote control and configuration
- **Multicore Architecture** - Real-time performance with dedicated cores
- **Test Pattern Generation** - Built-in patterns for testing and calibration

#### Hardware Support
- **ESP32 D1 Mini (WROOM-32)** - Main controller
- **Resonant MEMS Mirror** - Oscillating mirror for beam steering
- **RGB Lasers** - Three laser diodes (R, G, B channels)
- **Laser Drivers** - 3.3V logic-compatible drivers

#### Software Components
- **Arduino Firmware** - Complete laser scanning control
- **Python Host Application** - GUI for control and pattern generation
- **Comprehensive Documentation** - Architecture, API, and usage guides

#### Key Features
- **1MHz Laser Output** - High-speed pulse generation
- **1024 Dot Capacity** - Large pattern storage
- **Real-time Control** - No blocking operations
- **Safety Features** - Automatic disarming and recovery
- **Telemetry System** - Real-time system monitoring

#### Configuration Options
- **Mirror Settings** - Frequency, amplitude, and sample rate
- **BD Timing** - Cluster thresholds and EMA smoothing
- **FB Direction** - Slope polarity and detection parameters
- **TTL Configuration** - Pulse width, frequency, and offsets
- **Dot Buffer** - Test patterns and buffer management

#### Communication Protocol
- **JSON Lines (JSONL)** - Simple, human-readable format
- **115200 Baud** - Fast serial communication
- **Comprehensive Commands** - Full system control
- **Error Handling** - Robust error reporting
- **Telemetry** - Detailed system monitoring

#### Development Tools
- **Python GUI** - Easy control and pattern generation
- **Image Processing** - Convert images to laser patterns
- **Auto-scanning** - Automated pattern generation
- **Real-time Monitoring** - Live system status

#### Documentation
- **Architecture Guide** - Complete system design
- **API Reference** - Detailed protocol specification
- **Quick Start Guide** - Step-by-step setup instructions
- **FAQ** - Common questions and solutions
- **Changelog** - Version history and updates

#### Safety Features
- **Automatic Disarming** - On BD loss or system errors
- **Recovery Mode** - Configurable recovery patterns
- **Timeout Protection** - Prevents stuck states
- **Default States** - Safe defaults on boot
- **Monitoring** - Real-time system health checks

#### Performance Characteristics
- **Timing Precision** - Sub-microsecond stability
- **Throughput** - Real-time operation with no blocking
- **Memory Usage** - Efficient memory management
- **CPU Utilization** - Dedicated cores for critical tasks
- **DMA Support** - Hardware acceleration for I2S and RMT

#### Future Enhancements (Planned)
- **Advanced Patterns** - Vector graphics and text rendering
- **Color Blending** - PWM-based color mixing
- **Motion Planning** - Smooth trajectory generation
- **Network Control** - WiFi-based remote operation
- **Performance Optimizations** - Improved timing and throughput

## [Unreleased]
### Planned Features
- **Enhanced Error Handling** - More detailed error reporting
- **Advanced Pattern Types** - New pattern generation algorithms
- **Improved Performance** - Optimizations for specific applications
- **Additional Hardware Support** - Support for more ESP32 variants
- **Enhanced Documentation** - More detailed guides and examples

### Known Issues
- **FB Signal Conditioning** - May require external amplification
- **Power Supply Stability** - Critical for analog circuits
- **Timing Calibration** - May require manual adjustment
- **Laser Safety** - Requires proper safety measures

## Migration Guide

### From Previous Versions
This is the initial release, so no migration is required.

### To Future Versions
Future versions will maintain backward compatibility with version 1.0.

## Compatibility

### Hardware Compatibility
- **ESP32 D1 Mini (WROOM-32)** - Fully supported
- **Other ESP32 Variants** - May require modifications
- **Resonant Mirrors** - Compatible with standard MEMS mirrors
- **Laser Systems** - Compatible with 3.3V logic lasers

### Software Compatibility
- **Arduino IDE** - Compatible with Arduino 1.8.19+
- **ESP32 Core** - Compatible with ESP32 Arduino Core 2.3.7+
- **Python** - Compatible with Python 3.8+
- **Libraries** - Compatible with ArduinoJson 6.x

## Support

### Getting Help
- **Documentation** - Check the comprehensive documentation
- **FAQ** - Review common questions and solutions
- **Community** - Engage with the ESP32 and laser enthusiast communities
- **Issues** - Report problems with specific error messages

### Contributing
- **Fork the Repository** - Create your own copy
- **Make Changes** - Implement your improvements
- **Test Thoroughly** - Verify all functionality
- **Document** - Update documentation as needed
- **Submit PR** - Share your changes with the community

---

**Note:** This is the initial release of GalvoLSUDriver. Future versions will build upon this foundation with additional features, improvements, and optimizations.