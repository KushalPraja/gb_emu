#include "ppu.h"

namespace {
// LCDC bit helpers.
constexpr u8 LCDC_ENABLE = 0x80;
constexpr u8 LCDC_WIN_MAP = 0x40;
constexpr u8 LCDC_WIN_ENABLE = 0x20;
constexpr u8 LCDC_TILE_DATA = 0x10;
constexpr u8 LCDC_BG_MAP = 0x08;
constexpr u8 LCDC_OBJ_SIZE = 0x04;
constexpr u8 LCDC_OBJ_ENABLE = 0x02;
constexpr u8 LCDC_BG_ENABLE = 0x01;
} // namespace

void PPU::setMode(u8 mode) {
  stat = (stat & ~0x03) | (mode & 0x03);
}

void PPU::checkStatInterrupt() {
  // The STAT interrupt fires on the rising edge of the OR of all enabled
  // sources (LYC match, and the three mode conditions).
  bool lycMatch = (ly == lyc);
  if (lycMatch)
    stat |= 0x04;
  else
    stat &= ~0x04;

  u8 mode = stat & 0x03;
  bool line = false;
  if ((stat & 0x40) && lycMatch)
    line = true;
  if ((stat & 0x20) && mode == 2)
    line = true;
  if ((stat & 0x10) && mode == 1)
    line = true;
  if ((stat & 0x08) && mode == 0)
    line = true;

  if (line && !statLine)
    irq |= 0x02; // request STAT interrupt on rising edge
  statLine = line;
}

void PPU::tick(int cycles) {
  if (!(lcdc & LCDC_ENABLE)) {
    // LCD off: reset the timing, force mode 0 and LY 0.
    dot = 0;
    ly = 0;
    windowLine = 0;
    setMode(0);
    statLine = false;
    return;
  }

  dot += cycles;

  switch (stat & 0x03) {
  case 2: // OAM scan
    if (dot >= 80) {
      dot -= 80;
      setMode(3);
      checkStatInterrupt();
    }
    break;
  case 3: // Drawing
    if (dot >= 172) {
      dot -= 172;
      renderScanline();
      setMode(0);
      checkStatInterrupt();
    }
    break;
  case 0: // HBlank
    if (dot >= 204) {
      dot -= 204;
      ly++;
      if (ly == 144) {
        setMode(1); // enter VBlank
        irq |= 0x01;
        frameReady = true;
      } else {
        setMode(2);
      }
      checkStatInterrupt();
    }
    break;
  case 1: // VBlank
    if (dot >= 456) {
      dot -= 456;
      ly++;
      if (ly > 153) {
        ly = 0;
        windowLine = 0;
        setMode(2);
      }
      checkStatInterrupt();
    }
    break;
  }
}

void PPU::renderScanline() {
  bgColor.fill(0);
  if (lcdc & LCDC_BG_ENABLE)
    renderBackground();
  else
    for (int x = 0; x < WIDTH; x++)
      fb[ly * WIDTH + x] = colors[0];
  if (lcdc & LCDC_OBJ_ENABLE)
    renderSprites();
}

void PPU::renderBackground() {
  bool winEnabled = (lcdc & LCDC_WIN_ENABLE) && wy <= ly;
  int winX = (int)wx - 7;
  bool usedWindow = false;

  u16 bgMapBase = (lcdc & LCDC_BG_MAP) ? 0x1C00 : 0x1800;
  u16 winMapBase = (lcdc & LCDC_WIN_MAP) ? 0x1C00 : 0x1800;
  bool unsignedTiles = (lcdc & LCDC_TILE_DATA) != 0;

  for (int x = 0; x < WIDTH; x++) {
    bool inWindow = winEnabled && x >= winX;
    u16 mapBase;
    int mapX, mapY;
    if (inWindow) {
      usedWindow = true;
      mapBase = winMapBase;
      mapX = x - winX;
      mapY = windowLine;
    } else {
      mapBase = bgMapBase;
      mapX = (scx + x) & 0xFF;
      mapY = (scy + ly) & 0xFF;
    }

    int tileCol = mapX / 8;
    int tileRow = mapY / 8;
    u8 tileIndex = vram[mapBase + tileRow * 32 + tileCol];

    u16 tileAddr;
    if (unsignedTiles)
      tileAddr = (u16)tileIndex * 16;
    else
      tileAddr = 0x1000 + (s8)tileIndex * 16;

    int lineInTile = mapY % 8;
    u8 lo = vram[tileAddr + lineInTile * 2];
    u8 hi = vram[tileAddr + lineInTile * 2 + 1];
    int bit = 7 - (mapX % 8);
    u8 colorNum = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);

    bgColor[x] = colorNum;
    u8 shade = (bgp >> (colorNum * 2)) & 0x03;
    fb[ly * WIDTH + x] = colors[shade];
  }

  if (usedWindow)
    windowLine++;
}

