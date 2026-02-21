# Serial Protocol API Reference

## Overview

The GalvoLSUDriver communicates via newline-delimited JSON (JSONL) over UART at 115200 baud. Each command is a single JSON object on one line, and responses are also JSON objects on separate lines.

## Command Structure

### Request Format
```json
{
  "cmd": "command_name",
  "path": "optional.path.to.variable",
  "value": "optional_value",
  "dots": ["optional", "array", "of", "dots"],
  "other": "optional_parameters"
}
```

### Response Format
```json
{
  "status": "success|error",
  "path": "optional.path",
  "value": "optional_value",
  "error": "optional_error_message",
  "telemetry": {"optional_telemetry_data"},
  "dots": ["optional", "array", "of", "dots"]
}
```

## Command Reference

### System Control

#### Arm/Disarm System
```json
// Request
{"cmd":"arm","value":true}

// Response (success)
{"status":"armed=true"}

// Response (error)
{"error":"system_already_armed"}
```

#### Get Status
```json
// Request
{"cmd":"status"}

// Response
{
  "status": {
    "armed": true,
    "fb_forward": true,
    "bd_cluster": 1,
    "sweep_us": 130,
    "dots_active": 50,
    "activeBuf": 0
  }
}
```

#### Get Telemetry
```json
// Request
{"cmd":"get","path":"*"}

// Response
{
  "telemetry": {
    "bd": {
      "shortEMA": 18,
      "longEMA": 128,
      "gapEMA": 512,
      "cluster": 1,
      "locked": true,
      "sweep_us": 130
    },
    "fb": {
      "v0": 2048,
      "v1": 2100,
      "slope": 52,
      "forward": true
    },
    "ttl": {
      "pixelWidth_us": 1,
      "extraOffset_us": 0,
      "minSpacing_us": 1,
      "armed": true
    },
    "dots": {
      "testPatternEnable": true,
      "testCount": 100,
      "activeBuf": 0,
      "dotCount": 50
    },
    "system": {
      "arm": true
    }
  }
}
```

### Configuration Commands

#### Set Variable
```json
// Request
{"cmd":"set","path":"ttl.pixelWidth_us","value":2}

// Response
{"status":"set_success","path":"ttl.pixelWidth_us"}

// Error Response
{"error":"unsupported_set_path"}
```

#### Get Variable
```json
// Request
{"cmd":"get","path":"ttl.pixelWidth_us"}

// Response
{"value":2}
```

### Dot Buffer Management

#### Upload Dots to Inactive Buffer
```json
// Request
{
  "cmd": "dots.inactive",
  "dots": [
    {"idxNorm": 0, "rgbMask": 1},
    {"idxNorm": 32768, "rgbMask": 2},
    {"idxNorm": 65535, "rgbMask": 4}
  ]
}

// Response
{
  "status": "dots_uploaded",
  "count": 3,
  "inactive": 1
}
```

#### Request Buffer Swap
```json
// Request
{"cmd":"dots.swap","value":true}

// Response
{"status":"swap_requested"}
```

## Data Structures

### Dot Format
```json
{
  "idxNorm": 0,      // Normalized position (0-65535)
  "rgbMask": 1       // RGB bitmask (bit0=R, bit1=G, bit2=B)
}
```

### RGB Mask Values
```
0b001 (1) - Red only
0b010 (2) - Green only
0b100 (4) - Blue only
0b011 (3) - Red + Green (Yellow)
0b101 (5) - Red + Blue (Magenta)
0b110 (6) - Green + Blue (Cyan)
0b111 (7) - Red + Green + Blue (White)
```

### Configuration Variables

#### Mirror Configuration
```json
{
  "mirror.targetFreq_hz": 2000.0,    // Target frequency in Hz
  "mirror.amplitude": 0.8,           // Amplitude (0.0-1.0)
  "mirror.sineSamples": 1024        // Samples per cycle
}
```

#### Beam Detection Configuration
```json
{
  "bd.shortWindow_us": 20,          // Short cluster threshold
  "bd.longWindow_us": 130,           // Long cluster threshold
  "bd.gapWindow_us": 500,            // Gap cluster threshold
  "bd.emaShift": 4,                  // EMA smoothing factor
  "bd.noSignalTimeout_ms": 1000      // BD loss timeout
}
```

