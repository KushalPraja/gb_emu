#ifndef APU_H
#define APU_H

#include "types.h"
#include <array>
#include <vector>

// APU emulates the DMG sound hardware: two pulse channels (one with a frequency
// sweep), a wave channel and a noise channel, mixed through the master volume
// and panning registers. It is clocked off the CPU via tick() and accumulates
// interleaved stereo float samples for the host audio device.
class APU {
public:
  static constexpr int SAMPLE_RATE = 44100;

  void tick(int cycles);

  u8 readReg(u16 addr) const;
  void writeReg(u16 addr, u8 value);

  // Interleaved L,R float samples produced since the last drain.
  std::vector<float> samples;

private:
  // --- channel building blocks ---
  struct Envelope {
    u8 initialVolume = 0;
    bool addMode = false;
    u8 period = 0;
    u8 volume = 0;
    u8 timer = 0;
    void trigger();
    void clock();
  };

  struct Pulse {
    bool enabled = false;
    bool dacEnabled = false;
    u8 duty = 0;
    int dutyStep = 0;
    u16 freq = 0;
    int timer = 0;
    Envelope env;
    // Length
    u8 lengthCounter = 0;
    bool lengthEnabled = false;
    // Sweep (channel 1 only)
    bool hasSweep = false;
    u8 sweepPeriod = 0, sweepShift = 0;
    bool sweepNegate = false;
    bool sweepEnabled = false;
    u16 sweepShadow = 0;
    u8 sweepTimer = 0;

    void trigger();
    void clockTimer();
    void clockLength();
    void clockSweep();
    u16 sweepCalc();
    u8 output() const;
  };

  struct Wave {
    bool enabled = false;
    bool dacEnabled = false;
    u16 freq = 0;
    int timer = 0;
    int position = 0;
    u8 volumeCode = 0;
    u16 lengthCounter = 0; // wave length counts up to 256
    bool lengthEnabled = false;
    std::array<u8, 16> ram{};

    void trigger();
    void clockTimer();
    void clockLength();
    u8 output() const;
  };

  struct Noise {
    bool enabled = false;
    bool dacEnabled = false;
    int timer = 0;
    u16 lfsr = 0x7FFF;
    u8 clockShift = 0, divisorCode = 0;
    bool widthMode = false;
    Envelope env;
    u8 lengthCounter = 0;
    bool lengthEnabled = false;

    void trigger();
    void clockTimer();
    void clockLength();
    u8 output() const;
  };

  Pulse ch1, ch2;
  Wave ch3;
  Noise ch4;

  // Master control.
  bool powerOn = false;
  u8 nr50 = 0; // master volume + VIN
  u8 nr51 = 0; // panning

  // Frame sequencer (512 Hz) and sample down-conversion.
  int frameSeqTimer = 0;
  int frameSeqStep = 0;
  double sampleCounter = 0.0;

  // DC-blocking high-pass filter state (per channel side), like the DMG.
  float capL = 0.0f, capR = 0.0f;

  // Raw register bytes for 0xFF10-0xFF26 to support accurate read-back.
  std::array<u8, 0x17> raw{};

  void clockFrameSequencer();
  void generateSample();
};

#endif // APU_H
