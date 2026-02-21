// resonant_mirror_controller.ino
// ESP32 (Arduino-ESP32 2.3.7) resonant mirror controller - fixed + RMT TTL
//
// Features implemented (compilable):
// - I2S DAC sine output on GPIO25 (DAC1) using I2S built-in DAC mode
// - Beam Detect interrupt on GPIO34 with dt clustering (short/long/gap) and EMA tracking
// - FB slope direction detection using ADC on GPIO35
// - RMT-based RGB TTL output (GPIO16/17/27) with 1us resolution
//   -> builds one RMT sequence per color per sweep window (no blocking waits)
// - Double-buffered dot buffers, swap during gap
// - Test pattern generation
// - Serial JSON protocol: get/set/status/arm/dots.inactive/dots.swap
//
// Notes:
// - This sketch assumes your "forward sweep window" corresponds to bd_cluster == 1 ("long").
// - The RMT program is rebuilt and transmitted each forward sweep.
// - Host should send dots sorted by idxNorm for best results.

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/rmt.h>
#include <esp_timer.h>
#include <ArduinoJson.h>
#include <math.h>

// ================== Pins ==================
#define PIN_DAC    25
#define PIN_BD     34
#define PIN_FB     35
#define PIN_RMT_R  16
#define PIN_RMT_G  17
#define PIN_RMT_B  27

// ================== Serial ==================
#define SERIAL_BAUD 115200

// ================== Mirror defaults ==================
static constexpr float MIRROR_DEFAULT_FREQ_HZ = 2000.0f;
static constexpr float MIRROR_DEFAULT_AMPLITUDE = 0.8f;
static constexpr int   MIRROR_DEFAULT_SINE_SAMPLES = 1024;

// ================== BD timing defaults ==================
static constexpr uint32_t BD_SHORT_WINDOW_US = 20;
static constexpr uint32_t BD_LONG_WINDOW_US  = 130;
static constexpr uint32_t BD_GAP_WINDOW_US   = 500;
static constexpr uint8_t  BD_EMA_SHIFT       = 4;
static constexpr uint32_t BD_NO_SIGNAL_TIMEOUT_MS = 1000;

// ================== FB defaults ==================
static constexpr uint32_t FB_SLOPE_DT_US = 8;
static constexpr bool FB_DEFAULT_FORWARD_SLOPE_POSITIVE = true;

// ================== TTL defaults ==================
static constexpr uint32_t TTL_DEFAULT_FREQ_HZ = 1000000; // used for minSpacing only (optional)
static constexpr uint32_t TTL_DEFAULT_PIXEL_WIDTH_US = 1;
static constexpr int32_t  TTL_DEFAULT_EXTRA_OFFSET_US = 0;

// ================== Dots ==================
#define DOT_CAP 1024
static constexpr int TEST_PATTERN_REPEAT = 5;
static const uint8_t testPatternMask[TEST_PATTERN_REPEAT] = {
  0b001, // R
  0b010, // G
  0b100, // B
  0b011, // Y
  0b110  // C
};

// ================== RMT ==================
// 80MHz / 80 = 1MHz => 1 tick = 1us
static constexpr uint8_t RMT_CLK_DIV = 80;
static constexpr int RMT_ITEMS_MAX = 256; // per channel; if too small, reduce dots or raise carefully

static const int rmt_gpio[3] = {PIN_RMT_R, PIN_RMT_G, PIN_RMT_B};
static const rmt_channel_t rmt_channels[3] = {RMT_CHANNEL_0, RMT_CHANNEL_1, RMT_CHANNEL_2};

// ================== Structs ==================
struct MirrorConfig {
  float targetFreq_hz;
  float amplitude;
  int sineSamples;
  int sampleRate_hz;
  int16_t *sineTable;
  size_t sineTableSize;
};

struct MirrorState {
  MirrorConfig config;
  bool isRunning;
  uint64_t lastFreqChangeTime_us;
};

struct BDTimingConfig {
  uint32_t shortWindow_us;
  uint32_t longWindow_us;
  uint32_t gapWindow_us;
  uint8_t emaShift;
  uint32_t noSignalTimeout_ms;
};

