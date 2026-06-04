#include "cartridge.h"
#include <cstdio>
#include <ctime>
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
  case 0x05: // MBC2
  case 0x06: // MBC2 + battery
    mbc = Mbc::Mbc2;
    break;
  case 0x0F: // MBC3 + timer + battery
  case 0x10: // MBC3 + timer + RAM + battery
    mbc = Mbc::Mbc3;
    hasRtc = true;
    break;
  case 0x11: // MBC3
  case 0x12: // MBC3 + RAM
  case 0x13: // MBC3 + RAM + battery
    mbc = Mbc::Mbc3;
    break;
  case 0x19: // MBC5
  case 0x1A: // MBC5 + RAM
  case 0x1B: // MBC5 + RAM + battery
  case 0x1C: // MBC5 + rumble
  case 0x1D: // MBC5 + rumble + RAM
  case 0x1E: // MBC5 + rumble + RAM + battery
    mbc = Mbc::Mbc5;
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

  if (mbc == Mbc::Mbc2) {
    // MBC2 has 512 x 4-bit RAM built into the mapper; the size header is 0.
    hasRam = true;
    ramBankCount = 0; // not bank-switched
    ram.assign(512, 0);
  } else {
    hasRam = ramBankCount > 0;
    ram.assign((size_t)ramBankCount * 0x2000, 0);
  }

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

  rtcBase = (long long)std::time(nullptr);

  savePath = std::filesystem::path(path).replace_extension(".sav").string();
  ramDirty = false;
  if (hasBattery && (hasRam || hasRtc))
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
  // RTC cartridges append an 18-byte clock block after the RAM image.
  if (hasRtc) {
    u8 buf[18];
    if (std::fread(buf, 1, sizeof(buf), f) == sizeof(buf)) {
      rtcS = buf[0];
      rtcM = buf[1];
      rtcH = buf[2];
      rtcDL = buf[3];
      rtcDH = buf[4];
      rtcLatchS = buf[5];
      rtcLatchM = buf[6];
      rtcLatchH = buf[7];
      rtcLatchDL = buf[8];
      rtcLatchDH = buf[9];
      rtcBase = 0;
      for (int i = 0; i < 8; i++)
        rtcBase |= (long long)buf[10 + i] << (8 * i);
      updateRtc(); // catch the clock up to the present
    }
  }
  std::fclose(f);
  std::printf("Loaded save: %s (%zu bytes)\n", savePath.c_str(), got);
}

void Cartridge::flushSave() {
  if (!hasBattery || !ramDirty || (!hasRam && !hasRtc))
    return;
  std::FILE *f = std::fopen(savePath.c_str(), "wb");
  if (!f) {
    std::printf("could not write save %s\n", savePath.c_str());
    return;
  }
  if (hasRam)
    std::fwrite(ram.data(), 1, ram.size(), f);
  if (hasRtc) {
    updateRtc();
    u8 buf[18] = {rtcS,       rtcM,       rtcH,       rtcDL,     rtcDH,
                  rtcLatchS,  rtcLatchM,  rtcLatchH,  rtcLatchDL, rtcLatchDH};
    for (int i = 0; i < 8; i++)
      buf[10 + i] = (u8)((rtcBase >> (8 * i)) & 0xFF);
    std::fwrite(buf, 1, sizeof(buf), f);
  }
  std::fclose(f);
  ramDirty = false;
}

