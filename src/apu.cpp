#include "apu.h"

namespace {
constexpr double CPU_FREQ = 4194304.0;
constexpr double CYCLES_PER_SAMPLE = CPU_FREQ / APU::SAMPLE_RATE;

// Duty patterns for the pulse channels.
constexpr u8 kDuty[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
};

// Read-back OR masks for 0xFF10-0xFF26 (bits that always read as 1).
constexpr u8 kReadMask[0x17] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, // NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF, // NR15(unused)-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF, // NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF, // NR40(unused)-NR44
    0x00, 0x00, 0x70,             // NR50-NR52
};
} // namespace

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------
void APU::Envelope::trigger() {
  volume = initialVolume;
  timer = period;
}

void APU::Envelope::clock() {
  if (period == 0)
    return;
  if (timer > 0)
    timer--;
  if (timer == 0) {
    timer = period;
    if (addMode && volume < 15)
      volume++;
    else if (!addMode && volume > 0)
      volume--;
  }
}

// ---------------------------------------------------------------------------
// Pulse channel
// ---------------------------------------------------------------------------
void APU::Pulse::clockTimer() {
  if (--timer <= 0) {
    timer = (2048 - freq) * 4;
    dutyStep = (dutyStep + 1) & 7;
  }
}

void APU::Pulse::clockLength() {
  if (lengthEnabled && lengthCounter > 0) {
    if (--lengthCounter == 0)
      enabled = false;
  }
}

u16 APU::Pulse::sweepCalc() {
  u16 delta = sweepShadow >> sweepShift;
  u16 newFreq = sweepNegate ? sweepShadow - delta : sweepShadow + delta;
  if (newFreq > 2047)
    enabled = false; // overflow disables the channel
  return newFreq;
}

void APU::Pulse::clockSweep() {
  if (!hasSweep)
    return;
  if (--sweepTimer <= 0) {
    sweepTimer = sweepPeriod ? sweepPeriod : 8;
    if (sweepEnabled && sweepPeriod > 0) {
      u16 newFreq = sweepCalc();
      if (newFreq <= 2047 && sweepShift > 0) {
        sweepShadow = newFreq;
        freq = newFreq;
        sweepCalc(); // second calc for the overflow check
      }
    }
  }
}

void APU::Pulse::trigger() {
  enabled = dacEnabled;
  env.trigger();
  timer = (2048 - freq) * 4;
  if (lengthCounter == 0)
    lengthCounter = 64;
  if (hasSweep) {
    sweepShadow = freq;
    sweepTimer = sweepPeriod ? sweepPeriod : 8;
    sweepEnabled = (sweepPeriod > 0) || (sweepShift > 0);
    if (sweepShift > 0)
      sweepCalc();
  }
}

u8 APU::Pulse::output() const {
  if (!enabled || !dacEnabled)
    return 0;
  return kDuty[duty][dutyStep] ? env.volume : 0;
}

// ---------------------------------------------------------------------------
// Wave channel
// ---------------------------------------------------------------------------
void APU::Wave::clockTimer() {
  if (--timer <= 0) {
    timer = (2048 - freq) * 2;
    position = (position + 1) & 31;
  }
}

void APU::Wave::clockLength() {
  if (lengthEnabled && lengthCounter > 0) {
    if (--lengthCounter == 0)
      enabled = false;
  }
}

void APU::Wave::trigger() {
  enabled = dacEnabled;
  timer = (2048 - freq) * 2;
  position = 0;
  if (lengthCounter == 0)
    lengthCounter = 256;
}

u8 APU::Wave::output() const {
  if (!enabled || !dacEnabled || volumeCode == 0)
    return 0;
  u8 sample = ram[position / 2];
  sample = (position & 1) ? (sample & 0x0F) : (sample >> 4);
  return sample >> (volumeCode - 1); // 100% / 50% / 25%
}

// ---------------------------------------------------------------------------
// Noise channel
// ---------------------------------------------------------------------------
void APU::Noise::clockTimer() {
  if (--timer <= 0) {
    int divisor = divisorCode ? divisorCode * 16 : 8;
    timer = divisor << clockShift;
    u8 xorBit = (lfsr ^ (lfsr >> 1)) & 1;
    lfsr >>= 1;
    lfsr |= xorBit << 14;
    if (widthMode) {
      lfsr &= ~(1 << 6);
      lfsr |= xorBit << 6;
    }
  }
}

