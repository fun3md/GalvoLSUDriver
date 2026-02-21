# Frequently Asked Questions

## General Questions

### What is GalvoLSUDriver?
GalvoLSUDriver is a high-performance laser scanning system built around an ESP32 D1 Mini that controls a resonant MEMS mirror and RGB laser output. It provides precise timing control for laser dot patterns, making it ideal for laser displays, 3D scanning, or material processing applications.

### What hardware do I need?
- **ESP32 D1 Mini (WROOM-32)** - Main controller
- **Resonant MEMS Mirror** - Oscillating mirror for beam steering
- **RGB Lasers** - Three laser diodes (R, G, B channels)
- **Laser Drivers** - 3.3V logic-compatible drivers
- **Power Supply** - Stable 5V/3.3V for ESP32 and lasers

### What software do I need?
- **Arduino IDE** with ESP32 board support
- **Python 3.8+** for the host application
- **ArduinoJson** library for the ESP32 firmware
- **pyserial, gradio, pillow, numpy** for the Python GUI

## Hardware Setup

### How do I connect the components?
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

### What power supply do I need?
- **ESP32:** 5V USB power (minimum 500mA)
- **Lasers:** Separate 5V supply with current limiting (depends on laser power)
- **Total:** Ensure adequate power for all components

### Do I need signal conditioning for the FB input?
Yes, the FB signal may require amplification and conditioning. The FB signal is typically a low-voltage analog signal from the mirror's feedback mechanism. You may need to:
- Add a voltage amplifier
- Include filtering for noise reduction
- Provide proper biasing for the ADC input

## Software Installation

### How do I install the ESP32 board support?
1. Open Arduino IDE
2. Go to `File > Preferences`
3. Add `https://dl.espressif.com/dl/package_esp32_index.json` to Additional Boards Manager URLs
4. Go to `Tools > Board > Boards Manager`
5. Search for "ESP32" and install "esp32 by Espressif Systems"

### How do I install the required libraries?
1. Open Library Manager: `Sketch > Include Library > Manage Libraries`
2. Search for "ArduinoJson" by Benoit Blanchon
3. Click Install
4. Ensure ESP32 Arduino Core 2.3.7+ is installed

### How do I run the Python host application?
```bash
# Install dependencies
pip install pyserial gradio pillow numpy

# Run the application
python mirror_controller.py
```

## Operation

### How do I connect to the system?
1. **Using Python GUI:**
   - Select the correct COM port
   - Click "Connect"
   - Monitor connection status

2. **Using Serial Monitor:**
   - Baud rate: 115200
   - Line ending: Newline
   - Send JSON commands

### How do I arm the system?
```json
{"cmd":"arm","value":true}
```

### How do I upload a dot pattern?
1. **Generate pattern** using the Python GUI or custom code
2. **Upload to inactive buffer:**
   ```json
   {"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, ...]}
   ```
3. **Request buffer swap:**
   ```json
   {"cmd":"dots.swap","value":true}
   ```

### How do I create custom patterns?
You can create custom patterns by:
- **Using the Python GUI:** Generate patterns with built-in tools
- **Writing custom code:** Create patterns programmatically
- **Loading images:** Convert images to dot patterns
- **Real-time generation:** Create patterns on-the-fly

## Troubleshooting

### I'm not getting any laser output. What should I check?
1. **System arming:** Ensure the system is armed (`{"cmd":"arm","value":true}`)
2. **Laser drivers:** Verify laser drivers are receiving signals
3. **TTL signals:** Check RMT output with oscilloscope
4. **Power supply:** Ensure adequate power for lasers
5. **Safety:** Check laser safety interlocks

### I'm getting JSON parse errors. What's wrong?
1. **Syntax:** Ensure valid JSON format
2. **Line endings:** Use newline as line ending
3. **Encoding:** Use UTF-8 encoding
4. **Command format:** Follow the specified command structure

### The system isn't detecting BD edges. What should I check?
1. **Signal levels:** Verify BD signal amplitude
2. **Wiring:** Check BD input connections
3. **Signal conditioning:** Add amplification if needed
4. **Interrupts:** Verify interrupt configuration
5. **Timing:** Check for signal noise or interference

### The FB direction detection isn't working. What's wrong?
1. **Signal quality:** Check FB signal amplitude and noise
2. **ADC settings:** Verify ADC configuration
3. **Slope detection:** Adjust slope detection parameters
4. **Signal conditioning:** Add amplification and filtering
5. **Timing:** Check sampling rate and timing

### The patterns are distorted or incorrect. What should I check?
1. **Mirror frequency:** Verify mirror oscillation frequency
2. **Timing:** Check RMT timing and synchronization
3. **Dot mapping:** Verify dot position calculations
4. **Buffer management:** Check double-buffering operation
5. **Signal quality:** Verify all input signals

### The system is unstable or crashing. What should I do?
1. **Power supply:** Check for power fluctuations
2. **Memory usage:** Monitor memory consumption
3. **Interrupts:** Check for interrupt conflicts
4. **Timing:** Verify real-time task priorities
5. **Serial communication:** Check for buffer overflows