void PPU::renderSprites() {
  int height = (lcdc & LCDC_OBJ_SIZE) ? 16 : 8;

  // Collect up to 10 sprites visible on this line, in OAM order.
  struct Sprite {
    int x, y, oamIndex;
    u8 tile, attr;
  };
  Sprite line[10];
  int count = 0;
  for (int i = 0; i < 40 && count < 10; i++) {
    int sy = (int)oam[i * 4] - 16;
    if (ly >= sy && ly < sy + height) {
      line[count++] = {(int)oam[i * 4 + 1] - 8, sy, i, oam[i * 4 + 2],
                       oam[i * 4 + 3]};
    }
  }

  // Draw in reverse priority order so the highest-priority sprite (smallest x,
  // then smallest OAM index) is drawn last and wins overlaps.
  for (int a = 0; a < count; a++)
    for (int b = a + 1; b < count; b++)
      if (line[b].x < line[a].x ||
          (line[b].x == line[a].x && line[b].oamIndex < line[a].oamIndex)) {
        Sprite t = line[a];
        line[a] = line[b];
        line[b] = t;
      }

  for (int s = count - 1; s >= 0; s--) {
    const Sprite &sp = line[s];
    bool yflip = sp.attr & 0x40;
    bool xflip = sp.attr & 0x20;
    bool behindBg = sp.attr & 0x80;
    u8 palette = (sp.attr & 0x10) ? obp1 : obp0;

    int row = ly - sp.y;
    if (yflip)
      row = height - 1 - row;

    u8 tile = sp.tile;
    if (height == 16)
      tile &= 0xFE; // 8x16 uses an even tile pair
    u16 tileAddr = (u16)tile * 16 + row * 2;
    u8 lo = vram[tileAddr];
    u8 hi = vram[tileAddr + 1];

    for (int px = 0; px < 8; px++) {
      int x = sp.x + px;
      if (x < 0 || x >= WIDTH)
        continue;
      int bit = xflip ? px : 7 - px;
      u8 colorNum = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
      if (colorNum == 0)
        continue; // transparent
      if (behindBg && bgColor[x] != 0)
        continue; // sprite hidden behind non-zero background
      u8 shade = (palette >> (colorNum * 2)) & 0x03;
      fb[ly * WIDTH + x] = colors[shade];
    }
  }
}

u8 PPU::readReg(u16 addr) const {
  switch (addr) {
  case 0xFF40:
    return lcdc;
  case 0xFF41:
    return stat | 0x80; // bit 7 reads as 1
  case 0xFF42:
    return scy;
  case 0xFF43:
    return scx;
  case 0xFF44:
    return ly;
  case 0xFF45:
    return lyc;
  case 0xFF46:
    return dma;
  case 0xFF47:
    return bgp;
  case 0xFF48:
    return obp0;
  case 0xFF49:
    return obp1;
  case 0xFF4A:
    return wy;
  case 0xFF4B:
    return wx;
  }
  return 0xFF;
}

void PPU::writeReg(u16 addr, u8 value) {
  switch (addr) {
  case 0xFF40: {
    bool wasOn = lcdc & LCDC_ENABLE;
    lcdc = value;
    if (wasOn && !(lcdc & LCDC_ENABLE)) {
      // Turning the LCD off resets the renderer state.
      ly = 0;
      dot = 0;
      windowLine = 0;
      setMode(0);
      statLine = false;
    }
    break;
  }
  case 0xFF41:
    stat = (stat & 0x07) | (value & 0x78); // only bits 3-6 are writable
    break;
  case 0xFF42:
    scy = value;
    break;
  case 0xFF43:
    scx = value;
    break;
  case 0xFF44:
    break; // LY is read-only
  case 0xFF45:
    lyc = value;
    break;
  case 0xFF46:
    dma = value; // actual transfer is handled by the bus
    break;
  case 0xFF47:
    bgp = value;
    break;
  case 0xFF48:
    obp0 = value;
    break;
  case 0xFF49:
    obp1 = value;
    break;
  case 0xFF4A:
    wy = value;
    break;
  case 0xFF4B:
    wx = value;
    break;
  }
}