struct BDTimingState {
  BDTimingConfig config;
  uint32_t shortEMA;
  uint32_t longEMA;
  uint32_t gapEMA;
  bool isLocked;
  uint64_t lastEdge_us;
  uint64_t lastSignal_us;
  uint32_t filteredSweep_us; // estimate used to map idxNorm into time
};

struct FBConfig {
  bool forwardSlopePositive;
  uint32_t slope_dt_us;
  int validThreshold;
};

struct FBState {
  FBConfig config;
  int v0;
  int v1;
  int slope;
  bool forward;
};

struct TTLConfig {
  uint32_t ttlFreq_hz;
  uint32_t pixelWidth_us;
  int32_t extraOffset_us;
  uint32_t minSpacing_us;
};

struct TTLState {
  TTLConfig config;
  bool isArmed;
};

struct DotBufferConfig {
  uint16_t bufferSize;
  uint8_t activeBuffer;
  bool testPatternEnable;
  uint16_t testCount;
};

struct Dot {
  uint16_t idxNorm;  // 0..65535
  uint8_t rgbMask;   // bit0=R, bit1=G, bit2=B
};

struct DotBufferState {
  DotBufferConfig config;
  uint16_t dotCount[2];
  bool swapRequested;
  Dot buffer[2][DOT_CAP];
};

struct SystemState {
  bool arm;
};

// ================== Globals ==================
static MirrorState mirrorState{};
static BDTimingState bdTimingState{};
static FBState fbState{};
static TTLState ttlState{};
static DotBufferState dotBufferState{};
static SystemState systemState{};

// BD ISR communication
static volatile bool bd_edge = false;
static volatile uint64_t bd_timestamp_us = 0;
static volatile uint8_t bd_cluster = 0; // 0=short,1=long,2=gap

static volatile bool fb_forward = true;

// RMT item buffers (static to avoid stack use)
static rmt_item32_t itemsR[RMT_ITEMS_MAX];
static rmt_item32_t itemsG[RMT_ITEMS_MAX];
static rmt_item32_t itemsB[RMT_ITEMS_MAX];

// ================== Prototypes ==================
static void initAll();

static void generateSineTable();
static void setupI2S();
static void mirrorPumpOnce();

static void setupRMT();
static void clearRMTChannels();

static void setupBDInterrupt();
static void IRAM_ATTR bdISR();
static void resetBDTiming();
static void processBDEdge(uint64_t ts_us);
static void updateBDCluster(uint32_t dt_us);

static void setupFB();
static bool detectFBSlope();

static void initDots();
static void generateTestPatternToInactive();
static uint16_t getActiveDotCount();
static void swapDotBuffersIfSafe();

static void armSystem();
static void disarmSystem();

static void processForwardWindow(uint64_t windowStart_us);

static void rtTask(void *parameter);

static void handleSerial();
static void processJSONCommand(const String &cmd);
static void sendStatus();
static void sendTelemetry();

// ================== Utility: RMT items ==================
static inline rmt_item32_t mkItem(uint16_t d0, uint8_t l0, uint16_t d1, uint8_t l1) {
  rmt_item32_t it;
  it.level0 = l0; it.duration0 = d0;
  it.level1 = l1; it.duration1 = d1;
  return it;
}

// Append LOW for low_us (split if needed)
static int appendLow(rmt_item32_t *items, int n, uint32_t low_us) {
  while (low_us && n < RMT_ITEMS_MAX) {
    uint16_t chunk = (low_us > 32767) ? 32767 : (uint16_t)low_us;
    // Encode as low for chunk, then low for 1 tick (avoid 0 duration)
    items[n++] = mkItem(chunk, 0, 1, 0);
    low_us -= chunk;
  }
  return n;
}

// Append HIGH pulse for width_us, then LOW separator 1us
static int appendPulse(rmt_item32_t *items, int n, uint32_t width_us) {
  if (n >= RMT_ITEMS_MAX) return n;
  if (width_us == 0) width_us = 1;
  uint16_t w = (width_us > 32767) ? 32767 : (uint16_t)width_us;
  items[n++] = mkItem(w, 1, 1, 0);
  return n;
}