void APU::Noise::clockLength() {
  if (lengthEnabled && lengthCounter > 0) {
    if (--lengthCounter == 0)
      enabled = false;
  }
}

void APU::Noise::trigger() {
  enabled = dacEnabled;
  env.trigger();
  lfsr = 0x7FFF;
  int divisor = divisorCode ? divisorCode * 16 : 8;
  timer = divisor << clockShift;
  if (lengthCounter == 0)
    lengthCounter = 64;
}

u8 APU::Noise::output() const {
  if (!enabled || !dacEnabled)
    return 0;
  return (~lfsr & 1) ? env.volume : 0;
}

// ---------------------------------------------------------------------------
// Frame sequencer + mixing
// ---------------------------------------------------------------------------
void APU::clockFrameSequencer() {
  // 512 Hz sequence: length on even steps, sweep on 2/6, envelope on 7.
  switch (frameSeqStep) {
  case 0:
  case 4:
    ch1.clockLength();
    ch2.clockLength();
    ch3.clockLength();
    ch4.clockLength();
    break;
  case 2:
  case 6:
    ch1.clockLength();
    ch2.clockLength();
    ch3.clockLength();
    ch4.clockLength();
    ch1.clockSweep();
    break;
  case 7:
    ch1.env.clock();
    ch2.env.clock();
    ch4.env.clock();
    break;
  }
  frameSeqStep = (frameSeqStep + 1) & 7;
}

void APU::generateSample() {
  auto dac = [](u8 digital, bool on) -> float {
    return on ? (digital / 7.5f - 1.0f) : 0.0f;
  };

  float c1 = dac(ch1.output(), ch1.dacEnabled);
  float c2 = dac(ch2.output(), ch2.dacEnabled);
  float c3 = dac(ch3.output(), ch3.dacEnabled);
  float c4 = dac(ch4.output(), ch4.dacEnabled);

  float left = 0, right = 0;
  if (nr51 & 0x10)
    left += c1;
  if (nr51 & 0x20)
    left += c2;
  if (nr51 & 0x40)
    left += c3;
  if (nr51 & 0x80)
    left += c4;
  if (nr51 & 0x01)
    right += c1;
  if (nr51 & 0x02)
    right += c2;
  if (nr51 & 0x04)
    right += c3;
  if (nr51 & 0x08)
    right += c4;

  float leftVol = ((nr50 >> 4) & 0x07) + 1;
  float rightVol = (nr50 & 0x07) + 1;
  left = left / 4.0f * (leftVol / 8.0f);
  right = right / 4.0f * (rightVol / 8.0f);

  // One-pole DC-blocking high-pass filter.
  float outL = left - capL;
  capL = left - outL * 0.996f;
  float outR = right - capR;
  capR = right - outR * 0.996f;

  samples.push_back(outL);
  samples.push_back(outR);
}

