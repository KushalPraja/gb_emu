#include "bus.h"
#include "core.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int SCALE = 4;
constexpr double FRAME_MS = 1000.0 / 59.7275;

// Run without a window: step the CPU for a while and rely on serial output.
// Useful for the Blargg CPU test ROMs.
int runHeadless(Bus &bus) {
  Core cpu(bus);
  cpu.powerOn();
  for (long i = 0; i < 200'000'000L; i++)
    cpu.step();
  std::printf("\n");
  return 0;
}

// Run headless for a number of frames, then print the framebuffer as ASCII.
// Lets us verify PPU rendering without a display.
int runAscii(Bus &bus, int frames) {
  Core cpu(bus);
  cpu.powerOn();
  for (int f = 0; f < frames; f++) {
    int guard = 0;
    while (!bus.ppu.frameReady && guard < 140000)
      guard += cpu.step();
    bus.ppu.frameReady = false;
  }
  const u32 *fbuf = bus.ppu.framebuffer();
  const char *shades = " .:#"; // light -> dark, by luminance bucket
  for (int y = 0; y < PPU::HEIGHT; y += 3) {
    for (int x = 0; x < PPU::WIDTH; x += 2) {
      u32 p = fbuf[y * PPU::WIDTH + x];
      int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
      int lum = (r + g + b) / 3;
      int bucket = lum > 180 ? 0 : lum > 110 ? 1 : lum > 50 ? 2 : 3;
      std::putchar(shades[bucket]);
    }
    std::putchar('\n');
  }
  return 0;
}

// Update the host button bitmask from a key event.
void handleKey(SDL_Keycode key, bool down, u8 &buttons) {
  int bit = -1;
  switch (key) {
  case SDLK_z:
    bit = 0;
    break; // A
  case SDLK_x:
    bit = 1;
    break; // B
  case SDLK_BACKSPACE:
    bit = 2;
    break; // Select
  case SDLK_RETURN:
    bit = 3;
    break; // Start
  case SDLK_RIGHT:
    bit = 4;
    break;
  case SDLK_LEFT:
    bit = 5;
    break;
  case SDLK_UP:
    bit = 6;
    break;
  case SDLK_DOWN:
    bit = 7;
    break;
  default:
    return;
  }
  if (down)
    buttons |= (1 << bit);
  else
    buttons &= ~(1 << bit);
}
} // namespace

int main(int argc, char **argv) {
  const char *romPath = nullptr;
  bool headless = false;
  int asciiFrames = 0;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--headless") == 0)
      headless = true;
    else if (std::strcmp(argv[i], "--ascii") == 0)
      asciiFrames = (i + 1 < argc) ? std::atoi(argv[++i]) : 300;
    else
      romPath = argv[i];
  }
  if (!romPath) {
    std::printf("usage: %s [--headless] rom.gb\n", argv[0]);
    return 1;
  }

  Bus bus;
  if (!bus.loadRom(romPath)) {
    std::printf("could not open %s\n", romPath);
    return 1;
  }

  if (asciiFrames > 0)
    return runAscii(bus, asciiFrames);

  if (headless)
    return runHeadless(bus);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    std::printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow(
      "gb_emu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      PPU::WIDTH * SCALE, PPU::HEIGHT * SCALE, SDL_WINDOW_RESIZABLE);
  SDL_Renderer *renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_RenderSetLogicalSize(renderer, PPU::WIDTH, PPU::HEIGHT);
  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING, PPU::WIDTH, PPU::HEIGHT);

  // Audio: 44100 Hz, stereo float, fed via the queue API.
  SDL_AudioSpec want{}, have{};
  want.freq = APU::SAMPLE_RATE;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (audio)
    SDL_PauseAudioDevice(audio, 0);

  Core cpu(bus);
  cpu.powerOn();

  u8 buttons = 0;
  bool running = true;
  Uint64 frameStart = SDL_GetTicks64();

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        running = false;
      else if (e.type == SDL_KEYDOWN && !e.key.repeat)
        handleKey(e.key.keysym.sym, true, buttons);
      else if (e.type == SDL_KEYUP)
        handleKey(e.key.keysym.sym, false, buttons);
    }
    bus.setButtons(buttons);

    // Run the CPU until the PPU signals a completed frame. The cycle cap keeps
    // the loop progressing even while the LCD is disabled (no frames produced).
    int guard = 0;
    while (!bus.ppu.frameReady && guard < 140000)
      guard += cpu.step();
    bus.ppu.frameReady = false;

    SDL_UpdateTexture(texture, nullptr, bus.ppu.framebuffer(),
                      PPU::WIDTH * sizeof(u32));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);

    // Push this frame's audio, dropping it if the queue is already deep to keep
    // latency bounded.
    if (audio) {
      if (SDL_GetQueuedAudioSize(audio) < APU::SAMPLE_RATE * sizeof(float)) {
        SDL_QueueAudio(audio, bus.apu.samples.data(),
                       bus.apu.samples.size() * sizeof(float));
      }
    }
    bus.apu.samples.clear();

    // Pace to ~59.73 fps.
    Uint64 now = SDL_GetTicks64();
    double elapsed = (double)(now - frameStart);
    if (elapsed < FRAME_MS)
      SDL_Delay((Uint32)(FRAME_MS - elapsed));
    frameStart = SDL_GetTicks64();
  }

  if (audio)
    SDL_CloseAudioDevice(audio);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
