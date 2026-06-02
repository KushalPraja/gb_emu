#include "bus.h"
#include "core.h"
#include <cstdio>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::printf("usage: %s rom.gb\n", argv[0]);
    return 1;
  }

  Bus bus;
  if (!bus.loadRom(argv[1])) {
    std::printf("could not open %s\n", argv[1]);
    return 1;
  }

  Core cpu(bus);
  cpu.powerOn();

  for (long i = 0; i < 50'000'000L; i++)
    cpu.step();

  std::printf("\n");
  return 0;
}