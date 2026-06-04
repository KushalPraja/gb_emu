#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "types.h"
#include <string>
#include <vector>

// Cartridge handles ROM storage, the MBC (memory bank controller) and the
// optional battery-backed external RAM. Plain 32KB ROMs plus MBC1, MBC2, MBC3
// (with real-time clock) and MBC5 are supported, which covers essentially the
// whole DMG/GBC commercial library.
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

  // Battery-backed RAM persistence. flushSave() writes the cart RAM to the
  // ".sav" file beside the ROM, but only for battery cartridges with unsaved
  // changes; it is a cheap no-op otherwise.
  void flushSave();

private:
  enum class Mbc { None, Mbc1, Mbc2, Mbc3, Mbc5 };

  std::vector<u8> rom;
  std::vector<u8> ram;
  std::string romTitle;

  Mbc mbc = Mbc::None;
  bool hasRam = false;

  // Battery-backed save support.
  bool hasBattery = false;
  bool ramDirty = false;   // RAM written since the last flush
  std::string savePath;    // "<rom>.sav"
  void loadSave();         // read savePath into ram on load

  // MBC1 state
  bool ramEnabled = false;
  u8 romBankLow = 1;  // lower 5 bits (0 is remapped to 1)
  u8 romBankHigh = 0; // upper 2 bits (RAM bank or ROM bank bits 5-6)
  u8 bankMode = 0;    // 0 = ROM banking, 1 = RAM banking

  // MBC3 state
  u8 mbc3RomBank = 1; // 7-bit ROM bank (0 is remapped to 1)
  u8 mbc3RamBank = 0; // 0-3 select RAM; 0x08-0x0C select an RTC register

  // MBC5 state
  u16 mbc5RomBank = 1; // 9-bit ROM bank (bank 0 is valid, not remapped)
  u8 mbc5RamBank = 0;  // 4-bit RAM bank

  u32 romBankCount = 0;
  u32 ramBankCount = 0;

  // MBC3 real-time clock. Only present on "timer" cartridge types (0x0F/0x10).
  // The live registers advance with wall-clock time; the latched copies are
  // what the game reads after writing the 0->1 latch sequence.
  bool hasRtc = false;
  u8 rtcS = 0, rtcM = 0, rtcH = 0, rtcDL = 0, rtcDH = 0; // live registers
  u8 rtcLatchS = 0, rtcLatchM = 0, rtcLatchH = 0, rtcLatchDL = 0,
     rtcLatchDH = 0;      // latched copies
  u8 rtcLatchPrev = 0xFF; // last value written to the latch register
  long long rtcBase = 0;  // unix time the live registers were last synced
  void updateRtc();       // advance live registers by elapsed real time

  u32 romOffsetLow(u16 addr) const;  // for 0x0000-0x3FFF
  u32 romOffsetHigh(u16 addr) const; // for 0x4000-0x7FFF
  u32 ramOffset(u16 addr) const;
};

#endif // CARTRIDGE_H
