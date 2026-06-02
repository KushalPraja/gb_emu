#include "bus.h"
#include <cstdio> // add to bus.h

bool Bus::loadRom(const char *path) {
  std::FILE *f = std::fopen(path, "rb");
  if (!f)
    return false;
  std::fread(memory.data(), 1, memory.size(),
             f); // up to 64KB; fine for 32KB test ROMs
  std::fclose(f);
  return true;
}

void Bus::write(u16 addr, u8 value) {
  if (addr < 0x8000)
    return; // ROM region is read-only
  if (addr >= 0xFF04 && addr <= 0xFF07) {
    timer.write(addr, value);
    return;
  }
  memory[addr] = value;
  if (addr == 0xFF02 && value == 0x81) {
    std::putchar((char)memory[0xFF01]);
    std::fflush(stdout);
  }
}