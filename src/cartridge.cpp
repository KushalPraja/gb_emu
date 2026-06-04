#include "cartridge.h"
#include <cstdio>
#include <filesystem>

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
  case 0x0F: // MBC3 + timer + battery
  case 0x10: // MBC3 + timer + RAM + battery
  case 0x11: // MBC3
  case 0x12: // MBC3 + RAM
  case 0x13: // MBC3 + RAM + battery
    mbc = Mbc::Mbc3;
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

  // Cartridge types with a battery keep their RAM across power cycles.
  switch (type) {
  case 0x03: // MBC1 + RAM + battery
  case 0x06: // MBC2 + battery
  case 0x09: // ROM + RAM + battery
  case 0x0D: // MMM01 + RAM + battery
  case 0x0F: // MBC3 + timer + battery
  case 0x10: // MBC3 + timer + RAM + battery
  case 0x13: // MBC3 + RAM + battery
  case 0x1B: // MBC5 + RAM + battery
  case 0x1E: // MBC5 + rumble + RAM + battery
    hasBattery = true;
    break;
  default:
    hasBattery = false;
    break;
  }

  savePath = std::filesystem::path(path).replace_extension(".sav").string();
  ramDirty = false;
  if (hasBattery && hasRam)
    loadSave();

  std::printf("Loaded ROM: \"%s\"  type=0x%02X  %u ROM banks  %u RAM banks%s\n",
              romTitle.c_str(), (unsigned)type, romBankCount, ramBankCount,
              hasBattery ? "  [battery]" : "");
  return true;
}

void Cartridge::loadSave() {
  std::FILE *f = std::fopen(savePath.c_str(), "rb");
  if (!f)
    return; // no save yet — fresh cartridge
  size_t got = std::fread(ram.data(), 1, ram.size(), f);
  std::fclose(f);
  std::printf("Loaded save: %s (%zu bytes)\n", savePath.c_str(), got);
}

void Cartridge::flushSave() {
  if (!hasBattery || !hasRam || !ramDirty)
    return;
  std::FILE *f = std::fopen(savePath.c_str(), "wb");
  if (!f) {
    std::printf("could not write save %s\n", savePath.c_str());
    return;
  }
  std::fwrite(ram.data(), 1, ram.size(), f);
  std::fclose(f);
  ramDirty = false;
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
  u32 bank;
  if (mbc == Mbc::Mbc3) {
    bank = mbc3RomBank == 0 ? 1 : mbc3RomBank;
  } else {
    u32 low = romBankLow & 0x1F;
    if (low == 0)
      low = 1;
    bank = ((u32)romBankHigh << 5) | low;
  }
  bank &= (romBankCount - 1);
  return bank * 0x4000u + (addr - 0x4000u);
}

u8 Cartridge::readRom(u16 addr) const {
  u32 off = addr < 0x4000 ? romOffsetLow(addr) : romOffsetHigh(addr);
  if (off < rom.size())
    return rom[off];
  return 0xFF;
}

void Cartridge::writeRom(u16 addr, u8 value) {
  if (mbc == Mbc::Mbc1) {
    if (addr < 0x2000) {
      ramEnabled = (value & 0x0F) == 0x0A;
    } else if (addr < 0x4000) {
      romBankLow = value & 0x1F;
    } else if (addr < 0x6000) {
      romBankHigh = value & 0x03;
    } else {
      bankMode = value & 0x01;
    }
  } else if (mbc == Mbc::Mbc3) {
    if (addr < 0x2000) {
      ramEnabled = (value & 0x0F) == 0x0A; // also enables RTC registers
    } else if (addr < 0x4000) {
      mbc3RomBank = value & 0x7F; // full 7 bits, 0 is remapped to 1 on read
    } else if (addr < 0x6000) {
      mbc3RamBank = value; // 0x00-0x03 RAM bank, 0x08-0x0C RTC register
    } else {
      // 0x6000-0x7FFF: RTC latch. No RTC implemented, so nothing to latch.
    }
  }
}

u32 Cartridge::ramOffset(u16 addr) const {
  u32 bank;
  if (mbc == Mbc::Mbc3)
    bank = mbc3RamBank & 0x03;
  else
    bank = (mbc == Mbc::Mbc1 && bankMode == 1) ? romBankHigh : 0;
  if (ramBankCount > 0)
    bank &= (ramBankCount - 1);
  return bank * 0x2000u + (addr - 0xA000u);
}

u8 Cartridge::readRam(u16 addr) const {
  if (!hasRam || (mbc != Mbc::None && !ramEnabled))
    return 0xFF;
  // MBC3 RTC register select (0x08-0x0C): no RTC implemented, read back 0x00.
  if (mbc == Mbc::Mbc3 && mbc3RamBank >= 0x08)
    return 0x00;
  u32 off = ramOffset(addr);
  if (off < ram.size())
    return ram[off];
  return 0xFF;
}

void Cartridge::writeRam(u16 addr, u8 value) {
  if (!hasRam || (mbc != Mbc::None && !ramEnabled))
    return;
  // MBC3 RTC register select (0x08-0x0C): no RTC implemented, drop the write.
  if (mbc == Mbc::Mbc3 && mbc3RamBank >= 0x08)
    return;
  u32 off = ramOffset(addr);
  if (off < ram.size()) {
    if (ram[off] != value)
      ramDirty = true;
    ram[off] = value;
  }
}
