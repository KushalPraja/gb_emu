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
  memory[addr] = value;
  if (addr == 0xFF02 && value == 0x81) { 
    std::putchar((char)memory[0xFF01]);
    std::fflush(stdout);
  }
}