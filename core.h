// gameboy cpu

#include "bus.h"
#include "types.h"
#include <cstdint>

#ifndef CORE_H
#define CORE_H

class Core {
  public:
    struct Registers {
        u8 a, f, b, c, d, e, h, l;
        u16 sp, pc;

        // Getters for combined registers
        u16 af() const { return (a << 8) | f; }
        u16 bc() const { return (b << 8) | c; }
        u16 de() const { return (d << 8) | e; }
        u16 hl() const { return (h << 8) | l; }

        // Setters for combined registers

        void af(u16 value) {
            a = (value >> 8) & 0xFF;
            f = value & 0xFF;
        }

        void bc(u16 value) {
            b = (value >> 8) & 0xFF;
            c = value & 0xFF;
        }

        void de(u16 value) {
            d = (value >> 8) & 0xFF;
            e = value & 0xFF;
        }

        void hl(u16 value) {
            h = (value >> 8) & 0xFF;
            l = value & 0xFF;
        }

        // Flag manipulation

        bool FlagZ() const { return (f & 0x80) != 0; }
        bool FlagN() const { return (f & 0x40) != 0; }
        bool FlagH() const { return (f & 0x20) != 0; }
        bool FlagC() const { return (f & 0x10) != 0; }

        // Flag write

        void FlagZ(bool value) { SetFlag(0x80, value); }
        void FlagN(bool value) { SetFlag(0x40, value); }
        void FlagH(bool value) { SetFlag(0x20, value); }
        void FlagC(bool value) { SetFlag(0x10, value); }

      private:
        void SetFlag(u8 flag, bool value) {
            if (value)
                f |= flag;
            else
                f &= ~flag;
        }
    };

    Registers regs{};

    Core(Bus &bus) : bus(&bus) {
        
    }

  private:
    Bus *bus;
};

#endif