void Cartridge::updateRtc() {
  long long now = (long long)std::time(nullptr);
  if (rtcDH & 0x40) { // clock halted: keep the base in sync, don't advance
    rtcBase = now;
    return;
  }
  long long elapsed = now - rtcBase;
  if (elapsed <= 0)
    return;
  rtcBase = now;

  long long days = rtcDL | ((rtcDH & 0x01) << 8);
  long long secs = rtcS + rtcM * 60LL + rtcH * 3600LL + days * 86400LL + elapsed;

  rtcS = (u8)(secs % 60);
  secs /= 60;
  rtcM = (u8)(secs % 60);
  secs /= 60;
  rtcH = (u8)(secs % 24);
  secs /= 24;
  days = secs;
  if (days > 0x1FF) {
    days &= 0x1FF;
    rtcDH |= 0x80; // day counter carry (sticky until cleared by the game)
  }
  rtcDL = (u8)(days & 0xFF);
  rtcDH = (u8)((rtcDH & 0xC0) | ((days >> 8) & 0x01));
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
  switch (mbc) {
  case Mbc::Mbc2:
    bank = (romBankLow & 0x0F);
    if (bank == 0)
      bank = 1;
    break;
  case Mbc::Mbc3:
    bank = mbc3RomBank == 0 ? 1 : mbc3RomBank;
    break;
  case Mbc::Mbc5:
    bank = mbc5RomBank; // bank 0 is valid here, no remapping
    break;
  default: { // Mbc1
    u32 low = romBankLow & 0x1F;
    if (low == 0)
      low = 1;
    bank = ((u32)romBankHigh << 5) | low;
    break;
  }
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
  switch (mbc) {
  case Mbc::Mbc1:
    if (addr < 0x2000) {
      ramEnabled = (value & 0x0F) == 0x0A;
    } else if (addr < 0x4000) {
      romBankLow = value & 0x1F;
    } else if (addr < 0x6000) {
      romBankHigh = value & 0x03;
    } else {
      bankMode = value & 0x01;
    }
    break;

  case Mbc::Mbc2:
    // Below 0x4000 a single address bit selects which register is written:
    // bit 8 clear = RAM enable, bit 8 set = 4-bit ROM bank number.
    if (addr < 0x4000) {
      if (addr & 0x0100)
        romBankLow = value & 0x0F;
      else
        ramEnabled = (value & 0x0F) == 0x0A;
    }
    break;

  case Mbc::Mbc3:
    if (addr < 0x2000) {
      ramEnabled = (value & 0x0F) == 0x0A; // also enables RTC registers
    } else if (addr < 0x4000) {
      mbc3RomBank = value & 0x7F; // full 7 bits, 0 is remapped to 1 on read
    } else if (addr < 0x6000) {
      mbc3RamBank = value; // 0x00-0x03 RAM bank, 0x08-0x0C RTC register
    } else {
      // 0x6000-0x7FFF: writing 0x00 then 0x01 latches the clock.
      if (rtcLatchPrev == 0x00 && value == 0x01) {
        updateRtc();
        rtcLatchS = rtcS;
        rtcLatchM = rtcM;
        rtcLatchH = rtcH;
        rtcLatchDL = rtcDL;
        rtcLatchDH = rtcDH;
      }
      rtcLatchPrev = value;
    }
    break;

  case Mbc::Mbc5:
    if (addr < 0x2000) {
      ramEnabled = (value & 0x0F) == 0x0A;
    } else if (addr < 0x3000) {
      mbc5RomBank = (mbc5RomBank & 0x100) | value; // low 8 bits
    } else if (addr < 0x4000) {
      mbc5RomBank = (mbc5RomBank & 0x0FF) | ((u16)(value & 0x01) << 8); // bit 8
    } else if (addr < 0x6000) {
      mbc5RamBank = value & 0x0F; // bit 3 doubles as rumble on rumble carts
    }
    break;

  case Mbc::None:
    break;
  }
}

u32 Cartridge::ramOffset(u16 addr) const {
  // MBC2's 512 x 4-bit RAM is not bank-switched and echoes every 0x200 bytes.
  if (mbc == Mbc::Mbc2)
    return (addr - 0xA000u) & 0x01FF;

  u32 bank;
  if (mbc == Mbc::Mbc3)
    bank = mbc3RamBank & 0x03;
  else if (mbc == Mbc::Mbc5)
    bank = mbc5RamBank;
  else
    bank = (mbc == Mbc::Mbc1 && bankMode == 1) ? romBankHigh : 0;
  if (ramBankCount > 0)
    bank &= (ramBankCount - 1);
  return bank * 0x2000u + (addr - 0xA000u);
}

u8 Cartridge::readRam(u16 addr) const {
  if (!hasRam || (mbc != Mbc::None && !ramEnabled))
    return 0xFF;
  // MBC3 RTC register select (0x08-0x0C) returns the latched clock value.
  if (mbc == Mbc::Mbc3 && mbc3RamBank >= 0x08) {
    switch (mbc3RamBank) {
    case 0x08:
      return rtcLatchS;
    case 0x09:
      return rtcLatchM;
    case 0x0A:
      return rtcLatchH;
    case 0x0B:
      return rtcLatchDL;
    case 0x0C:
      return rtcLatchDH;
    default:
      return 0xFF;
    }
  }
  u32 off = ramOffset(addr);
  if (off < ram.size()) {
    // MBC2 RAM is 4-bit; the upper nibble reads back as 1.
    if (mbc == Mbc::Mbc2)
      return ram[off] | 0xF0;
    return ram[off];
  }
  return 0xFF;
}

void Cartridge::writeRam(u16 addr, u8 value) {
  if (!hasRam || (mbc != Mbc::None && !ramEnabled))
    return;
  // MBC3 RTC register select (0x08-0x0C): write the live clock register.
  if (mbc == Mbc::Mbc3 && mbc3RamBank >= 0x08) {
    updateRtc(); // resync so the write is not immediately overwritten
    switch (mbc3RamBank) {
    case 0x08:
      rtcS = value & 0x3F;
      break;
    case 0x09:
      rtcM = value & 0x3F;
      break;
    case 0x0A:
      rtcH = value & 0x1F;
      break;
    case 0x0B:
      rtcDL = value;
      break;
    case 0x0C:
      rtcDH = value & 0xC1;
      break;
    }
    ramDirty = true;
    return;
  }
  u32 off = ramOffset(addr);
  if (off < ram.size()) {
    if (mbc == Mbc::Mbc2)
      value &= 0x0F; // only the low nibble is stored
    if (ram[off] != value)
      ramDirty = true;
    ram[off] = value;
  }
}