// Build items for one channel from dot list
static int buildChannelItems(rmt_item32_t *items,
                             const Dot *dots, uint16_t count,
                             uint8_t channelBit,
                             uint32_t sweep_us,
                             uint32_t pixelWidth_us,
                             int32_t extraOffset_us,
                             uint32_t minSpacing_us)
{
  int n = 0;
  uint32_t t = 0;
  uint32_t lastStart = 0;

  for (uint16_t i = 0; i < count; i++) {
    if (!(dots[i].rgbMask & channelBit)) continue;

    uint32_t offset_us = (uint32_t)((uint64_t)dots[i].idxNorm * (uint64_t)(sweep_us - 1) / 65535ULL);
    int32_t off = (int32_t)offset_us + extraOffset_us;
    if (off < 0) off = 0;
    if (off > (int32_t)(sweep_us - 1)) off = (int32_t)(sweep_us - 1);

    uint32_t start_us = (uint32_t)off;

    // Enforce min spacing (optional)
    if (minSpacing_us && (start_us - lastStart) < minSpacing_us && start_us >= lastStart) {
      continue;
    }

    // Prevent overlap in time encoding
    if (start_us < t) start_us = t;

    uint32_t gap = start_us - t;
    n = appendLow(items, n, gap);
    if (n >= RMT_ITEMS_MAX) break;

    n = appendPulse(items, n, pixelWidth_us);
    if (n >= RMT_ITEMS_MAX) break;

    lastStart = start_us;
    t = start_us + pixelWidth_us + 1; // +1us separator used in appendPulse
    if (t >= sweep_us) break;
  }

  if (t < sweep_us && n < RMT_ITEMS_MAX) {
    n = appendLow(items, n, sweep_us - t);
  }

  return n;
}

// ================== Setup/Loop ==================
void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) { delay(10); }

  initAll();

  xTaskCreatePinnedToCore(rtTask, "RTTask", 8192, nullptr, 2, nullptr, 1);

  Serial.println("{\"status\":\"boot_ok\"}");
}

void loop() {
  handleSerial();
}

// ================== Init ==================
static void initAll() {
  // System
  systemState.arm = false;

  // Mirror
  mirrorState.config.targetFreq_hz = MIRROR_DEFAULT_FREQ_HZ;
  mirrorState.config.amplitude = MIRROR_DEFAULT_AMPLITUDE;
  mirrorState.config.sineSamples = MIRROR_DEFAULT_SINE_SAMPLES;
  mirrorState.config.sampleRate_hz = (int)(mirrorState.config.targetFreq_hz * mirrorState.config.sineSamples);
  mirrorState.config.sineTable = nullptr;
  mirrorState.config.sineTableSize = 0;
  mirrorState.isRunning = false;

  generateSineTable();
  setupI2S();
  mirrorState.isRunning = true;

  // BD timing
  bdTimingState.config.shortWindow_us = BD_SHORT_WINDOW_US;
  bdTimingState.config.longWindow_us  = BD_LONG_WINDOW_US;
  bdTimingState.config.gapWindow_us   = BD_GAP_WINDOW_US;
  bdTimingState.config.emaShift       = BD_EMA_SHIFT;
  bdTimingState.config.noSignalTimeout_ms = BD_NO_SIGNAL_TIMEOUT_MS;
  resetBDTiming();

  // FB
  fbState.config.forwardSlopePositive = FB_DEFAULT_FORWARD_SLOPE_POSITIVE;
  fbState.config.slope_dt_us = FB_SLOPE_DT_US;
  fbState.config.validThreshold = 0;
  setupFB();
  detectFBSlope();

  // TTL
  ttlState.config.ttlFreq_hz = TTL_DEFAULT_FREQ_HZ;
  ttlState.config.pixelWidth_us = TTL_DEFAULT_PIXEL_WIDTH_US;
  ttlState.config.extraOffset_us = TTL_DEFAULT_EXTRA_OFFSET_US;
  ttlState.config.minSpacing_us = (ttlState.config.ttlFreq_hz ? (1000000UL / ttlState.config.ttlFreq_hz) : 0);
  ttlState.isArmed = false;

  setupRMT();
  clearRMTChannels();

  // Dots
  initDots();
  generateTestPatternToInactive();
  dotBufferState.swapRequested = true; // swap at first gap

  // BD interrupt
  setupBDInterrupt();
}