void APU::tick(int cycles) {
  for (int i = 0; i < cycles; i++) {
    if (powerOn) {
      ch1.clockTimer();
      ch2.clockTimer();
      ch3.clockTimer();
      ch4.clockTimer();
      if (++frameSeqTimer >= 8192) {
        frameSeqTimer = 0;
        clockFrameSequencer();
      }
    }
    sampleCounter += 1.0;
    if (sampleCounter >= CYCLES_PER_SAMPLE) {
      sampleCounter -= CYCLES_PER_SAMPLE;
      generateSample();
    }
  }
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------
u8 APU::readReg(u16 addr) const {
  if (addr >= 0xFF30 && addr <= 0xFF3F)
    return ch3.ram[addr - 0xFF30];

  if (addr == 0xFF26) {
    u8 status = powerOn ? 0x80 : 0x00;
    if (ch1.enabled)
      status |= 0x01;
    if (ch2.enabled)
      status |= 0x02;
    if (ch3.enabled)
      status |= 0x04;
    if (ch4.enabled)
      status |= 0x08;
    return status | kReadMask[0x16];
  }

  if (addr >= 0xFF10 && addr <= 0xFF25) {
    int idx = addr - 0xFF10;
    return raw[idx] | kReadMask[idx];
  }
  return 0xFF;
}

void APU::writeReg(u16 addr, u8 value) {
  if (addr >= 0xFF30 && addr <= 0xFF3F) {
    ch3.ram[addr - 0xFF30] = value;
    return;
  }

  if (addr == 0xFF26) {
    bool turnOn = value & 0x80;
    if (!turnOn && powerOn) {
      // Powering off clears every sound register.
      ch1 = Pulse{};
      ch1.hasSweep = true;
      ch2 = Pulse{};
      ch3.enabled = false;
      ch3.dacEnabled = false;
      ch4 = Noise{};
      nr50 = nr51 = 0;
      raw.fill(0);
    }
    powerOn = turnOn;
    return;
  }

  if (!powerOn)
    return; // while off, only NR52 and wave RAM are writable

  int idx = addr - 0xFF10;
  if (idx >= 0 && idx < 0x17)
    raw[idx] = value;

  switch (addr) {
  // --- Channel 1 (pulse + sweep) ---
  case 0xFF10:
    ch1.hasSweep = true;
    ch1.sweepPeriod = (value >> 4) & 0x07;
    ch1.sweepNegate = value & 0x08;
    ch1.sweepShift = value & 0x07;
    break;
  case 0xFF11:
    ch1.duty = value >> 6;
    ch1.lengthCounter = 64 - (value & 0x3F);
    break;
  case 0xFF12:
    ch1.env.initialVolume = value >> 4;
    ch1.env.addMode = value & 0x08;
    ch1.env.period = value & 0x07;
    ch1.dacEnabled = (value & 0xF8) != 0;
    if (!ch1.dacEnabled)
      ch1.enabled = false;
    break;
  case 0xFF13:
    ch1.freq = (ch1.freq & 0x700) | value;
    break;
  case 0xFF14:
    ch1.freq = (ch1.freq & 0xFF) | ((value & 0x07) << 8);
    ch1.lengthEnabled = value & 0x40;
    if (value & 0x80)
      ch1.trigger();
    break;

  // --- Channel 2 (pulse) ---
  case 0xFF16:
    ch2.duty = value >> 6;
    ch2.lengthCounter = 64 - (value & 0x3F);
    break;
  case 0xFF17:
    ch2.env.initialVolume = value >> 4;
    ch2.env.addMode = value & 0x08;
    ch2.env.period = value & 0x07;
    ch2.dacEnabled = (value & 0xF8) != 0;
    if (!ch2.dacEnabled)
      ch2.enabled = false;
    break;
  case 0xFF18:
    ch2.freq = (ch2.freq & 0x700) | value;
    break;
  case 0xFF19:
    ch2.freq = (ch2.freq & 0xFF) | ((value & 0x07) << 8);
    ch2.lengthEnabled = value & 0x40;
    if (value & 0x80)
      ch2.trigger();
    break;

  // --- Channel 3 (wave) ---
  case 0xFF1A:
    ch3.dacEnabled = value & 0x80;
    if (!ch3.dacEnabled)
      ch3.enabled = false;
    break;
  case 0xFF1B:
    ch3.lengthCounter = 256 - value;
    break;
  case 0xFF1C:
    ch3.volumeCode = (value >> 5) & 0x03;
    break;
  case 0xFF1D:
    ch3.freq = (ch3.freq & 0x700) | value;
    break;
  case 0xFF1E:
    ch3.freq = (ch3.freq & 0xFF) | ((value & 0x07) << 8);
    ch3.lengthEnabled = value & 0x40;
    if (value & 0x80)
      ch3.trigger();
    break;

  // --- Channel 4 (noise) ---
  case 0xFF20:
    ch4.lengthCounter = 64 - (value & 0x3F);
    break;
  case 0xFF21:
    ch4.env.initialVolume = value >> 4;
    ch4.env.addMode = value & 0x08;
    ch4.env.period = value & 0x07;
    ch4.dacEnabled = (value & 0xF8) != 0;
    if (!ch4.dacEnabled)
      ch4.enabled = false;
    break;
  case 0xFF22:
    ch4.clockShift = value >> 4;
    ch4.widthMode = value & 0x08;
    ch4.divisorCode = value & 0x07;
    break;
  case 0xFF23:
    ch4.lengthEnabled = value & 0x40;
    if (value & 0x80)
      ch4.trigger();
    break;

  // --- Master control ---
  case 0xFF24:
    nr50 = value;
    break;
  case 0xFF25:
    nr51 = value;
    break;
  }
}
