#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"
#include <SDL2/SDL.h>

// Display owns the SDL window, renderer and the Game Boy frame texture. It is
// deliberately thin: it knows how to put the 160x144 emulator framebuffer onto
// a GPU texture and how to blit that texture aspect-correct into the window.
// All UI chrome (menus, ROM browser) is drawn on top by Dear ImGui in main.
class Display {
public:
  static constexpr int GB_W = 160;
  static constexpr int GB_H = 144;

  bool init(const char *title, int scale);
  void shutdown();
  ~Display();

  SDL_Window *window() const { return win; }
  SDL_Renderer *renderer() const { return ren; }

  // Upload the emulator's ARGB framebuffer into the game texture.
  void updateGame(const u32 *fb);

  // The game texture as an ImGui texture id (for ImGui::Image), or null.
  void *gameTexture() const { return tex; }

  // Draw the game texture into the window, scaled to fit while preserving the
  // 10:9 aspect ratio. The optional top inset reserves space for UI chrome.
  void blitGame(int topInset = 0);

private:
  SDL_Window *win = nullptr;
  SDL_Renderer *ren = nullptr;
  SDL_Texture *tex = nullptr;
  bool haveFrame = false;
};

#endif // DISPLAY_H
