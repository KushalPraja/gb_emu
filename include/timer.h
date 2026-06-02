#ifndef TIMER_H
#define TIMER_H

#include "types.h"

class Timer {
public:
  bool tick(int cycles) {
    bool overflow = false;
    for (int i = 0; i < cycles; i++) {
      u16 before = div;
      div++;                  
      if (tac & 0x04) {       
        u16 mask = timaMask();
        if ((before & mask) && !(div & mask)) { // falling edge of selected bit
          if (++tima == 0) {                     // TIMA wrapped past 0xFF
            tima = tma;                          // reload from TMA
            overflow = true;
          }
        }
      }
    }
    return overflow;
  }

  u8 read(u16 addr) {
    switch (addr) {
    case 0xFF04:
      return div >> 8; // DIV = high byte of the counter
    case 0xFF05:
      return tima;
    case 0xFF06:
      return tma;
    case 0xFF07:
      return tac | 0xF8; // unused top bits read as 1
    }
    return 0xFF;
  }

  void write(u16 addr, u8 value) {
    switch (addr) {
    case 0xFF04:
      div = 0; // any write resets the divider
      break;
    case 0xFF05:
      tima = value;
      break;
    case 0xFF06:
      tma = value;
      break;
    case 0xFF07:
      tac = value & 0x07;
      break;
    }
  }

private:
  u16 div = 0;
  u8 tima = 0, tma = 0, tac = 0;

  u16 timaMask() { // which counter bit ticks TIMA, per TAC
    switch (tac & 0x03) {
    case 0:
      return 1 << 9; // 4096 Hz   (every 1024 cycles)
    case 1:
      return 1 << 3; // 262144 Hz (every 16 cycles)
    case 2:
      return 1 << 5; // 65536 Hz  (every 64 cycles)
    default:
      return 1 << 7; // 16384 Hz  (every 256 cycles)
    }
  }
};

#endif