## Performance

### What is the maximum dot rate?
The maximum dot rate depends on:
- **Mirror frequency:** Higher frequency allows more dots
- **Minimum spacing:** Set by `ttl.ttlFreq_hz` (1MHz default = 1μs spacing)
- **Sweep duration:** Longer sweeps allow more dots
- **Buffer capacity:** Limited to 1024 dots per buffer

### How do I optimize performance?
1. **Increase mirror frequency:** Higher frequency for more dots
2. **Adjust minimum spacing:** Balance between density and quality
3. **Optimize patterns:** Use efficient dot arrangements
4. **Monitor telemetry:** Check system performance metrics
5. **Update firmware:** Use latest performance improvements

### What are the timing constraints?
- **Mirror drive:** Sub-microsecond stability required
- **BD processing:** Interrupt latency < 10μs
- **Laser output:** 1μs resolution, 1MHz maximum
- **Real-time task:** Core 1 dedicated to timing-critical operations

## Safety

### What safety precautions should I take?
1. **Laser safety glasses:** Always wear appropriate protection
2. **Beam blocks:** Use when not actively scanning
3. **Enclosure:** Use proper laser enclosure
4. **Power control:** Have emergency stop procedures
5. **Training:** Understand laser safety principles

### What are the laser safety classifications?
The system can be configured for different laser classes:
- **Class 1:** Safe under all conditions
- **Class 2:** Safe for accidental exposure
- **Class 3R:** Low risk, but avoid direct viewing
- **Class 3B:** Hazardous for direct viewing
- **Class 4:** High power, requires extreme caution

### How do I ensure electrical safety?
1. **Proper grounding:** Ensure good electrical grounding
2. **Current limiting:** Use appropriate current limiting
3. **Isolation:** Provide proper isolation between circuits
4. **Fusing:** Use appropriate fuses for protection
5. **Testing:** Verify all safety interlocks

## Development

### How do I modify the firmware?
1. **Understand the architecture:** Review `docs/ARCHITECTURE.md`
2. **Modify code:** Make changes to `GalvoLSUDriver.ino`
3. **Test thoroughly:** Verify all functionality
4. **Update documentation:** Document your changes
5. **Contribute:** Share improvements with the community

### How do I add new features?
1. **Plan the feature:** Define requirements and design
2. **Implement:** Add code following existing patterns
3. **Test:** Verify functionality and performance
4. **Document:** Update API reference and documentation
5. **Share:** Contribute to the project

### How do I contribute to the project?
1. **Fork the repository:** Create your own copy
2. **Make changes:** Implement your improvements
3. **Test:** Verify all functionality works
4. **Document:** Update documentation as needed
5. **Submit PR:** Share your changes with the community

## Advanced Topics

### How do I implement real-time pattern updates?
1. **Use double-buffering:** Upload to inactive buffer
2. **Request swap:** Trigger buffer swap during gap
3. **Monitor telemetry:** Check system status
4. **Handle errors:** Implement error recovery
5. **Optimize timing:** Minimize update latency

### How do I integrate with other systems?
1. **Serial communication:** Use the JSON protocol
2. **Network control:** Add WiFi/Ethernet interface
3. **API integration:** Create REST or WebSocket API
4. **Hardware interface:** Add additional sensors or actuators
5. **Software integration:** Connect to existing systems

### How do I optimize for specific applications?
1. **Display applications:** Focus on pattern quality and refresh rate
2. **3D scanning:** Optimize for accuracy and resolution
3. **Material processing:** Focus on power and precision
4. **Scientific applications:** Optimize for measurement accuracy
5. **Art installations:** Focus on creative expression and effects

## Common Error Messages

### JSON Parse Errors
```json
{"error":"JSON parse error: Unexpected token o in JSON at position 1"}
```
**Solution:** Check JSON syntax and formatting

### Unsupported Commands
```json
{"error":"unknown cmd"}
```
**Solution:** Verify command name and parameters

### Invalid Paths
```json
{"error":"unsupported get path"}
{"error":"unsupported set path"}
```
**Solution:** Check variable path and spelling

### System Errors
```json
{"error":"system_not_armed"}
{"error":"buffer_full"}
{"error":"timeout_waiting_for_JSON"}
```
**Solution:** Check system state and communication

## Version Information

### Current Version
- **Firmware:** 1.0
- **Protocol:** 1.0
- **Python GUI:** 1.0

### Compatibility
- **Backward compatible:** Version 1.0 maintains compatibility
- **Future versions:** Will maintain backward compatibility
- **Deprecated features:** Supported for at least one version cycle

### Updates
- **Check for updates:** Regularly check for new versions
- **Update procedure:** Follow standard update procedures
- **Compatibility:** Verify compatibility before updating

---

**Need more help?** Check the detailed documentation in the `docs/` folder, review the complete specification in `plans/spec.md`, or engage with the community for support.