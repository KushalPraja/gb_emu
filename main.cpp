#include "bus.h"
#include "core.h"

int main(int argc, char *argv[]) {
    Bus bus;
    Core core(bus);

    // Example: Load a simple program into memory (NOP, LD B, 0x42, LD B, C, XOR A)
    bus.write(0x0000, 0x00); // NOP
    bus.write(0x0001, 0x06); // LD B, d8
    bus.write(0x0002, 0x42); // d8 = 0x42
    bus.write(0x0003, 0x48); // LD C, B
    bus.write(0x0004, 0xAF); // XOR A

    // Run the program for a few steps
    for (int i = 0; i < 4; i++) {
        u8 cycles = core.step();
        std::printf("Executed instruction at PC: 0x%04X, Cycles: %d\n", (unsigned)core.regs.pc - 1,
                    (unsigned)cycles);
    }

    // Check register values after execution
    std::printf("Register B: 0x%02X\n", (unsigned)core.regs.b);
    std::printf("Register A: 0x%02X\n", (unsigned)core.regs.a);

    return 0;
}
