#include "bus.h"
#include "core.h"
#include "display.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr int SCALE = 4; // window starts at 4x the Game Boy resolution
constexpr double FRAME_MS = 1000.0 / 59.7275;

// Selectable output palettes (shade 0 lightest -> 3 darkest).
struct Theme {
  const char *name;
  u32 c[4];
};
const Theme kThemes[] = {
    {"DMG Green", {0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F}},
    {"Grayscale", {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000}},
    {"Pocket", {0xFFC4CFA1, 0xFF8B956D, 0xFF4D533C, 0xFF1F1F1F}},
    {"Ocean", {0xFFE0F8D0, 0xFF5FA0C0, 0xFF2060A0, 0xFF081840}},
    {"Crimson", {0xFFFFE9E9, 0xFFE08080, 0xFFA02020, 0xFF300000}},
};
constexpr int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

// ---- Headless / ASCII test paths (unchanged behaviour) --------------------

int runHeadless(Bus &bus) {
  Core cpu(bus);
  cpu.powerOn();
  for (long i = 0; i < 200'000'000L; i++)
    cpu.step();
  std::printf("\n");
  return 0;
}

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
  const char *shades = " .:#";
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

// ---- ROM discovery ---------------------------------------------------------

struct RomEntry {
  std::string path;
  std::string name;
};

std::string displayName(const fs::path &p) {
  std::string s = p.stem().string();
  for (char &c : s)
    if (c == '_')
      c = ' ';
  return s;
}

bool isRom(const fs::path &p) {
  std::string e = p.extension().string();
  for (char &c : e)
    c = (char)std::tolower((unsigned char)c);
  return e == ".gb" || e == ".gbc";
}

std::vector<RomEntry> scanRoms(const char *cliRom) {
  std::vector<RomEntry> out;
  auto add = [&](const fs::path &p) {
    std::string abs = fs::absolute(p).lexically_normal().string();
    for (const auto &r : out)
      if (r.path == abs)
        return; // de-dupe
    out.push_back({abs, displayName(p)});
  };
  if (cliRom)
    add(cliRom);
  size_t cliCount = out.size();
  for (const char *dir : {"games", "roms", "."}) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
      continue;
    for (const auto &ent : fs::directory_iterator(dir, ec))
      if (ent.is_regular_file() && isRom(ent.path()))
        add(ent.path());
  }
  std::sort(out.begin() + cliCount, out.end(),
            [](const RomEntry &a, const RomEntry &b) { return a.name < b.name; });
  return out;
}

// ---- Persisted library (imported ROM paths) --------------------------------

// One absolute path per line, stored under the user's home so it survives
// restarts regardless of the working directory.
std::string libraryFile() {
  const char *home = std::getenv("HOME");
  fs::path base = home ? fs::path(home) / ".gb_emu" : fs::path(".");
  std::error_code ec;
  fs::create_directories(base, ec);
  return (base / "library.txt").string();
}

