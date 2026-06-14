# GbEmu

An emulator for the Nintendo Game Boy written in pure C++. This is a work in progress, but the CPU is mostly complete and can run simple test ROMs.

**Watch the Demo:**

<p>
  <a href="https://drive.google.com/file/d/1J76Y_s49JQinETjMuuR8-7UFh_8zIgrn/view?usp=sharing">
    <img src="https://github.com/user-attachments/assets/24fe8c49-2806-4dd5-a6ac-22cb782a0faa" width="800" alt="Demo Video">
  </a>
</p>


## Current Status:

[x] CPU (interrupts, timers, and all opcodes) \
[x] PPU (background, window, sprites, LCD/STAT interrupts) \
[x] APU (2 pulse, wave, noise channels with audio output) \
[x] Input \
[x] Cartridges (plain 32KB + MBC1,3,5 , battery RAM and saves)

## Resources:

- Pan Docs: https://gbdev.io/pandocs/
- Opcode: https://gbdev.io/pandocs/CPU_Instruction_Set.html
- Interrupts: https://gbdev.io/pandocs/Interrupts.html

## Dependencies:

- A C++17 compiler and CMake (>= 3.16)
- [SDL2](https://www.libsdl.org/) for video, input and audio
  (`sudo apt install libsdl2-dev`)

## Building and Running:

1. Clone the repository and navigate to the project directory.

2. Build the project using CMake:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

3. Run the emulator (replace `path/to/rom.gb` with the actual path to your rom file):

   ```bash
   ./build/gb_emu path/to/rom.gb
   ```

   Extra modes:
   - `--headless rom.gb` — run without a window, printing serial output
     (used for the Blargg CPU test ROMs in `tests/`).
   - `--ascii N rom.gb` — run `N` frames headless then print the LCD as ASCII
     (a quick way to check rendering without a display).

## Controls:

| Key            | Game Boy button |
| -------------- | --------------- |
| Z              | A               |
| X              | B               |
| Enter          | Start           |
| Backspace      | Select          |
| Arrow keys     | D-pad           |

---
