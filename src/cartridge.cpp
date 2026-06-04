#include "cartridge.h"
#include <cstdio>

bool Cartridge::load(const char *path) {
  std::FILE *f = std::fopen(path, "rb");
  if (!f)
    return false;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  rom.resize((size_t)size);
  size_t got = std::fread(rom.data(), 1, rom.size(), f);
  std::fclose(f);
  if (got != rom.size())
    return false;

  // Title at 0x0134-0x0143 (ASCII, null padded).
  romTitle.clear();
  for (u16 i = 0x0134; i <= 0x0143 && i < rom.size(); i++) {
    char c = (char)rom[i];
    if (c == 0)
      break;
    romTitle.push_back(c);
  }

  u8 type = rom.size() > 0x0147 ? rom[0x0147] : 0x00;
  switch (type) {
  case 0x00: // ROM only
  case 0x08: // ROM + RAM
  case 0x09: // ROM + RAM + battery
    mbc = Mbc::None;
    break;
  case 0x01: // MBC1
  case 0x02: // MBC1 + RAM
  case 0x03: // MBC1 + RAM + battery
    mbc = Mbc::Mbc1;
    break;
  default:
    std::printf("cartridge: unsupported mapper type 0x%02X, assuming MBC1\n",
                (unsigned)type);
    mbc = Mbc::Mbc1;
    break;
  }

  // ROM size: 0x8000 << header value.
  u8 romSizeCode = rom.size() > 0x0148 ? rom[0x0148] : 0x00;
  romBankCount = (0x8000u << romSizeCode) / 0x4000u;
  if (romBankCount == 0)
    romBankCount = 2;

  // RAM size header.
  u8 ramSizeCode = rom.size() > 0x0149 ? rom[0x0149] : 0x00;
  switch (ramSizeCode) {
  case 0x02:
    ramBankCount = 1;
    break; // 8KB
  case 0x03:
    ramBankCount = 4;
    break; // 32KB
  case 0x04:
    ramBankCount = 16;
    break; // 128KB
  case 0x05:
    ramBankCount = 8;
    break; // 64KB
  default:
    ramBankCount = 0;
    break;
  }
  // ROM+RAM cartridge types imply RAM even if the size code is odd.
  if ((type == 0x08 || type == 0x09 || type == 0x02 || type == 0x03) &&
      ramBankCount == 0)
    ramBankCount = 1;

  hasRam = ramBankCount > 0;
  ram.assign((size_t)ramBankCount * 0x2000, 0);

  std::printf("Loaded ROM: \"%s\"  type=0x%02X  %u ROM banks  %u RAM banks\n",
              romTitle.c_str(), (unsigned)type, romBankCount, ramBankCount);
  return true;
}

u32 Cartridge::romOffsetLow(u16 addr) const {
  // 0x0000-0x3FFF. In MBC1 advanced (RAM) banking mode the upper bits select
  // this region's bank; otherwise it is fixed bank 0.
  u32 bank = 0;
  if (mbc == Mbc::Mbc1 && bankMode == 1)
    bank = ((u32)romBankHigh << 5);
  bank &= (romBankCount - 1);
  return bank * 0x4000u + addr;
}

u32 Cartridge::romOffsetHigh(u16 addr) const {
  if (mbc == Mbc::None)
    return addr; // flat 32KB
  u32 low = romBankLow & 0x1F;
  if (low == 0)
    low = 1;
  u32 bank = (((u32)romBankHigh << 5) | low) & (romBankCount - 1);
  return bank * 0x4000u + (addr - 0x4000u);
}

u8 Cartridge::readRom(u16 addr) const {
  u32 off = addr < 0x4000 ? romOffsetLow(addr) : romOffsetHigh(addr);
  if (off < rom.size())
    return rom[off];
  return 0xFF;
}

void Cartridge::writeRom(u16 addr, u8 value) {
  if (mbc != Mbc::Mbc1)
    return;
  if (addr < 0x2000) {
    ramEnabled = (value & 0x0F) == 0x0A;
  } else if (addr < 0x4000) {
    romBankLow = value & 0x1F;
  } else if (addr < 0x6000) {
    romBankHigh = value & 0x03;
  } else {
    bankMode = value & 0x01;
  }
}

u32 Cartridge::ramOffset(u16 addr) const {
  u32 bank = (mbc == Mbc::Mbc1 && bankMode == 1) ? romBankHigh : 0;
  if (ramBankCount > 0)
    bank &= (ramBankCount - 1);
  return bank * 0x2000u + (addr - 0xA000u);
}

u8 Cartridge::readRam(u16 addr) const {
  if (!hasRam || (mbc == Mbc::Mbc1 && !ramEnabled))
    return 0xFF;
  u32 off = ramOffset(addr);
  if (off < ram.size())
    return ram[off];
  return 0xFF;
}

void Cartridge::writeRam(u16 addr, u8 value) {
  if (!hasRam || (mbc == Mbc::Mbc1 && !ramEnabled))
    return;
  u32 off = ramOffset(addr);
  if (off < ram.size())
    ram[off] = value;
}
