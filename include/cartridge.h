#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "types.h"
#include <string>
#include <vector>

// Cartridge handles ROM storage, the MBC (memory bank controller) and the
// optional battery-backed external RAM. Only plain 32KB ROMs and MBC1 are
// supported, which covers the Blargg test ROMs and most early DMG games.
class Cartridge {
public:
  bool load(const char *path);

  // 0x0000-0x7FFF
  u8 readRom(u16 addr) const;
  void writeRom(u16 addr, u8 value); // MBC control registers

  // 0xA000-0xBFFF
  u8 readRam(u16 addr) const;
  void writeRam(u16 addr, u8 value);

  const std::string &title() const { return romTitle; }

private:
  enum class Mbc { None, Mbc1 };

  std::vector<u8> rom;
  std::vector<u8> ram;
  std::string romTitle;

  Mbc mbc = Mbc::None;
  bool hasRam = false;

  // MBC1 state
  bool ramEnabled = false;
  u8 romBankLow = 1;  // lower 5 bits (0 is remapped to 1)
  u8 romBankHigh = 0; // upper 2 bits (RAM bank or ROM bank bits 5-6)
  u8 bankMode = 0;    // 0 = ROM banking, 1 = RAM banking

  u32 romBankCount = 0;
  u32 ramBankCount = 0;

  u32 romOffsetLow(u16 addr) const;  // for 0x0000-0x3FFF
  u32 romOffsetHigh(u16 addr) const; // for 0x4000-0x7FFF
  u32 ramOffset(u16 addr) const;
};

#endif // CARTRIDGE_H