// ================== Mirror/I2S ==================
static void generateSineTable() {
  auto &cfg = mirrorState.config;
  if (cfg.sineTable) {
    free(cfg.sineTable);
    cfg.sineTable = nullptr;
  }
  cfg.sineTableSize = (size_t)cfg.sineSamples;
  cfg.sineTable = (int16_t*)malloc(cfg.sineTableSize * sizeof(int16_t));
  for (size_t i = 0; i < cfg.sineTableSize; i++) {
    float angle = (float)i * (2.0f * PI) / (float)cfg.sineTableSize;
    float s = sinf(angle) * cfg.amplitude;
    cfg.sineTable[i] = (int16_t)(s * 32767.0f);
  }
}

static void setupI2S() {
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = mirrorState.config.sampleRate_hz;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 512;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, NULL);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN); // GPIO25
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// Keep DAC fed
static void mirrorPumpOnce() {
  if (!mirrorState.isRunning || !mirrorState.config.sineTable) return;
  size_t bytes_written = 0;
  i2s_write(I2S_NUM_0,
            mirrorState.config.sineTable,
            mirrorState.config.sineTableSize * sizeof(int16_t),
            &bytes_written,
            0);
}

// ================== RMT ==================
static void setupRMT() {
  for (int i = 0; i < 3; i++) {
    rmt_config_t c = RMT_DEFAULT_CONFIG_TX((gpio_num_t)rmt_gpio[i], rmt_channels[i]);
    c.clk_div = RMT_CLK_DIV; // 1us tick
    c.tx_config.loop_en = false;
    c.tx_config.carrier_en = false;
    c.tx_config.idle_output_en = true;
    c.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

    rmt_config(&c);
    rmt_driver_install(rmt_channels[i], 0, 0);
  }
}

static void clearRMTChannels() {
  for (int i = 0; i < 3; i++) {
    rmt_set_idle_level(rmt_channels[i], true, RMT_IDLE_LEVEL_LOW);
  }
}

// ================== BD Interrupt/Timing ==================
static void setupBDInterrupt() {
  pinMode(PIN_BD, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BD), bdISR, RISING);
}

static void IRAM_ATTR bdISR() {
  bd_timestamp_us = (uint64_t)esp_timer_get_time();
  bd_edge = true;
}

static void resetBDTiming() {
  bdTimingState.shortEMA = BD_SHORT_WINDOW_US;
  bdTimingState.longEMA  = BD_LONG_WINDOW_US;
  bdTimingState.gapEMA   = BD_GAP_WINDOW_US;
  bdTimingState.isLocked = false;
  bdTimingState.lastEdge_us = 0;
  bdTimingState.lastSignal_us = (uint64_t)esp_timer_get_time();
  bdTimingState.filteredSweep_us = BD_LONG_WINDOW_US;
  bd_cluster = 0;
}

static void processBDEdge(uint64_t ts_us) {
  bdTimingState.lastSignal_us = ts_us;

  if (bdTimingState.lastEdge_us != 0) {
    uint32_t dt = (uint32_t)(ts_us - bdTimingState.lastEdge_us);
    updateBDCluster(dt);
  }
  bdTimingState.lastEdge_us = ts_us;
}

