#ifndef bus_h
#define bus_h

#include "types.h"
#include <array>
#include <cstdio>
#include <string>

class Bus {

public:
  u8 read(u16 addr) { return memory[addr]; }
  void write(u16 addr, u8 value);
  bool loadRom(const char *path);

private:
  std::array<u8, 0x10000>
      memory{}; // 64KB of memory(temporary still have to implement memory map)
};

#endif
