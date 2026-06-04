#include "display.h"

bool Display::init(const char *title, int scale) {
  win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         GB_W * scale, GB_H * scale + 24,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!win)
    return false;
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) // fall back to software (e.g. headless / no GPU)
    ren = SDL_CreateRenderer(win, -1, 0);
  if (!ren)
    return false;
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, GB_W, GB_H);
  SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest); // crisp pixels
  return tex != nullptr;
}

void Display::shutdown() {
  if (tex)
    SDL_DestroyTexture(tex);
  if (ren)
    SDL_DestroyRenderer(ren);
  if (win)
    SDL_DestroyWindow(win);
  tex = nullptr;
  ren = nullptr;
  win = nullptr;
}

Display::~Display() { shutdown(); }

void Display::updateGame(const u32 *fb) {
  SDL_UpdateTexture(tex, nullptr, fb, GB_W * sizeof(u32));
  haveFrame = true;
}

void Display::blitGame() {
  if (!haveFrame)
    return;
  int w, h;
  SDL_GetRendererOutputSize(ren, &w, &h);

  // Integer-friendly aspect-fit: largest GB-sized rect that fits the window.
  float scale = SDL_min((float)w / GB_W, (float)h / GB_H);
  int dw = (int)(GB_W * scale), dh = (int)(GB_H * scale);
  SDL_Rect dst{(w - dw) / 2, (h - dh) / 2, dw, dh};
  SDL_RenderCopy(ren, tex, nullptr, &dst);
}
