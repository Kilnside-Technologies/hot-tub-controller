#pragma once
#include "esphome.h"

// Raw capture of the Balboa display bus on CLK=GPIO35, DATA=GPIO34.
// Own interrupt (independent of kgstorm's component) so we can see, in ONE flash:
//   - clk/s   : clock rising edges per second  -> is the signal on the pin / ISR firing?
//   - frames  : completed frames (24-bit groups separated by a >3ms gap)
//   - bits    : bit count of the last frame (should be 24)
//   - raw/p1..p4 : the raw frame bits, to reverse-engineer the decode

#define RC_CLK  35
#define RC_DATA 34

volatile uint32_t rc_shift = 0;
volatile uint8_t  rc_bits = 0;
volatile uint32_t rc_edges = 0;
volatile uint32_t rc_last_us = 0;
volatile uint32_t rc_frame = 0;
volatile uint8_t  rc_frame_bits = 0;
volatile uint32_t rc_frames = 0;

void IRAM_ATTR rc_isr() {
  uint32_t now = micros();
  rc_edges++;
  // A gap longer than 3 ms marks the boundary between frames (intra-frame
  // clock gaps are ~21 us; the inter-frame gap is ~19 ms).
  if (now - rc_last_us > 3000) {
    if (rc_bits > 0) {
      rc_frame = rc_shift;
      rc_frame_bits = rc_bits;
      rc_frames++;
    }
    rc_shift = 0;
    rc_bits = 0;
  }
  rc_last_us = now;
  // Sample DATA (GPIO34) on this rising clock edge. Fast register read
  // (pins 32-39 live in GPIO_IN1_REG, bit = pin-32) — safe inside an ISR.
  uint32_t d = (REG_READ(GPIO_IN1_REG) >> (RC_DATA - 32)) & 0x1U;
  rc_shift = (rc_shift << 1) | d;
  if (rc_bits < 32) rc_bits++;
}

void rawcap_setup() {
  pinMode(RC_CLK, INPUT);
  pinMode(RC_DATA, INPUT);
  attachInterrupt(digitalPinToInterrupt(RC_CLK), rc_isr, RISING);
}

std::string rawcap_report() {
  static uint32_t last_edges = 0;
  noInterrupts();
  uint32_t edges  = rc_edges;
  uint32_t frame  = rc_frame & 0xFFFFFFU;
  uint8_t  fbits  = rc_frame_bits;
  uint32_t frames = rc_frames;
  interrupts();
  uint32_t rate = edges - last_edges;
  last_edges = edges;
  uint8_t p1 = (frame >> 17) & 0x7F;
  uint8_t p2 = (frame >> 10) & 0x7F;
  uint8_t p3 = (frame >> 3)  & 0x7F;
  uint8_t p4 = frame & 0x7;
  char buf[170];
  snprintf(buf, sizeof(buf),
           "clk/s=%u frames=%u bits=%u | raw=0x%06X p1=0x%02X p2=0x%02X p3=0x%02X p4=0x%X",
           (unsigned) rate, (unsigned) frames, (unsigned) fbits,
           (unsigned) frame, p1, p2, p3, p4);
  ESP_LOGI("rawcap", "%s", buf);
  return std::string(buf);
}