#### Feedback Configuration
```json
{
  "fb.forwardSlopePositive": true,   // Slope polarity for forward
  "fb.slope_dt_us": 8,               // Time between ADC samples
  "fb.validThreshold": 0             // Slope validation threshold
}
```

#### TTL Configuration
```json
{
  "ttl.ttlFreq_hz": 1000000,         // Maximum pulse frequency
  "ttl.pixelWidth_us": 1,            // Laser pulse width
  "ttl.extraOffset_us": 0,           // Time offset for dot placement
  "ttl.minSpacing_us": 1             // Minimum spacing between pulses
}
```

#### Dot Buffer Configuration
```json
{
  "dots.testPatternEnable": true,    // Generate test pattern when empty
  "dots.testCount": 100,             // Number of test pattern dots
  "dots.activeBuf": 0,               // Currently active buffer
  "dots.swap": false                 // Swap request flag
}
```

#### System Configuration
```json
{
  "system.arm": false                // System armed state
}
```

## Error Handling

### Common Errors

#### JSON Parse Errors
```json
{"error":"JSON parse error: Unexpected token o in JSON at position 1"}
```

#### Unsupported Commands
```json
{"error":"unknown cmd"}
```

#### Invalid Paths
```json
{"error":"unsupported get path"}
{"error":"unsupported set path"}
```

#### Invalid Values
```json
{"error":"invalid value type"}
{"error":"value_out_of_range"}
```

#### System Errors
```json
{"error":"system_not_armed"}
{"error":"buffer_full"}
{"error":"timeout_waiting_for_JSON"}
```

### Error Response Format
```json
{
  "error": "error_type",
  "detail": "optional_detailed_message",
  "path": "optional_path_that_caused_error"
}
```

## Examples

### Complete Session

```json
// 1. Connect and get status
{"cmd":"status"}
{"status":{"armed":false,"fb_forward":true,"bd_cluster":2,"sweep_us":130,"dots_active":0,"activeBuf":0}}

// 2. Arm the system
{"cmd":"arm","value":true}
{"status":"armed=true"}

// 3. Get telemetry
{"cmd":"get","path":"*"}
{"telemetry":{"bd":{"shortEMA":18,"longEMA":128,"gapEMA":512,"cluster":1,"locked":true,"sweep_us":130},"fb":{"v0":2048,"v1":2100,"slope":52,"forward":true},"ttl":{"pixelWidth_us":1,"extraOffset_us":0,"minSpacing_us":1,"armed":true},"dots":{"testPatternEnable":true,"testCount":100,"activeBuf":0,"dotCount":0},"system":{"arm":true}}}

// 4. Upload dots
{"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, {"idxNorm":32768,"rgbMask":2}, {"idxNorm":65535,"rgbMask":4}]}
{"status":"dots_uploaded","count":3,"inactive":1}

// 5. Request buffer swap
{"cmd":"dots.swap","value":true}
{"status":"swap_requested"}

// 6. Verify swap
{"cmd":"get","path":"dots.activeBuf"}
{"value":1}
```

### Pattern Generation

```json
// Generate a line pattern
{
  "cmd": "dots.inactive",
  "dots": [
    {"idxNorm": 0, "rgbMask": 1},
    {"idxNorm": 8192, "rgbMask": 1},
    {"idxNorm": 16384, "rgbMask": 1},
    {"idxNorm": 24576, "rgbMask": 1},
    {"idxNorm": 32768, "rgbMask": 1},
    {"idxNorm": 40960, "rgbMask": 1},
    {"idxNorm": 49152, "rgbMask": 1},
    {"idxNorm": 57344, "rgbMask": 1},
    {"idxNorm": 65535, "rgbMask": 1}
  ]
}
```

### Color Patterns