static void updateBDCluster(uint32_t dt) {
  auto &st = bdTimingState;
  auto &cfg = st.config;

  if (!st.isLocked) {
    if (dt < cfg.shortWindow_us * 2) st.shortEMA = (st.shortEMA + dt) >> 1;
    else if (dt < cfg.longWindow_us * 2) st.longEMA = (st.longEMA + dt) >> 1;
    else st.gapEMA = (st.gapEMA + dt) >> 1;

    if (st.shortEMA && st.longEMA && st.gapEMA) st.isLocked = true;
    return;
  }

  uint32_t shortEMA = st.shortEMA;
  uint32_t longEMA  = st.longEMA;
  uint32_t gapEMA   = st.gapEMA;

  if (dt < shortEMA + (shortEMA >> 2)) {
    st.shortEMA = st.shortEMA + ((int32_t)dt - (int32_t)st.shortEMA) / (1 << cfg.emaShift);
    bd_cluster = 0;
  } else if (dt < longEMA + (longEMA >> 2)) {
    st.longEMA = st.longEMA + ((int32_t)dt - (int32_t)st.longEMA) / (1 << cfg.emaShift);
    bd_cluster = 1;
    st.filteredSweep_us = st.longEMA;
  } else {
    st.gapEMA = st.gapEMA + ((int32_t)dt - (int32_t)st.gapEMA) / (1 << cfg.emaShift);
    bd_cluster = 2;
  }
}

// ================== FB ==================
static void setupFB() {
  pinMode(PIN_FB, INPUT);
  analogReadResolution(12);
}

static bool detectFBSlope() {
  int v0 = analogRead(PIN_FB);
  delayMicroseconds(fbState.config.slope_dt_us);
  int v1 = analogRead(PIN_FB);

  fbState.v0 = v0;
  fbState.v1 = v1;
  fbState.slope = v1 - v0;

  bool forward = fbState.config.forwardSlopePositive ? (fbState.slope > 0) : (fbState.slope < 0);
  fbState.forward = forward;
  fb_forward = forward;
  return forward;
}

// ================== Dots ==================
static void initDots() {
  dotBufferState.config.bufferSize = DOT_CAP;
  dotBufferState.config.activeBuffer = 0;
  dotBufferState.config.testPatternEnable = true;
  dotBufferState.config.testCount = 100;

  dotBufferState.dotCount[0] = 0;
  dotBufferState.dotCount[1] = 0;
  dotBufferState.swapRequested = false;
}

static void generateTestPatternToInactive() {
  uint8_t inactive = dotBufferState.config.activeBuffer ^ 1;
  uint16_t n = min<uint16_t>(dotBufferState.config.testCount, DOT_CAP);
  if (n < 2) n = 2;

  for (uint16_t i = 0; i < n; i++) {
    Dot d;
    d.idxNorm = (uint16_t)((uint32_t)i * 65535UL / (uint32_t)(n - 1));
    d.rgbMask = testPatternMask[i % TEST_PATTERN_REPEAT];
    dotBufferState.buffer[inactive][i] = d;
  }
  dotBufferState.dotCount[inactive] = n;
}

static uint16_t getActiveDotCount() {
  return dotBufferState.dotCount[dotBufferState.config.activeBuffer];
}

static void swapDotBuffersIfSafe() {
  if (dotBufferState.swapRequested && bd_cluster == 2) {
    dotBufferState.config.activeBuffer ^= 1;
    dotBufferState.swapRequested = false;
  }
}

// ================== System arm ==================
static void armSystem() {
  systemState.arm = true;
  ttlState.isArmed = true;
}

static void disarmSystem() {
  systemState.arm = false;
  ttlState.isArmed = false;
  clearRMTChannels();
}

// ================== Forward window processing (RMT fire) ==================
static void processForwardWindow(uint64_t /*windowStart_us*/) {
  if (!systemState.arm || !ttlState.isArmed) return;
  if (!fb_forward) return;

  uint8_t ab = dotBufferState.config.activeBuffer;
  uint16_t count = dotBufferState.dotCount[ab];
  if (count == 0) {
    if (dotBufferState.config.testPatternEnable) {
      generateTestPatternToInactive();
      dotBufferState.swapRequested = true;
    }
    return;
  }

  uint32_t sweep_us = bdTimingState.filteredSweep_us;
  // Your target is ~200us sweep; keep a sane fallback
  if (sweep_us < 50 || sweep_us > 2000) sweep_us = 200;

  const Dot *dots = dotBufferState.buffer[ab];

  int nR = buildChannelItems(itemsR, dots, count, 0b001, sweep_us,
                            ttlState.config.pixelWidth_us, ttlState.config.extraOffset_us,
                            ttlState.config.minSpacing_us);
  int nG = buildChannelItems(itemsG, dots, count, 0b010, sweep_us,
                            ttlState.config.pixelWidth_us, ttlState.config.extraOffset_us,
                            ttlState.config.minSpacing_us);
  int nB = buildChannelItems(itemsB, dots, count, 0b100, sweep_us,
                            ttlState.config.pixelWidth_us, ttlState.config.extraOffset_us,
                            ttlState.config.minSpacing_us);

  // Fire without waits (non-blocking). If a channel is still transmitting from last time,
  // this will fail/take effect late. For 20kHz, keep sequences short and consider fewer dots.
  if (nR > 0) rmt_write_items(rmt_channels[0], itemsR, nR, false);
  if (nG > 0) rmt_write_items(rmt_channels[1], itemsG, nG, false);
  if (nB > 0) rmt_write_items(rmt_channels[2], itemsB, nB, false);
}

