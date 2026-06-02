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
  u16 ime = false; // Interrupt Master Enable flag

  Core(Bus &bus) : bus(&bus) {
    for (auto &fn : opcodeTable) {
      fn = &undefined_opcode; // default to undefined opcode handler
    }

    // LUT for opcode handlers (only a few implemented for demonstration)
    opcodeTable[0x00] = &op_nop; // NOP

    // INC r[y], DEC r[y], LD r[y], d8
    for (int opcode = 0x04; opcode <= 0x3C; opcode += 8) {
      opcodeTable[opcode] = &op_inc_r;
    }

    // DEC r[y]
    for (int opcode = 0x05; opcode <= 0x3D; opcode += 8) {
      opcodeTable[opcode] = &op_dec_r;
    }

    // LD r[y], d8
    for (int opcode = 0x06; opcode <= 0x3E; opcode += 8) {
      opcodeTable[opcode] = &op_ld_r_d8;
    }

    // LD dd, d16
    for (int opcode = 0x01; opcode <= 0x31; opcode += 0x10) {
      opcodeTable[opcode] = &op_ld_dd_d16;
    }

    // ALU A, d8
    for (int opcode = 0xC6; opcode <= 0xFE; opcode += 8) {
      opcodeTable[opcode] = &op_alu_d8;
    }

    // LD r[y], r[z]
    for (int opcode = 0x40; opcode <= 0x7F; opcode++) {
      if (opcode != 0x76) { // 0x76 is HALT, which we won't implement yet
        opcodeTable[opcode] = &op_ld_r_r;
      }
    }

    // ALU A, r[y]
    for (int opcode = 0x80; opcode <= 0xBF; opcode++) {
      opcodeTable[opcode] = &op_alu;
    }

    // INC rr, DEC rr
    for (int opcode = 0x03; opcode <= 0x33; opcode += 0x10) {
      opcodeTable[opcode] = &op_inc_rr; // INC rr
    }

    for (int opcode = 0x0B; opcode <= 0x3B; opcode += 0x10) {
      opcodeTable[opcode] = &op_dec_rr; // DEC rr
    }

    // ADD HL, rr
    for (int opcode = 0x09; opcode <= 0x39; opcode += 0x10) {
      opcodeTable[opcode] = &op_add_hl_rr; // ADD HL, rr
    }

    // PUSH dd
    for (int opcode = 0xC5; opcode <= 0xD5; opcode += 0x10) {
      opcodeTable[opcode] = &op_push_rr; // PUSH dd
    }

    // POP dd
    for (int opcode = 0xD1; opcode <= 0xE1; opcode += 0x10) {
      opcodeTable[opcode] = &op_pop_rr; // POP dd
    }

    opcodeTable[0xE8] = &op_add_sp_r8; // ADD SP, r8
    opcodeTable[0xC3] = &op_jp_a16;    // JP a16
    opcodeTable[0xE9] = &op_jp_hl;     // JP (HL)
    opcodeTable[0x18] = &op_jr_r8;     // JR r8
    opcodeTable[0xCD] = &op_call_a16;  // CALL a16
    opcodeTable[0xC9] = &op_ret;       // RET
    opcodeTable[0xD9] = &op_reti;      // RETI
    opcodeTable[0xC7] = &op_rst;       // RST
    opcodeTable[0xF3] = &op_di;        // DI
    opcodeTable[0xFB] = &op_ei;        // EI

    for (int opcode = 0xC2; opcode <= 0xDA; opcode += 8) {
      opcodeTable[opcode] = &op_jp_cc_a16; // JP cc, a16
    }

    for (int opcode = 0x20; opcode <= 0x38; opcode += 8) {
      opcodeTable[opcode] = &op_jr_cc_r8; // JR cc, r8
    }
    for (int opcode = 0xC4; opcode <= 0xDC; opcode += 8) {
      opcodeTable[opcode] = &op_call_cc_a16; // CALL cc, a16
    }

    for (int opcode = 0xC0; opcode <= 0xD8; opcode += 8) {
      opcodeTable[opcode] = &op_ret_cc; // RET cc
    }

    for (int opcode = 0xC7; opcode <= 0xFF; opcode += 8) {
      opcodeTable[opcode] = &op_rst; // RST n
    }

    opcodeTable[0x0A] = &op_ld_a_bc;
    opcodeTable[0x1A] = &op_ld_a_de;
    opcodeTable[0x02] = &op_ld_bc_a;
    opcodeTable[0x12] = &op_ld_de_a;
    opcodeTable[0x22] = &op_ld_hli_a;
    opcodeTable[0x32] = &op_ld_hld_a;
    opcodeTable[0x2A] = &op_ld_a_hli;
    opcodeTable[0x3A] = &op_ld_a_hld;
    opcodeTable[0xE0] = &op_ldh_a8_a;
    opcodeTable[0xF0] = &op_ldh_a_a8;
    opcodeTable[0xE2] = &op_ld_c_a;
    opcodeTable[0xF2] = &op_ld_a_c;
    opcodeTable[0xEA] = &op_ld_a16_a;
    opcodeTable[0xFA] = &op_ld_a_a16;
    opcodeTable[0xF9] = &op_ld_sp_hl;
    opcodeTable[0xF8] = &op_ld_hl_sp_r8;
    opcodeTable[0x08] = &op_ld_a16_sp;
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

  static u8 undefined_opcode(Core &core, u8 opcode) {
    u16 addr = core.regs.pc - 1; // opcode was already consumed by step()
    std::printf("Undefined opcode: 0x%02X at PC: 0x%04X\n", (unsigned)opcode,
                (unsigned)addr);
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

  void aluOp(int y, u8 value) {
    switch (y) {
    case 0:
      add8(value);
      break; // ADD A, r[y]
    case 1:
      adc8(value);
      break; // ADC A, r[y]
    case 2:
      sub8(value);
      break; // SUB A, r[y]
    case 3:
      sbc8(value);
      break; // SBC A, r[y]
    case 4:
      and8(value);
      break; // AND A, r[y]
    case 5:
      xor8(value);
      break; // XOR A, r[y]
    case 6:
      or8(value);
      break; // OR A, r[y]
    case 7:
      cp8(value);
      break; // CP A, r[y]
    }
  }

  // Handle LD d16
  void setReg16(int i, u16 value) {
    switch (i) {
    case 0:
      regs.bc(value);
      break;
    case 1:
      regs.de(value);
      break;
    case 2:
      regs.hl(value);
      break;
    case 3:
      regs.sp = value;
      break;
    default:
      break; // invalid index, do nothing
    }
  }

  void getReg16(int i, u16 &value) {
    switch (i) {
    case 0:
      value = regs.bc();
      break;
    case 1:
      value = regs.de();
      break;
    case 2:
      value = regs.hl();
      break;
    case 3:
      value = regs.sp;
      break;
    default:
      value = 0; // invalid index, return 0
      break;
    }
  }

  u16 getReg16Stack(int i) {
    switch (i) {
    case 0:
      return regs.bc();
    case 1:
      return regs.de();
    case 2:
      return regs.hl();
    case 3:
      return regs.sp;
    default:
      return 0; // invalid index, return 0
    }
  }

  void setReg16Stack(int i, u16 value) {
    switch (i) {
    case 0:
      regs.bc(value);
      break;
    case 1:
      regs.de(value);
      break;
    case 2:
      regs.hl(value);
      break;
    case 3:
      regs.sp = value;
      break;
    default:
      break; // invalid index, do nothing
    }
  }

  // Stack operations
  void push16(u16 value) {
    bus->write(--regs.sp, (value >> 8) & 0xFF); // high byte
    bus->write(--regs.sp, value & 0xFF);        // low byte
  }

  void pop16(u16 &value) {
    value = bus->read(regs.sp++);
    value |= (bus->read(regs.sp++) << 8);
  }

  bool checkCondition(int condition) {
    switch (condition) {
    case 0: // NZ
      return !regs.FlagZ();
    case 1: // Z
      return regs.FlagZ();
    case 2: // NC
      return !regs.FlagC();
    default: // C
      return regs.FlagC();
    }
  }

  // Handle ALU operations (ADD, ADC, SUB, SBC, AND, XOR, OR, CP)
  static u8 op_alu(Core &core, u8 opcode) {
    int y = (opcode >> 3) & 0x07; // bits 3-5
    int z = opcode & 0x07;        // bits 0-2
    core.aluOp(y, core.readReg(z));
    return (z == 6) ? 8 : 4; // (HL) takes extra cycles
  }

  // Handle LD r[y], r[z] instructions (0x40-0x7F, excluding 0x76)
  static u8 op_ld_r_r(Core &core, u8 opcode) {
    int dest = (opcode >> 3) & 0x07; // bits 3-5
    int src = opcode & 0x07;         // bits 0-2

    core.writeReg(dest, core.readReg(src));
    return (src == 6 || dest == 6) ? 8 : 4;
  }

  // Handle ALU A, d8
  static u8 op_alu_d8(Core &core, u8 opcode) {
    int y = (opcode >> 3) & 0x07; // bits 3-5
    core.aluOp(y, core.fetch8());
    return 8;
  }

  // Inc and Dec r[y]
  static u8 op_inc_r(Core &core, u8 opcode) {
    int y = (opcode >> 3) & 0x07; // bits 3-5
    u8 value = core.readReg(y);
    core.writeReg(y, core.inc8(value));
    return (y == 6) ? 12 : 4; // (HL) takes extra cycles
  }

  static u8 op_dec_r(Core &core, u8 opcode) {
    int y = (opcode >> 3) & 0x07; // bits 3-5
    u8 value = core.readReg(y);
    core.writeReg(y, core.dec8(value));
    return (y == 6) ? 12 : 4; // (HL) takes extra cycles
  }

  // Handle LD r[y], d8 instructions (0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36,
  // 0x3E)
  static u8 op_ld_r_d8(Core &core, u8 opcode) {
    int y = (opcode >> 3) & 0x07; // bits 3-5
    core.writeReg(y, core.fetch8());
    return (y == 6) ? 12 : 8; // (HL) takes extra cycles
  }

  // Handle LD dd, d16 instructions (0x01, 0x11, 0x21, 0x31)
  static u8 op_ld_dd_d16(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits
    u16 value = core.fetch16();
    core.setReg16(dd, value);
    return 12;
  }

  static u8 op_inc_rr(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits 4-5
    u16 value;
    core.getReg16(dd, value);
    value++;
    core.setReg16(dd, value);
    return 8;
  }

  static u8 op_dec_rr(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits 4-5
    u16 value;
    core.getReg16(dd, value);
    value--;
    core.setReg16(dd, value);
    return 8;
  }

  static u8 op_add_hl_rr(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits 4-5
    u16 hl = core.regs.hl();
    u16 value;
    core.getReg16(dd, value);
    u32 result = hl + value;
    core.regs.FlagH(((hl & 0xFFF) + (value & 0xFFF)) > 0xFFF);
    core.regs.FlagC(result > 0xFFFF);
    core.regs.FlagN(false);
    core.regs.hl(result & 0xFFFF);
    return 8;
  }

  static u8 op_add_sp_r8(Core &core, u8 opcode) {
    u8 raw = core.fetch8();
    std::int8_t r8 = static_cast<std::int8_t>(raw);
    u16 sp = core.regs.sp;
    core.regs.FlagZ(false);
    core.regs.FlagN(false);
    core.regs.FlagH(((sp & 0xF) + (r8 & 0xF)) > 0x0F);
    core.regs.FlagC((sp & 0xFF) + (r8 & 0xFF) > 0xFF);
    core.regs.sp = (u16)(sp + r8); // jump to signed address
    return 16;
  }

  static u8 op_push_rr(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits 4-5
    u16 value = core.getReg16Stack(dd);
    core.bus->write(--core.regs.sp, (value >> 8) & 0xFF); // high byte
    core.bus->write(--core.regs.sp, value & 0xFF);        // low byte
    return 16;
  }

  static u8 op_pop_rr(Core &core, u8 opcode) {
    int dd = (opcode >> 4) & 0x03; // bits 4-5
    u16 value = core.bus->read(core.regs.sp++);
    value |= (core.bus->read(core.regs.sp++) << 8);
    core.setReg16Stack(dd, value);
    return 12;
  }

  static u8 op_nop(Core &core, u8 opcode) { return 4; }


  static u8 op_jp_a16(Core &core, u8 opcode) {
    u16 addr = core.fetch16();
    core.regs.pc = addr;
    return 16;
  }

  static u8 op_jp_cc_a16(Core &core, u8 opcode) {
    int condition = (opcode >> 3) & 0x03; // bits 3-4
    u16 addr = core.fetch16();
    if (core.checkCondition(condition)) {
      core.regs.pc = addr;
      return 16;
    }
    return 12; // condition not met, still need to consume the address
  }

  static u8 op_jp_hl(Core &core, u8 opcode) {
    core.regs.pc = core.regs.hl();
    return 4;
  }

  static u8 op_jr_r8(Core &core, u8 opcode) {
    std::int8_t offset = static_cast<std::int8_t>(core.fetch8());
    core.regs.pc += offset;
    return 12;
  }

  static u8 op_jr_cc_r8(Core &core, u8 opcode) {
    int condition = (opcode >> 3) & 0x03; // bits 3-4
    std::int8_t offset = static_cast<std::int8_t>(core.fetch8());
    if (core.checkCondition(condition)) {
      core.regs.pc += offset;
      return 12;
    }
    return 8; // condition not met, still need to consume the offset
  }

  static u8 op_call_a16(Core &core, u8 opcode) {
    u16 addr = core.fetch16();
    core.push16(core.regs.pc);
    core.regs.pc = addr;
    return 24;
  }

  static u8 op_call_cc_a16(Core &core, u8 opcode) {
    int condition = (opcode >> 3) & 0x03; // bits 3-4
    u16 addr = core.fetch16();
    if (core.checkCondition(condition)) {
      core.push16(core.regs.pc);
      core.regs.pc = addr;
      return 24;
    }
    return 12; // condition not met, still need to consume the address
  }

  // returns

  static u8 op_ret(Core &core, u8 opcode) {
    u16 addr;
    core.pop16(addr);
    core.regs.pc = addr;
    return 16;
  }

  static u8 op_ret_cc(Core &core, u8 opcode) {
    int condition = (opcode >> 3) & 0x03; // bits 3-4
    if (core.checkCondition(condition)) {
      u16 addr;
      core.pop16(addr);
      core.regs.pc = addr;
      return 20;
    }
    return 8; // condition not met, no stack operation
  }

  static u8 op_reti(Core &core, u8 opcode) {
    u16 addr;
    core.pop16(addr);
    core.regs.pc = addr;
    return 16;
  }

  // restarts
  static u8 op_rst(Core &core, u8 opcode) {
    int n = (opcode >> 3) & 0x07; // bits 3-5
    core.push16(core.regs.pc);
    core.regs.pc = n * 8;
    return 16;
  }

  static u8 op_di(Core &core, u8 opcode) {
    core.ime = false;
    return 4;
  }

  static u8 op_ei(Core &core, u8 opcode) {
    core.ime = true;
    return 4;
  };

  // implement more laods

  static u8 op_ld_a_bc(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(core.regs.bc());
    return 8;
  }

  static u8 op_ld_a_de(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(core.regs.de());
    return 8;
  }

  static u8 op_ld_bc_a(Core &core, u8 opcode) {
    core.bus->write(core.regs.bc(), core.regs.a);
    return 8;
  };

  static u8 op_ld_de_a(Core &core, u8 opcode) {
    core.bus->write(core.regs.de(), core.regs.a);
    return 8;
  };

  // HIL instructions

  static u8 op_ld_hli_a(Core &core, u8 opcode) {
    core.bus->write(core.regs.hl(), core.regs.a);
    core.regs.hl(core.regs.hl() + 1);
    return 8;
  };

  static u8 op_ld_hld_a(Core &core, u8 opcode) {
    core.bus->write(core.regs.hl(), core.regs.a);
    core.regs.hl(core.regs.hl() - 1);
    return 8;
  }

  static u8 op_ld_a_hli(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(core.regs.hl());
    core.regs.hl(core.regs.hl() + 1);
    return 8;
  };

  static u8 op_ld_a_hld(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(core.regs.hl());
    core.regs.hl(core.regs.hl() - 1);
    return 8;
  }

  static u8 op_ldh_a8_a(Core &core, u8 opcode) {
    u8 offset = core.fetch8();
    core.bus->write(0xFF00 + offset, core.regs.a);
    return 12;
  };

  static u8 op_ldh_a_a8(Core &core, u8 opcode) {
    u8 offset = core.fetch8();
    core.regs.a = core.bus->read(0xFF00 + offset);
    return 12;
  }

  static u8 op_ld_c_a(Core &core, u8 opcode) {
    core.bus->write(0xFF00 + core.regs.c, core.regs.a);
    return 8;
  }

  static u8 op_ld_a_c(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(0xFF00 + core.regs.c);
    return 8;
  }

  static u8 op_ld_a16_a(Core &core, u8 opcode) {
    core.bus->write(core.fetch16(), core.regs.a);
    return 16;
  }

  static u8 op_ld_a_a16(Core &core, u8 opcode) {
    core.regs.a = core.bus->read(core.fetch16());
    return 16;
  }

  static u8 op_ld_sp_hl(Core &core, u8 opcode) {
    core.regs.sp = core.regs.hl();
    return 8;
  }

  static u8 op_ld_hl_sp_r8(Core &core, u8 opcode) {
    std::int8_t offset = static_cast<std::int8_t>(core.fetch8());
    core.regs.hl((u16)(core.regs.sp + offset));
    return 12;
  }

  static u8 op_ld_a16_sp(Core &core, u8 opcode) {
    core.bus->write(core.fetch16(), core.regs.sp & 0xFF); // low byte
    core.bus->write(core.fetch16() + 1,
                    (core.regs.sp >> 8) & 0xFF); // high byte
    return 20;
  }

};
//

#endif