```json
// Generate a color bar pattern
{
  "cmd": "dots.inactive",
  "dots": [
    {"idxNorm": 0, "rgbMask": 1},      // Red
    {"idxNorm": 8192, "rgbMask": 1},
    {"idxNorm": 16384, "rgbMask": 1},
    {"idxNorm": 24576, "rgbMask": 1},
    {"idxNorm": 32768, "rgbMask": 1},
    {"idxNorm": 40960, "rgbMask": 1},
    {"idxNorm": 49152, "rgbMask": 1},
    {"idxNorm": 57344, "rgbMask": 1},
    {"idxNorm": 65535, "rgbMask": 1},
    {"idxNorm": 0, "rgbMask": 2},      // Green
    {"idxNorm": 8192, "rgbMask": 2},
    {"idxNorm": 16384, "rgbMask": 2},
    {"idxNorm": 24576, "rgbMask": 2},
    {"idxNorm": 32768, "rgbMask": 2},
    {"idxNorm": 40960, "rgbMask": 2},
    {"idxNorm": 49152, "rgbMask": 2},
    {"idxNorm": 57344, "rgbMask": 2},
    {"idxNorm": 65535, "rgbMask": 2},
    {"idxNorm": 0, "rgbMask": 4},      // Blue
    {"idxNorm": 8192, "rgbMask": 4},
    {"idxNorm": 16384, "rgbMask": 4},
    {"idxNorm": 24576, "rgbMask": 4},
    {"idxNorm": 32768, "rgbMask": 4},
    {"idxNorm": 40960, "rgbMask": 4},
    {"idxNorm": 49152, "rgbMask": 4},
    {"idxNorm": 57344, "rgbMask": 4},
    {"idxNorm": 65535, "rgbMask": 4}
  ]
}
```

## Best Practices

### Command Sequencing

1. **Always check status** before making changes
2. **Arm system** before enabling laser output
3. **Upload dots** to inactive buffer first
4. **Request swap** after upload completes
5. **Monitor telemetry** for system health

### Error Handling

```python
def send_command(cmd):
    try:
        # Send command and wait for response
        response = client.send_json(cmd, wait_json=True, timeout_s=1.0)
        
        if 'error' in response:
            print(f"Error: {response['error']}")
            return None
            
        return response
        
    except Exception as e:
        print(f"Communication error: {e}")
        return None
```

### Performance Considerations

- **Batch commands** when possible
- **Use appropriate timeouts** for different operations
- **Monitor system load** via telemetry
- **Handle errors gracefully** to prevent system lockups

## Advanced Usage

### Custom Pattern Generation

```python
def generate_spiral_pattern(revolutions=3, points_per_revolution=100):
    dots = []
    total_points = revolutions * points_per_revolution
    
    for i in range(total_points):
        angle = 2 * 3.14159 * i / points_per_revolution
        radius = i / total_points
        idx_norm = int(radius * 65535)
        
        # Color based on angle
        if angle < 2 * 3.14159 / 3:
            rgb_mask = 1  # Red
        elif angle < 4 * 3.14159 / 3:
            rgb_mask = 2  # Green
        else:
            rgb_mask = 4  # Blue
            
        dots.append({"idxNorm": idx_norm, "rgbMask": rgb_mask})
        
        if len(dots) >= 1024:
            break
            
    return dots
```

### Real-time Control

```python
def live_pattern_update(client, pattern_generator):
    while True:
        # Generate new pattern
        dots = pattern_generator()
        
        # Upload to inactive buffer
        upload_response = client.send_json({
            "cmd": "dots.inactive",
            "dots": dots
        })
        
        # Request swap
        swap_response = client.send_json({
            "cmd": "dots.swap",
            "value": True
        })
        
        # Wait before next update
        time.sleep(0.1)
```

## Debugging

### Serial Monitor

Use a serial monitor to observe raw JSON traffic:
- **Baud rate:** 115200
- **Line ending:** Newline
- **Format:** JSON Lines

### Diagnostic Commands

```json
// Check all configuration variables
{"cmd":"get","path":"*"}

// Check system health
{"cmd":"status"}

// Check specific variable
{"cmd":"get","path":"ttl.pixelWidth_us"}
```

### Performance Monitoring

Monitor these telemetry values for performance issues:
- **`bd.locked`:** Beam detection lock status
- **`bd.cluster`:** Current beam detection cluster
- **`ttl.armed`:** Laser output status
- **`dots.dotCount`:** Current dot buffer usage

## Version Compatibility

### Protocol Version

The current protocol version is **1.0**. Future versions may add:
- New commands
- Additional configuration variables
- Enhanced telemetry data
- Improved error handling

### Backward Compatibility

- **Version 1.0:** Current stable version
- **Future versions:** Will maintain backward compatibility
- **Deprecated features:** Will be supported for at least one version cycle

## Support

For protocol-related questions:
- **Documentation:** Check this API reference
- **Examples:** Review the Python host application
- **Issues:** Report problems with specific error messages
- **Community:** Engage with the ESP32 and laser enthusiast communities

---

**Note:** This API reference is for version 1.0 of the GalvoLSUDriver protocol. Always check for updates and compatibility when using newer versions.