// ================== RT task ==================
static void rtTask(void *parameter) {
  (void)parameter;

  uint64_t lastForwardFireEdge = 0;

  for (;;) {
    mirrorPumpOnce();

    if (bd_edge) {
      uint64_t ts = bd_timestamp_us;
      bd_edge = false;

      processBDEdge(ts);

      // Swap buffers in gap
      if (bd_cluster == 2) {
        swapDotBuffersIfSafe();
      }

      // Fire once per long cluster edge (assumed forward window start)
      if (bd_cluster == 1 && ts != lastForwardFireEdge) {
        lastForwardFireEdge = ts;
        detectFBSlope();
        processForwardWindow(ts);
      }
    }

    // crude no-signal recovery (optional): reset if BD absent
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (now - bdTimingState.lastSignal_us > (uint64_t)bdTimingState.config.noSignalTimeout_ms * 1000ULL) {
      resetBDTiming();
      bdTimingState.lastSignal_us = now;
    }

    vTaskDelay(1);
  }
}

// ================== Telemetry ==================
static void sendStatus() {
  Serial.printf("{\"status\":{\"armed\":%s,\"fb_forward\":%s,\"bd_cluster\":%u,\"sweep_us\":%u,\"dots_active\":%u,\"activeBuf\":%u}}\n",
                systemState.arm ? "true" : "false",
                fb_forward ? "true" : "false",
                (unsigned)bd_cluster,
                (unsigned)bdTimingState.filteredSweep_us,
                (unsigned)getActiveDotCount(),
                (unsigned)dotBufferState.config.activeBuffer);
}

static void sendTelemetry() {
  Serial.printf("{\"telemetry\":{\"bd\":{\"shortEMA\":%u,\"longEMA\":%u,\"gapEMA\":%u,\"cluster\":%u,\"locked\":%s,\"sweep_us\":%u},",
                bdTimingState.shortEMA, bdTimingState.longEMA, bdTimingState.gapEMA,
                (unsigned)bd_cluster, bdTimingState.isLocked ? "true" : "false",
                (unsigned)bdTimingState.filteredSweep_us);
  Serial.printf("\"fb\":{\"v0\":%d,\"v1\":%d,\"slope\":%d,\"forward\":%s},",
                fbState.v0, fbState.v1, fbState.slope, fb_forward ? "true" : "false");
  Serial.printf("\"ttl\":{\"pixelWidth_us\":%u,\"extraOffset_us\":%d,\"minSpacing_us\":%u,\"armed\":%s},",
                (unsigned)ttlState.config.pixelWidth_us,
                (int)ttlState.config.extraOffset_us,
                (unsigned)ttlState.config.minSpacing_us,
                ttlState.isArmed ? "true" : "false");
  Serial.printf("\"dots\":{\"testPatternEnable\":%s,\"testCount\":%u,\"activeBuf\":%u,\"dotCount\":%u},",
                dotBufferState.config.testPatternEnable ? "true" : "false",
                (unsigned)dotBufferState.config.testCount,
                (unsigned)dotBufferState.config.activeBuffer,
                (unsigned)getActiveDotCount());
  Serial.printf("\"system\":{\"arm\":%s}}}\n", systemState.arm ? "true" : "false");
}

