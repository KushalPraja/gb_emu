#ifndef PPU_H
#define PPU_H

#include "types.h"
#include <array>

// PPU emulates the DMG LCD controller: VRAM, OAM, the LCD registers
// (0xFF40-0xFF4B) and a scanline-based renderer. It runs off the CPU clock via
// tick() and produces a 160x144 ARGB framebuffer once per frame.
class PPU {
public:
  static constexpr int WIDTH = 160;
  static constexpr int HEIGHT = 144;

  void tick(int cycles);

  // Memory access (decoded by the bus).
  u8 readVram(u16 addr) const { return vram[addr - 0x8000]; }
  void writeVram(u16 addr, u8 value) { vram[addr - 0x8000] = value; }
  u8 readOam(u16 addr) const { return oam[addr - 0xFE00]; }
  void writeOam(u16 addr, u8 value) { oam[addr - 0xFE00] = value; }

  u8 readReg(u16 addr) const;
  void writeReg(u16 addr, u8 value);

  // Swap the 4-shade output palette (index 0 = lightest). ARGB8888 entries.
  void setPalette(const u32 p[4]) {
    for (int i = 0; i < 4; i++)
      colors[i] = p[i];
  }

  // Interrupt requests accumulated during tick(): bit0 = VBlank, bit1 = STAT.
  // The bus drains this into the IF register.
  u8 irq = 0;

  // Set true when a full frame has been rendered; the host clears it.
  bool frameReady = false;
  const u32 *framebuffer() const { return fb.data(); }

private:
  std::array<u8, 0x2000> vram{};
  std::array<u8, 0xA0> oam{};
  std::array<u32, WIDTH * HEIGHT> fb{};

  // Output palette, shade 0 (lightest) to 3 (darkest). Defaults to DMG green.
  std::array<u32, 4> colors{0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F};

  // LCD registers.
  u8 lcdc = 0x91;
  u8 stat = 0x00;
  u8 scy = 0, scx = 0;
  u8 ly = 0, lyc = 0;
  u8 bgp = 0xFC, obp0 = 0xFF, obp1 = 0xFF;
  u8 wy = 0, wx = 0;
  u8 dma = 0;

  int dot = 0;           // dot counter within the current scanline (0-455)
  int windowLine = 0;    // internal window line counter
  bool statLine = false; // for STAT interrupt edge detection

  // Per-scanline background colour numbers (pre-palette) for sprite priority.
  std::array<u8, WIDTH> bgColor{};

  void setMode(u8 mode);
  void checkStatInterrupt();
  void renderScanline();
  void renderBackground();
  void renderSprites();
};

#endif // PPU_H
