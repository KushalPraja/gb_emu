// gameboy cpu

#include <cstdint>
#include <cstdio>

#include "bus.h"
#include "types.h"

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
            f = value & 0xF0;
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
        for (auto &fn : opcodeTable) {
            fn = &undefined_opcode; // default to undefined opcode handler
        }

        // LUT for opcode handlers (only a few implemented for demonstration)

        opcodeTable[0x00] = &op_nop;     // NOP
        opcodeTable[0x06] = &op_ld_b_d8; // LD B, d8

        for (int opcode = 0x40; opcode <= 0x7F; opcode++) {
            if (opcode != 0x76) { // 0x76 is HALT, which we won't implement yet
                opcodeTable[opcode] = &op_ld_r_r;
            }
        }

        for (int opcode = 0x80; opcode <= 0xBF; opcode++) {
            opcodeTable[opcode] = &op_alu;
        }
    }

    u8 step() {
        u8 opcode = fetch8();
        return opcodeTable[opcode](*this, opcode);
    }

  private:
    Bus *bus;

    using opFn = u8 (*)(Core &, u8);
    opFn opcodeTable[256]{}; // LUT for opcode functions

    // fetch a byte from memory at the current PC and increment PC
    u8 fetch8() {
        u8 val = bus->read(regs.pc);
        regs.pc++;
        return val;
    }

    // little-endian fetch: low byte first, then high byte
    u16 fetch16() {
        u16 low = fetch8();
        u16 high = fetch8();
        return (high << 8) | low;
    }

    static u8 op_nop(Core &core, u8 opcode) { return 4; }

    static u8 undefined_opcode(Core &core, u8 opcode) {
        u16 addr = core.regs.pc - 1; // opcode was already consumed by step()
        std::printf("Undefined opcode: 0x%02X at PC: 0x%04X\n", (unsigned)opcode, (unsigned)addr);
        return 4;
    }

    void add8(u8 value) {
        u16 result = regs.a + value;
        regs.FlagH(((regs.a & 0xF) + (value & 0xF)) > 0xF); // original A
        regs.FlagC(result > 0xFF);
        regs.FlagN(false);
        regs.a = result & 0xFF;
        regs.FlagZ(regs.a == 0);
    }

    void adc8(u8 value) {
        u16 carry = regs.FlagC() ? 1 : 0;
        u16 result = regs.a + value + carry;
        regs.FlagH(((regs.a & 0xF) + (value & 0xF) + carry) > 0xF);
        regs.FlagC(result > 0xFF);
        regs.FlagN(false);
        regs.a = result & 0xFF;
        regs.FlagZ(regs.a == 0);
    }

    void sub8(u8 value) {
        regs.FlagH((regs.a & 0xF) < (value & 0xF));
        regs.FlagC(regs.a < value);
        regs.FlagN(true);
        regs.a = regs.a - value;
        regs.FlagZ(regs.a == 0);
    }

    void sbc8(u8 value) {
        u16 carry = regs.FlagC() ? 1 : 0;
        int result = regs.a - value - carry;
        regs.FlagH((regs.a & 0xF) < ((value & 0xF) + carry));
        regs.FlagC(result < 0);
        regs.FlagN(true);
        regs.a = result & 0xFF;
        regs.FlagZ(regs.a == 0);
    }

    void and8(u8 value) {
        regs.a &= value;
        regs.FlagC(false);
        regs.FlagZ(regs.a == 0);
        regs.FlagN(false);
        regs.FlagH(true);
    }

    void or8(u8 value) {
        regs.a |= value;
        regs.FlagC(false);
        regs.FlagZ(regs.a == 0);
        regs.FlagN(false);
        regs.FlagH(false);
    }

    void xor8(u8 value) {
        regs.a ^= value;
        regs.FlagC(false);
        regs.FlagZ(regs.a == 0);
        regs.FlagN(false);
        regs.FlagH(false);
    }

    void cp8(u8 value) {
        regs.FlagZ(regs.a == value);
        regs.FlagC(regs.a < value);
        regs.FlagN(true);
        regs.FlagH((regs.a & 0xF) < (value & 0xF));
    }

    u8 inc8(u8 value) {
        u8 result = value + 1;
        regs.FlagZ(result == 0);
        regs.FlagN(false);
        regs.FlagH((value & 0xF) == 0xF);
        return result;
    }

    u8 dec8(u8 value) {
        u8 result = value - 1;
        regs.FlagZ(result == 0);
        regs.FlagN(true);
        regs.FlagH((value & 0xF) == 0x00);
        return result;
    }

    u8 readReg(int i) {
        switch (i) {
        case 0:
            return regs.b;
        case 1:
            return regs.c;
        case 2:
            return regs.d;
        case 3:
            return regs.e;
        case 4:
            return regs.h;
        case 5:
            return regs.l;
        case 6:
            return bus->read(regs.hl()); // (HL)
        default:
            return regs.a; // default to A for invalid index
        }
    };

    u8 writeReg(int i, u8 value) {
        switch (i) {
        case 0:
            regs.b = value;
            break;
        case 1:
            regs.c = value;
            break;
        case 2:
            regs.d = value;
            break;
        case 3:
            regs.e = value;
            break;
        case 4:
            regs.h = value;
            break;
        case 5:
            regs.l = value;
            break;
        case 6:
            bus->write(regs.hl(), value);
            break; // (HL)
        default:
            regs.a = value;
            break; // default to A for invalid index
        }
        return value;
    };

    static u8 op_alu(Core &core, u8 opcode) {
        int x = (opcode >> 6) & 0x03; // bits 6-7
        int y = (opcode >> 3) & 0x07; // bits 3-5
        int z = opcode & 0x07;        // bits 0-2

        u8 value = core.readReg(z);
        switch (y) {
        case 0:
            core.add8(value);
            break; // ADD A, r[z]
        case 1:
            core.adc8(value);
            break; // ADC A, r[z]
        case 2:
            core.sub8(value);
            break; // SUB A, r[z]
        case 3:
            core.sbc8(value);
            break; // SBC A, r[z]
        case 4:
            core.and8(value);
            break; // AND A, r[z]
        case 5:
            core.xor8(value);
            break; // OR A, r[z]
        case 6:
            core.or8(value);
            break; // XOR A, r[z]
        case 7:
            core.cp8(value);
            break; // CP A, r[z]
        }
        return (z == 6) ? 8 : 4; // (HL) takes extra cycles
    }

    static u8 op_ld_r_r(Core &core, u8 opcode) {
        int dest = (opcode >> 3) & 0x07; // bits 3-5
        int src = opcode & 0x07;         // bits 0-2

        core.writeReg(dest, core.readReg(src));
        return (src == 6 || dest == 6) ? 8 : 4;
    }

    static u8 op_ld_b_d8(Core &core, u8 opcode) {
        u8 value = core.fetch8();
        core.regs.b = value;
        return 8;
    }
};

#endif