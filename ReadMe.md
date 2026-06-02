# GbEmu

An emulator for the Nintendo Game Boy written in pure C++. This is a work in progress, but the CPU is mostly complete and can run simple test ROMs.

## Current Status:

[x] CPU (interrupts, timers, and all opcodes) \
[ ] PPU \
[ ] APU \
[ ] Input

## Resources:

- Pan Docs: https://gbdev.io/pandocs/
- Opcode: https://gbdev.io/pandocs/CPU_Instruction_Set.html
- Interrupts: https://gbdev.io/pandocs/Interrupts.html

## Building and Running:

1. Clone the repository and navigate to the project directory.

2. Build the project using CMake:
   ```bash
   mkdir build
   cmake -S . -B build
   cmake --build build 
   ```

3. Run the emulator (replace `path/to/rom.gb` with the actual path to your rom file):

   ```bash
   ./build/gb_emu path/to/rom.gb
   ```

---