// ================== JSON commands ==================
// Supported:
// {"cmd":"get","path":"*"} -> telemetry
// {"cmd":"status"}         -> status
// {"cmd":"arm","value":true/false}
// {"cmd":"set","path":"ttl.pixelWidth_us","value":1}
// {"cmd":"set","path":"ttl.extraOffset_us","value":0}
// {"cmd":"set","path":"dots.testPatternEnable","value":true}
// {"cmd":"set","path":"dots.testCount","value":100}
// {"cmd":"dots.inactive","dots":[{"idxNorm":0,"rgbMask":1}, ...]}
// {"cmd":"dots.swap","value":true}
static void processJSONCommand(const String &cmd) {
  DynamicJsonDocument doc(4096);
  auto err = deserializeJson(doc, cmd);
  if (err) {
    Serial.printf("{\"error\":\"JSON parse error: %s\"}\n", err.c_str());
    return;
  }

  const char *type = doc["cmd"] | "";
  if (!strcmp(type, "get")) {
    const char *path = doc["path"] | "*";
    if (!strcmp(path, "*")) sendTelemetry();
    else if (!strcmp(path, "status")) sendStatus();
    else Serial.println("{\"error\":\"unsupported get path\"}");
    return;
  }

  if (!strcmp(type, "status")) {
    sendStatus();
    return;
  }

  if (!strcmp(type, "arm")) {
    bool v = doc["value"] | false;
    if (v) armSystem();
    else disarmSystem();
    Serial.printf("{\"status\":\"armed=%s\"}\n", v ? "true" : "false");
    return;
  }

  if (!strcmp(type, "set")) {
    const char *path = doc["path"] | "";
    if (!strcmp(path, "ttl.pixelWidth_us")) {
      ttlState.config.pixelWidth_us = doc["value"].as<uint32_t>();
    } else if (!strcmp(path, "ttl.extraOffset_us")) {
      ttlState.config.extraOffset_us = doc["value"].as<int32_t>();
    } else if (!strcmp(path, "ttl.ttlFreq_hz")) {
      ttlState.config.ttlFreq_hz = doc["value"].as<uint32_t>();
      ttlState.config.minSpacing_us = (ttlState.config.ttlFreq_hz ? (1000000UL / ttlState.config.ttlFreq_hz) : 0);
    } else if (!strcmp(path, "dots.testPatternEnable")) {
      dotBufferState.config.testPatternEnable = doc["value"].as<bool>();
    } else if (!strcmp(path, "dots.testCount")) {
      dotBufferState.config.testCount = doc["value"].as<uint16_t>();
    } else {
      Serial.println("{\"error\":\"unsupported set path\"}");
      return;
    }
    Serial.printf("{\"status\":\"set_success\",\"path\":\"%s\"}\n", path);
    return;
  }

  if (!strcmp(type, "dots.inactive")) {
    JsonArray arr = doc["dots"].as<JsonArray>();
    uint8_t inactive = dotBufferState.config.activeBuffer ^ 1;

    uint16_t n = 0;
    for (JsonVariant v : arr) {
      if (n >= DOT_CAP) break;
      dotBufferState.buffer[inactive][n].idxNorm = v["idxNorm"] | 0;
      dotBufferState.buffer[inactive][n].rgbMask = v["rgbMask"] | 0;
      n++;
    }
    dotBufferState.dotCount[inactive] = n;
    Serial.printf("{\"status\":\"dots_uploaded\",\"count\":%u,\"inactive\":%u}\n", (unsigned)n, (unsigned)inactive);
    return;
  }

  if (!strcmp(type, "dots.swap")) {
    dotBufferState.swapRequested = doc["value"] | false;
    Serial.println("{\"status\":\"swap_requested\"}");
    return;
  }

  Serial.println("{\"error\":\"unknown cmd\"}");
}

// ================== Serial handler ==================
static void handleSerial() {
  static String rx;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      String line = rx;
      rx = "";
      line.trim();
      if (line.length()) processJSONCommand(line);
    } else {
      rx += c;
    }
  }
}