std::vector<std::string> loadLibrary() {
  std::vector<std::string> out;
  std::ifstream f(libraryFile());
  std::string line;
  while (std::getline(f, line)) {
    while (!line.empty() &&
           (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
      line.pop_back();
    if (!line.empty())
      out.push_back(line);
  }
  return out;
}

void saveLibrary(const std::vector<std::string> &paths) {
  std::ofstream f(libraryFile(), std::ios::trunc);
  for (const auto &p : paths)
    f << p << "\n";
}

// ---- Input mapping ---------------------------------------------------------

// Returns the Game Boy button bit for a key, or -1 if not a game key.
int gameBit(SDL_Keycode key) {
  switch (key) {
  case SDLK_z: return 0;         // A
  case SDLK_x: return 1;         // B
  case SDLK_BACKSPACE: return 2; // Select
  case SDLK_RETURN: return 3;    // Start
  case SDLK_RIGHT: return 4;
  case SDLK_LEFT: return 5;
  case SDLK_UP: return 6;
  case SDLK_DOWN: return 7;
  default: return -1;
  }
}

// ---- ImGui look & feel -----------------------------------------------------

ImU32 fromArgb(u32 c) {
  return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
}

void applyStyle() {
  ImGuiStyle &s = ImGui::GetStyle();
  s.WindowRounding = 0.0f;
  s.ChildRounding = 0.0f;
  s.FrameRounding = 0.0f;
  s.GrabRounding = 0.0f;
  s.PopupRounding = 0.0f;
  s.ScrollbarRounding = 0.0f;
  s.TabRounding = 0.0f;
  s.WindowBorderSize = 1.0f;
  s.WindowPadding = ImVec2(16, 14);
  s.FramePadding = ImVec2(10, 7);
  s.ItemSpacing = ImVec2(10, 9);
  s.ScrollbarSize = 12.0f;

  ImVec4 *c = s.Colors;
  const ImVec4 accent = ImColor(90, 130, 180, 255).Value;    // steel blue
  const ImVec4 accentDk = ImColor(40, 58, 84, 255).Value;    // muted dark blue
  const ImVec4 accentHov = ImColor(66, 96, 138, 255).Value;
  c[ImGuiCol_WindowBg] = ImColor(26, 27, 30, 245).Value;
  c[ImGuiCol_ChildBg] = ImColor(19, 20, 23, 255).Value;
  c[ImGuiCol_PopupBg] = ImColor(26, 27, 30, 250).Value;
  c[ImGuiCol_Border] = ImColor(50, 66, 92, 200).Value;
  c[ImGuiCol_FrameBg] = ImColor(38, 40, 45, 255).Value;
  c[ImGuiCol_FrameBgHovered] = accentDk;
  c[ImGuiCol_FrameBgActive] = accentDk;
  c[ImGuiCol_TitleBg] = ImColor(19, 20, 23, 255).Value;
  c[ImGuiCol_TitleBgActive] = accentDk;
  c[ImGuiCol_MenuBarBg] = ImColor(32, 34, 38, 255).Value;
  c[ImGuiCol_Header] = accentDk;
  c[ImGuiCol_HeaderHovered] = accentHov;
  c[ImGuiCol_HeaderActive] = accent;
  c[ImGuiCol_Button] = accentDk;
  c[ImGuiCol_ButtonHovered] = accentHov;
  c[ImGuiCol_ButtonActive] = accent;
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_Separator] = ImColor(44, 74, 18, 180).Value;
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

  if (headless || asciiFrames > 0) {
    if (!romPath) {
      std::printf("usage: %s [--headless|--ascii N] rom.gb\n", argv[0]);
      return 1;
    }
    Bus bus;
    if (!bus.loadRom(romPath)) {
      std::printf("could not open %s\n", romPath);
      return 1;
    }
    return asciiFrames > 0 ? runAscii(bus, asciiFrames) : runHeadless(bus);
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    std::printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  Display display;
  if (!display.init("gb_emu", SCALE)) {
    std::printf("display init failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  // Dear ImGui setup.
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // don't litter an imgui.ini file
  ImFont *fontBig = nullptr;
  const char *fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
  if (fs::exists(fontPath)) {
    io.Fonts->AddFontFromFileTTF(fontPath, 18.0f);
    fontBig = io.Fonts->AddFontFromFileTTF(fontPath, 48.0f);
  } else {
    io.Fonts->AddFontDefault();
    fontBig = io.Fonts->AddFontDefault();
  }
  ImGui::StyleColorsDark();
  applyStyle();
  ImGui_ImplSDL2_InitForSDLRenderer(display.window(), display.renderer());
  ImGui_ImplSDLRenderer2_Init(display.renderer());

  // Audio: 44100 Hz stereo float via the queue API.
  SDL_AudioSpec want{}, have{};
  want.freq = APU::SAMPLE_RATE;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 1024;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (audio)
    SDL_PauseAudioDevice(audio, 0);

  // Library = auto-discovered ROMs + previously imported paths (persisted).
  std::vector<std::string> saved = loadLibrary();
  std::vector<RomEntry> roms;
  auto refreshRoms = [&]() {
    roms = scanRoms(romPath);
    for (const auto &p : saved) {
      std::error_code ec;
      if (!fs::is_regular_file(p, ec))
        continue; // skip imports that have since moved/been deleted
      std::string abs = fs::absolute(p).lexically_normal().string();
      bool dup = false;
      for (const auto &r : roms)
        if (r.path == abs) {
          dup = true;
          break;
        }
      if (!dup)
        roms.push_back({abs, displayName(p)});
    }
  };
  refreshRoms();

  // Active emulation session, recreated whenever a ROM is loaded.
  std::unique_ptr<Bus> bus;
  std::unique_ptr<Core> cpu;
  std::string currentRom, currentName;
  int themeIdx = 0, librarySel = 0;
  u8 buttons = 0;
  bool paused = false, showLibrary = true, showAbout = false;
  bool showImport = false;
  std::string browseDir;

  auto loadRom = [&](const RomEntry &rom) -> bool {
    auto b = std::make_unique<Bus>();
    if (!b->loadRom(rom.path.c_str()))
      return false;
    if (bus)
      bus->saveRam(); // persist the outgoing game before swapping it out
    b->ppu.setPalette(kThemes[themeIdx].c);
    auto c = std::make_unique<Core>(*b);
    c->powerOn();
    bus = std::move(b);
    cpu = std::move(c);
    currentRom = rom.path;
    currentName = rom.name;
    buttons = 0;
    paused = false;
    showLibrary = false;
    return true;
  };
  auto resetGame = [&]() {
    if (!currentRom.empty())
      loadRom({currentRom, currentName});
  };

  // Boot splash timing.
  enum class State { Splash, Run };
  State state = State::Splash;
  Uint64 splashStart = SDL_GetTicks64();

  bool running = true;
  Uint64 frameStart = SDL_GetTicks64();

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL2_ProcessEvent(&e);
      if (e.type == SDL_QUIT) {
        running = false;
        continue;
      }
      if (state == State::Splash) {
        if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN)
          state = State::Run;
        continue;
      }
      bool keydown = e.type == SDL_KEYDOWN && !e.key.repeat;
      bool keyup = e.type == SDL_KEYUP;
      if ((!keydown && !keyup) || io.WantCaptureKeyboard)
        continue;
      SDL_Keycode k = e.key.keysym.sym;
      bool ctrl = SDL_GetModState() & KMOD_CTRL;

      if (keydown && k == SDLK_ESCAPE) {
        if (bus)
          showLibrary = !showLibrary;
      } else if (keydown && ctrl && k == SDLK_q) {
        running = false;
      } else if (keydown && k == SDLK_p && bus) {
        paused = !paused;
      } else if (keydown && k == SDLK_r && bus) {
        resetGame();
      } else if (bus && !showLibrary) {
        int bit = gameBit(k);
        if (bit >= 0) {
          if (keydown)
            buttons |= (1 << bit);
          else if (keyup)
            buttons &= ~(1 << bit);
        }
      }
    }

    if (state == State::Splash &&
        SDL_GetTicks64() - splashStart > 1900)
      state = State::Run;

    // Advance emulation one frame when actively playing.
    bool emulating = bus && !paused && !showLibrary && state == State::Run;
    if (emulating) {
      bus->setButtons(buttons);
      int guard = 0;
      while (!bus->ppu.frameReady && guard < 140000)
        guard += cpu->step();
      bus->ppu.frameReady = false;
      display.updateGame(bus->ppu.framebuffer());

      if (audio &&
          SDL_GetQueuedAudioSize(audio) < APU::SAMPLE_RATE * sizeof(float))
        SDL_QueueAudio(audio, bus->apu.samples.data(),
                       bus->apu.samples.size() * sizeof(float));
      bus->apu.samples.clear();
    } else if (bus) {
      display.updateGame(bus->ppu.framebuffer()); // keep last frame visible
    }

    // ---- Build the UI --------------------------------------------------
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (state == State::Splash) {
      ImGui::SetNextWindowPos(ImVec2(0, 0));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("##splash", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoBackground);
      ImGui::PushFont(fontBig);
      const char *title = "GB-EMU";
      ImVec2 ts = ImGui::CalcTextSize(title);
      ImGui::SetCursorPos(ImVec2((io.DisplaySize.x - ts.x) / 2,
                                 io.DisplaySize.y / 2 - ts.y));
      ImGui::TextColored(ImColor(fromArgb(0xFF8FB4E6)), "%s", title);
      ImGui::PopFont();
      const char *sub = "a game boy emulator";
      ImVec2 ss = ImGui::CalcTextSize(sub);
      ImGui::SetCursorPos(
          ImVec2((io.DisplaySize.x - ss.x) / 2, io.DisplaySize.y / 2 + 20));
      ImGui::TextDisabled("%s", sub);
      ImGui::End();
    } else {
      // Menu bar.
      if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
          if (ImGui::MenuItem("Library...", "Esc"))
            showLibrary = true;
          if (ImGui::MenuItem("Import ROM..."))
            showImport = true;
          if (ImGui::MenuItem("Rescan ROMs"))
            refreshRoms();
          ImGui::Separator();
          if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            running = false;
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Emulation")) {
          ImGui::MenuItem("Pause", "P", &paused, (bool)bus);
          if (ImGui::MenuItem("Reset", "R", false, (bool)bus))
            resetGame();
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
          if (ImGui::BeginMenu("Palette")) {
            for (int i = 0; i < kThemeCount; i++)
              if (ImGui::MenuItem(kThemes[i].name, nullptr, themeIdx == i)) {
                themeIdx = i;
                if (bus)
                  bus->ppu.setPalette(kThemes[i].c);
              }
            ImGui::EndMenu();
          }
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
          if (ImGui::MenuItem("About"))
            showAbout = true;
          ImGui::EndMenu();
        }
        char status[64];
        if (bus) {
          std::string nm = currentName.size() > 22
                               ? currentName.substr(0, 21) + "\xE2\x80\xA6"
                               : currentName;
          std::snprintf(status, sizeof(status), "%s  -  %s",
                        paused ? "PAUSED" : "PLAYING", nm.c_str());
        } else {
          std::snprintf(status, sizeof(status), "no game loaded");
        }
        // Only draw the status if there is room beyond the menu items.
        float w = ImGui::CalcTextSize(status).x;
        float x = ImGui::GetWindowWidth() - w - 16;
        if (x > ImGui::GetCursorPosX() + 12) {
          ImGui::SameLine(x);
          ImGui::TextDisabled("%s", status);
        }
        ImGui::EndMainMenuBar();
      }

      // Library / start menu.
      if (showLibrary) {
        ImVec2 sz(440, 480);
        ImGui::SetNextWindowSize(sz, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(
            ImVec2((io.DisplaySize.x - sz.x) / 2, (io.DisplaySize.y - sz.y) / 2),
            ImGuiCond_Appearing);
        bool *pOpen = bus ? &showLibrary : nullptr; // can't close with no game
        if (ImGui::Begin("Game Library", pOpen,
                         ImGuiWindowFlags_NoCollapse)) {

          float footer = ImGui::GetFrameHeightWithSpacing() + 4;
          ImGui::BeginChild("list", ImVec2(0, -footer), true);
          for (int i = 0; i < (int)roms.size(); i++) {
            if (ImGui::Selectable(roms[i].name.c_str(), librarySel == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
              librarySel = i;
              if (ImGui::IsMouseDoubleClicked(0))
                loadRom(roms[i]);
            }
          }
          if (roms.empty())
            ImGui::TextDisabled("Use File > Import ROM to add a game");
          ImGui::EndChild();

          ImGui::BeginDisabled(roms.empty());
          if (ImGui::Button("Play", ImVec2(-1, 0)) && !roms.empty())
            loadRom(roms[librarySel]);
          ImGui::EndDisabled();
        }
        ImGui::End();
      }

      // Import ROM: a small in-window file browser (no native dialog needed).
      if (showImport) {
        ImGui::OpenPopup("Import ROM");
        showImport = false;
        if (browseDir.empty()) {
          std::error_code ec;
          browseDir = fs::current_path(ec).string();
        }
      }
      ImGui::SetNextWindowSize(ImVec2(540, 440), ImGuiCond_Appearing);
      if (ImGui::BeginPopupModal("Import ROM", nullptr,
                                 ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("%s", browseDir.c_str());
        ImGui::Separator();
        ImGui::BeginChild("fb", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4),
                          true);

        fs::path cur(browseDir);
        if (cur.has_parent_path() && cur.parent_path() != cur &&
            ImGui::Selectable("../"))
          browseDir = cur.parent_path().string();

        std::vector<fs::path> dirs, files;
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(
                 cur, fs::directory_options::skip_permission_denied, ec)) {
          if (e.is_directory(ec))
            dirs.push_back(e.path());
          else if (e.is_regular_file(ec) && isRom(e.path()))
            files.push_back(e.path());
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        for (const auto &d : dirs) {
          std::string label = d.filename().string() + "/";
          if (ImGui::Selectable(label.c_str()))
            browseDir = d.string();
        }
        for (const auto &f : files) {
          if (ImGui::Selectable(f.filename().string().c_str())) {
            std::string abs = fs::absolute(f).lexically_normal().string();
            int idx = -1;
            for (int i = 0; i < (int)roms.size(); i++)
              if (roms[i].path == abs)
                idx = i;
            if (idx < 0) {
              roms.push_back({abs, displayName(f)});
              idx = (int)roms.size() - 1;
            }
            // Remember the import across restarts.
            if (std::find(saved.begin(), saved.end(), abs) == saved.end()) {
              saved.push_back(abs);
              saveLibrary(saved);
            }
            librarySel = idx;
            showLibrary = true;
            ImGui::CloseCurrentPopup();
          }
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }

      if (showAbout) {
        ImGui::OpenPopup("About GB-EMU");
        showAbout = false;
      }
      if (ImGui::BeginPopupModal("About GB-EMU", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("GB-EMU - a Game Boy (DMG) emulator");
        ImGui::Separator();
        ImGui::TextDisabled("Controls");
        ImGui::BulletText("D-Pad: Arrow keys");
        ImGui::BulletText("A / B: Z / X");
        ImGui::BulletText("Start / Select: Enter / Backspace");
        ImGui::BulletText("Library: Esc    Pause: P    Reset: R");
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0)))
          ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
    }

    // ---- Render --------------------------------------------------------
    ImGui::Render();
    SDL_SetRenderDrawColor(display.renderer(), 12, 14, 18, 255);
    SDL_RenderClear(display.renderer());
    display.blitGame();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),
                                          display.renderer());
    SDL_RenderPresent(display.renderer());

    // Periodic safety flush of battery RAM (cheap no-op unless RAM changed).
    static Uint64 lastSave = 0;
    if (bus && SDL_GetTicks64() - lastSave > 3000) {
      bus->saveRam();
      lastSave = SDL_GetTicks64();
    }

    // Pace to ~59.73 fps.
    Uint64 now = SDL_GetTicks64();
    double el = (double)(now - frameStart);
    if (el < FRAME_MS)
      SDL_Delay((Uint32)(FRAME_MS - el));
    frameStart = SDL_GetTicks64();
  }

  if (bus)
    bus->saveRam(); // final flush on quit
  if (audio)
    SDL_CloseAudioDevice(audio);
  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  display.shutdown();
  SDL_Quit();
  return 0;
}
