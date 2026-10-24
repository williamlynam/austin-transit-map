#include <FastLED.h>
#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <string.h>

#include "leds.h"

// One RMT channel sends the five chains one after another, switching its
// output pin between them. That lets it own all of the RMT's memory (512
// symbols instead of the 64 each chain got when FastLED ran five channels at
// once with Wi-Fi on). The interrupt that refills that memory then has about
// 320 us to run instead of about 40 us, so Wi-Fi interrupts can no longer
// make it late. A late refill sends stale bits, which is the likely cause of
// the flipped bit Will scoped on the last LED of the Red Line chain (0.1.10).
//
// FastLED is still used for its colour maths (correction, brightness and the
// power limit), so the LEDs look exactly as they did.

namespace {

const uint32_t RMT_HZ = 40000000;  // 25 ns ticks
// WS2812B bit timing, the same as FastLED's: 0 = 250 ns high, 1000 ns low;
// 1 = 875 ns high, 375 ns low.
rmt_symbol_word_t symbol(uint16_t highTicks, uint16_t lowTicks) {
  rmt_symbol_word_t s;
  s.level0 = 1;
  s.duration0 = highTicks;
  s.level1 = 0;
  s.duration1 = lowTicks;
  return s;
}

rmt_channel_handle_t chan = nullptr;
rmt_encoder_handle_t enc = nullptr;
int curPin = -1;
uint32_t maxMw = 0;

CRGB buf[board::NUM_LEDS];
uint8_t wire[3 * 64];  // one chain's GRB bytes; the longest chain is 44 LEDs

Rgb lastFrame[board::NUM_LEDS];
uint8_t lastBrightness = 0;
uint32_t lastShowMs = 0;
bool shown = false;

// Unchanged frames are not re-sent, except this often, which also repairs
// any pixel a disturbed transmission left wrong.
const uint32_t REFRESH_MS = 1000;

// A chain's data pin while the RMT is sending to another chain: a plain
// output held low (the pull-down covers the moment of switching).
void holdLow(int pin) {
  gpio_set_direction(gpio_num_t(pin), GPIO_MODE_OUTPUT);
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level(gpio_num_t(pin), 0);
}

void sendChain(const board::Chain &ch) {
  if (!chan) return;
  if (ch.gpio != curPin) {
    rmt_disable(chan);
    if (rmt_tx_switch_gpio(chan, gpio_num_t(ch.gpio), false) != ESP_OK) {
      log_e("RMT: can't switch to GPIO %u", ch.gpio);
      rmt_enable(chan);
      return;
    }
    if (curPin >= 0) holdLow(curPin);
    curPin = ch.gpio;
    rmt_enable(chan);
  }
  rmt_transmit_config_t tx = {};
  tx.loop_count = 0;
  tx.flags.eot_level = 0;
  rmt_transmit(chan, enc, wire, size_t(ch.count) * 3, &tx);
  rmt_tx_wait_all_done(chan, 50);
}

}  // namespace

void leds::begin(uint16_t maxMa) {
  setPowerLimit(maxMa);
  for (const board::Chain &ch : board::CHAINS) {
    gpio_pullup_dis(gpio_num_t(ch.gpio));
    gpio_pulldown_en(gpio_num_t(ch.gpio));
    holdLow(ch.gpio);
  }

  // As much RMT memory as the chip will give one channel.
  for (size_t symbols = 512; symbols >= 64 && !chan; symbols /= 2) {
    rmt_tx_channel_config_t cfg = {};
    cfg.gpio_num = gpio_num_t(board::CHAINS[0].gpio);
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.resolution_hz = RMT_HZ;
    cfg.mem_block_symbols = symbols;
    cfg.trans_queue_depth = 1;
    cfg.intr_priority = 3;  // the highest the driver allows
    if (rmt_new_tx_channel(&cfg, &chan) != ESP_OK) chan = nullptr;
    else Serial.printf("LEDs: one RMT channel, %u symbols of memory\n", unsigned(symbols));
  }
  if (!chan) {
    log_e("RMT: no TX channel; the LEDs stay dark");
    return;
  }
  curPin = board::CHAINS[0].gpio;

  rmt_bytes_encoder_config_t ec = {};
  ec.bit0 = symbol(10, 40);
  ec.bit1 = symbol(35, 15);
  ec.flags.msb_first = 1;
  if (rmt_new_bytes_encoder(&ec, &enc) != ESP_OK) {
    log_e("RMT: no encoder; the LEDs stay dark");
    rmt_del_channel(chan);
    chan = nullptr;
    return;
  }
  rmt_enable(chan);

  memset(wire, 0, sizeof wire);
  for (const board::Chain &ch : board::CHAINS) sendChain(ch);  // all off
}

void leds::setPowerLimit(uint16_t maxMa) { maxMw = 5UL * maxMa; }

// Most frames are identical (vehicles move once per feed poll), so they are
// only sent when something changed, plus the periodic refresh.
void leds::show(const Rgb frame[board::NUM_LEDS], uint8_t brightness) {
  uint32_t now = millis();
  bool same = shown && brightness == lastBrightness && memcmp(frame, lastFrame, sizeof lastFrame) == 0;
  if (same && now - lastShowMs < REFRESH_MS) return;
  memcpy(lastFrame, frame, sizeof lastFrame);
  lastBrightness = brightness;
  lastShowMs = now;
  shown = true;

  for (int i = 0; i < board::NUM_LEDS; i++) buf[i] = CRGB(frame[i].r, frame[i].g, frame[i].b);
  uint8_t b = maxMw ? calculate_max_brightness_for_power_mW(buf, board::NUM_LEDS, brightness, maxMw) : brightness;
  const CRGB adj = CRGB::computeAdjustment(b, CRGB(TypicalSMD5050), CRGB(UncorrectedTemperature));

  for (const board::Chain &ch : board::CHAINS) {
    uint8_t *w = wire;
    for (int i = 0; i < ch.count; i++) {
      const CRGB &c = buf[ch.offset + i];
      *w++ = scale8(c.g, adj.g);  // WS2812B byte order is G, R, B
      *w++ = scale8(c.r, adj.r);
      *w++ = scale8(c.b, adj.b);
    }
    sendChain(ch);
  }
}
