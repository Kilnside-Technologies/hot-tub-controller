#pragma once
#include "esphome.h"

// Raw capture of the Balboa display bus on CLK=GPIO35, DATA=GPIO34.
// Lean IRAM ISR (CPU-cycle timing, no micros()) so it doesn't drop edges under
// WiFi. Logs every DISTINCT 24-bit frame (tag "frame") so brief status flashes
// are caught, plus a clk/s rate summary (tag "rawcap").

#define RC_CLK  35
#define RC_DATA 34
#define RC_RING 64            // distinct-frame ring (power of 2)

volatile uint32_t rc_shift = 0;
volatile uint32_t rc_bits = 0;
volatile uint32_t rc_edges = 0;
volatile uint32_t rc_last_cc = 0;
volatile uint32_t rc_frame = 0;
volatile uint32_t rc_frame_bits = 0;
volatile uint32_t rc_frames = 0;
uint32_t rc_gap_cycles = 720000;   // 3 ms; recomputed from CPU freq in setup

// Distinct-frame ring (ISR = producer, loop = consumer)
volatile uint32_t rc_ring[RC_RING];
volatile uint32_t rc_ring_bits[RC_RING];
volatile uint32_t rc_head = 0;
uint32_t rc_tail = 0;
volatile uint32_t rc_last_pushed = 0xFFFFFFFFU;

static inline uint32_t IRAM_ATTR rc_ccount() {
  uint32_t c;
  __asm__ __volatile__("rsr %0, ccount" : "=r"(c));
  return c;
}

void IRAM_ATTR rc_isr() {
  uint32_t cc = rc_ccount();
  rc_edges++;
  if ((uint32_t)(cc - rc_last_cc) > rc_gap_cycles) {   // >3ms gap = frame boundary
    if (rc_bits > 0) {
      uint32_t f = rc_shift;
      rc_frame = f;
      rc_frame_bits = rc_bits;
      rc_frames++;
      if (f != rc_last_pushed) {                       // queue only distinct frames
        uint32_t h = rc_head;
        rc_ring[h & (RC_RING - 1)] = f;
        rc_ring_bits[h & (RC_RING - 1)] = rc_bits;
        rc_head = h + 1;
        rc_last_pushed = f;
      }
    }
    rc_shift = 0;
    rc_bits = 0;
  }
  rc_last_cc = cc;
  uint32_t d = (REG_READ(GPIO_IN1_REG) >> (RC_DATA - 32)) & 0x1U;  // sample DATA
  rc_shift = (rc_shift << 1) | d;
  rc_bits++;
}

void rawcap_setup() {
  pinMode(RC_CLK, INPUT);
  pinMode(RC_DATA, INPUT);
  rc_gap_cycles = (uint32_t) getCpuFrequencyMhz() * 3000U;  // 3 ms in CPU cycles
  rc_last_cc = rc_ccount();
  attachInterrupt(digitalPinToInterrupt(RC_CLK), rc_isr, RISING);
}

// Drain & log each new distinct frame; return the most recent (for the web UI).
std::string rawcap_drain() {
  std::string last = "";
  uint32_t guard = 0;
  while (rc_tail != rc_head && guard < RC_RING) {
    uint32_t f = rc_ring[rc_tail & (RC_RING - 1)];
    uint32_t b = rc_ring_bits[rc_tail & (RC_RING - 1)];
    rc_tail++;
    guard++;
    uint8_t p1 = (f >> 17) & 0x7F;
    uint8_t p2 = (f >> 10) & 0x7F;
    uint8_t p3 = (f >> 3)  & 0x7F;
    uint8_t p4 = f & 0x7;
    char buf[160];
    snprintf(buf, sizeof(buf), "bits=%u raw=0x%06X p1=0x%02X p2=0x%02X p3=0x%02X p4=0x%X",
             (unsigned) b, (unsigned)(f & 0xFFFFFF), p1, p2, p3, p4);
    ESP_LOGI("frame", "%s", buf);
    last = std::string(buf);
  }
  return last;
}

std::string rawcap_summary() {
  static uint32_t last_edges = 0;
  noInterrupts();
  uint32_t edges  = rc_edges;
  uint32_t frames = rc_frames;
  interrupts();
  uint32_t rate = edges - last_edges;
  last_edges = edges;
  char buf[80];
  snprintf(buf, sizeof(buf), "clk/s=%u frames=%u", (unsigned) rate, (unsigned) frames);
  ESP_LOGI("rawcap", "%s", buf);
  return std::string(buf);
}
