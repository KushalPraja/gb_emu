#ifndef bus_h
#define bus_h

#include "timer.h"
#include "types.h"
#include <array>
#include <cstdio>
#include <string>

class Bus {

public:
  u8 read(u16 addr) {
    if (addr == 0xFF44)
      return 0x90; // LY stub (GB Doctor)
    if (addr >= 0xFF04 && addr <= 0xFF07)
      return timer.read(addr);
    return memory[addr];
  }
  void write(u16 addr, u8 value);
  bool loadRom(const char *path);

  void tickTimer(int cycles) {
    if (timer.tick(cycles))
      memory[0xFF0F] |= 0x04; // request timer interrupt
  }

private:
  std::array<u8, 0x10000>
      memory{}; // 64KB of memory(temporary still have to implement memory map)
  Timer timer;
};

#endif
