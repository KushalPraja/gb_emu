#ifndef bus_h
#define bus_h

#include "apu.h"
#include "cartridge.h"
#include "ppu.h"
#include "timer.h"
#include "types.h"
#include <array>
#include <cstdio>

// Bus is the central memory map. It owns the cartridge, PPU, APU and timer and
// routes every CPU read/write to the right component, plus work/high RAM, the
// joypad, serial port and the interrupt registers (IF/IE).
class Bus {
public:
  PPU ppu;
  APU apu;

  bool loadRom(const char *path) { return cart.load(path); }

  // Persist battery-backed cartridge RAM (no-op for non-battery carts).
  void saveRam() { cart.flushSave(); }

  u8 read(u16 addr) {
    if (addr < 0x8000)
      return cart.readRom(addr);
    if (addr < 0xA000)
      return ppu.readVram(addr);
    if (addr < 0xC000)
      return cart.readRam(addr);
    if (addr < 0xE000)
      return wram[addr - 0xC000];
    if (addr < 0xFE00)
      return wram[addr - 0xE000]; // echo RAM
    if (addr < 0xFEA0)
      return ppu.readOam(addr);
    if (addr < 0xFF00)
      return 0xFF; // unusable
    if (addr == 0xFF00)
      return readJoypad();
    if (addr == 0xFF01)
      return sb;
    if (addr == 0xFF02)
      return sc | 0x7E;
    if (addr >= 0xFF04 && addr <= 0xFF07)
      return timer.read(addr);
    if (addr == 0xFF0F)
      return iflag | 0xE0;
    if (addr >= 0xFF10 && addr <= 0xFF3F)
      return apu.readReg(addr);
    if (addr >= 0xFF40 && addr <= 0xFF4B)
      return ppu.readReg(addr);
    if (addr >= 0xFF80 && addr <= 0xFFFE)
      return hram[addr - 0xFF80];
    if (addr == 0xFFFF)
      return ie;
    return 0xFF;
  }

  void write(u16 addr, u8 value) {
    if (addr < 0x8000) {
      cart.writeRom(addr, value);
      return;
    }
    if (addr < 0xA000) {
      ppu.writeVram(addr, value);
      return;
    }
    if (addr < 0xC000) {
      cart.writeRam(addr, value);
      return;
    }
    if (addr < 0xE000) {
      wram[addr - 0xC000] = value;
      return;
    }
    if (addr < 0xFE00) {
      wram[addr - 0xE000] = value; // echo RAM
      return;
    }
    if (addr < 0xFEA0) {
      ppu.writeOam(addr, value);
      return;
    }
    if (addr < 0xFF00)
      return; // unusable
    if (addr == 0xFF00) {
      joypSelect = value & 0x30;
      return;
    }
    if (addr == 0xFF01) {
      sb = value;
      return;
    }
    if (addr == 0xFF02) {
      sc = value;
      if ((value & 0x81) == 0x81) { // serial transfer with internal clock
        std::putchar((char)sb);
        std::fflush(stdout);
      }
      return;
    }
    if (addr >= 0xFF04 && addr <= 0xFF07) {
      timer.write(addr, value);
      return;
    }
    if (addr == 0xFF0F) {
      iflag = value & 0x1F;
      return;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
      apu.writeReg(addr, value);
      return;
    }
    if (addr == 0xFF46) {
      ppu.writeReg(addr, value);
      doOamDma(value);
      return;
    }
    if (addr >= 0xFF40 && addr <= 0xFF4B) {
      ppu.writeReg(addr, value);
      return;
    }
    if (addr >= 0xFF80 && addr <= 0xFFFE) {
      hram[addr - 0xFF80] = value;
      return;
    }
    if (addr == 0xFFFF) {
      ie = value;
      return;
    }
    // Remaining I/O registers are unimplemented and ignored.
  }

  // Called by the CPU after every instruction to advance the peripherals.
  void tick(int cycles) {
    if (timer.tick(cycles))
      iflag |= 0x04;
    ppu.tick(cycles);
    iflag |= ppu.irq;
    ppu.irq = 0;
    apu.tick(cycles);
  }

  // Host input: bit0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up, 7 Down
  // (1 = pressed). Requests a joypad interrupt on any newly pressed button.
  void setButtons(u8 pressed) {
    if (pressed & ~buttons)
      iflag |= 0x10;
    buttons = pressed;
  }

private:
  Cartridge cart;
  Timer timer;
  std::array<u8, 0x2000> wram{};
  std::array<u8, 0x7F> hram{};

  u8 iflag = 0; // IF (0xFF0F)
  u8 ie = 0;    // IE (0xFFFF)

  // Serial.
  u8 sb = 0, sc = 0;

  // Joypad.
  u8 buttons = 0;    // pressed state, 1 = pressed
  u8 joypSelect = 0; // 0xFF00 select bits 4-5

  u8 readJoypad() const {
    u8 res = 0xCF | joypSelect;
    if (!(joypSelect & 0x10)) { // direction keys selected
      if (buttons & 0x10)
        res &= ~0x01; // right
      if (buttons & 0x20)
        res &= ~0x02; // left
      if (buttons & 0x40)
        res &= ~0x04; // up
      if (buttons & 0x80)
        res &= ~0x08; // down
    }
    if (!(joypSelect & 0x20)) { // action buttons selected
      if (buttons & 0x01)
        res &= ~0x01; // A
      if (buttons & 0x02)
        res &= ~0x02; // B
      if (buttons & 0x04)
        res &= ~0x04; // select
      if (buttons & 0x08)
        res &= ~0x08; // start
    }
    return res;
  }

  void doOamDma(u8 value) {
    u16 src = (u16)value << 8;
    for (u16 i = 0; i < 0xA0; i++)
      ppu.writeOam(0xFE00 + i, read(src + i));
  }
};

#endif
