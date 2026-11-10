// World of Dragon Warrior: Torneko - The Last Hope - native PC port (static recompilation of SLUS-01181).
#include "game.h"
#include "config.h"
#include "hw.h"
#include "bios.h"
#include "gte.h"
#include "log.h"
#include "rumble.h"
#include "discpack.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#if defined(_WIN32) || defined(USE_SDL)
#include <SDL2/SDL.h>
#endif

namespace fs = std::filesystem;
static std::string g_exe_dir;

#include "hostinput.h"
// INPUT_SCRIPT="2400=cross,2410=,3000=up+cross" : from frame N hold these buttons (headless tests)
static void scripted_input(int frame) {
    static std::vector<std::pair<int, uint32_t>> steps;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        const char* e = getenv("INPUT_SCRIPT");
        if (!e) return;
        std::string s = e;
        size_t p = 0;
        while (p < s.size()) {
            size_t c = s.find(',', p);
            std::string item = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
            p = c == std::string::npos ? s.size() : c + 1;
            size_t eq = item.find('=');
            if (eq == std::string::npos) continue;
            int f = atoi(item.substr(0, eq).c_str());
            uint32_t b = 0;
            std::string names = item.substr(eq + 1) + "+";
            size_t q = 0;
            while (q < names.size()) {
                size_t pl = names.find('+', q);
                std::string n = names.substr(q, pl - q);
                q = pl + 1;
                static const struct { const char* n; uint32_t b; } map[] = {
                    {"up", IN_UP}, {"down", IN_DOWN}, {"left", IN_LEFT}, {"right", IN_RIGHT}, {"cross", IN_SOUTH},
                    {"circle", IN_EAST}, {"square", IN_WEST}, {"triangle", IN_NORTH}, {"l1", IN_L1}, {"r1", IN_R1},
                    {"l2", IN_L2}, {"r2", IN_R2}, {"start", IN_START}, {"select", IN_BACK}};
                for (auto& m : map) if (n == m.n) b |= m.b;
            }
            steps.push_back({f, b});
        }
    }
    for (auto& st : steps) if (st.first == frame) g_host_input.buttons = st.second;
    // ANCHOR=addr=value: ASCRIPT frames count from the first frame RAM[addr] == value (compare runs)
    static int anchor = -1;
    static uint32_t aa = 0, av = 0;
    static std::vector<std::pair<int, uint32_t>> as;
    static bool ainit = false;
    if (!ainit) {
        ainit = true;
        if (getenv("ANCHOR")) sscanf(getenv("ANCHOR"), "%x=%x", &aa, &av);
        if (const char* e = getenv("ASCRIPT")) {
            auto save = steps;
            steps.clear();
            // reuse the parser on a temporary copy
            std::string s2 = e; size_t p = 0;
            while (p < s2.size()) {
                size_t c = s2.find(',', p);
                std::string item = s2.substr(p, c == std::string::npos ? std::string::npos : c - p);
                p = c == std::string::npos ? s2.size() : c + 1;
                size_t eq = item.find('=');
                if (eq == std::string::npos) continue;
                uint32_t b = 0; std::string names = item.substr(eq + 1) + "+"; size_t q = 0;
                while (q < names.size()) {
                    size_t pl = names.find('+', q); std::string n = names.substr(q, pl - q); q = pl + 1;
                    static const struct { const char* n; uint32_t b; } map[] = {
                        {"up", IN_UP}, {"down", IN_DOWN}, {"left", IN_LEFT}, {"right", IN_RIGHT}, {"cross", IN_SOUTH},
                        {"circle", IN_EAST}, {"square", IN_WEST}, {"triangle", IN_NORTH}, {"l1", IN_L1}, {"r1", IN_R1},
                        {"l2", IN_L2}, {"r2", IN_R2}, {"start", IN_START}, {"select", IN_BACK}};
                    for (auto& m : map) if (n == m.n) b |= m.b;
                }
                as.push_back({atoi(item.substr(0, eq).c_str()), b});
            }
            steps = save;
        }
    }
    static int after = getenv("ANCHOR_AFTER") ? atoi(getenv("ANCHOR_AFTER")) : 0;
    if (aa && anchor < 0 && frame > after && MEM_RW(aa & ~3u) == av) { anchor = frame; fprintf(stderr, "[anchor] frame %d\n", frame); }
    if (anchor >= 0) for (auto& st : as) if (st.first == frame - anchor) g_host_input.buttons = st.second;
}

// WATCH="800bb360,gp+178c" : print these RAM words whenever they change (debugging)
static void watch_ram(int frame) {
    static std::vector<std::pair<std::string, uint32_t>> w;
    static std::vector<uint32_t> last;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        const char* e = getenv("WATCH");
        if (!e) return;
        std::string s = std::string(e) + ",";
        size_t p = 0, c;
        while ((c = s.find(',', p)) != std::string::npos) {
            std::string it = s.substr(p, c - p);
            p = c + 1;
            if (!it.empty()) w.push_back({it, 0});
        }
        last.assign(w.size(), 0xdeadbeef);
    }
    for (size_t i = 0; i < w.size(); i++) {
        const std::string& it = w[i].first;
        uint32_t a = it.rfind("gp+", 0) == 0 ? g_cpu.r[28] + (uint32_t)strtoul(it.c_str() + 3, nullptr, 16)
                                             : (uint32_t)strtoul(it.c_str(), nullptr, 16);
        uint32_t v = MEM_RW(a & ~3u);
        if (v != last[i]) {
            fprintf(stderr, "[watch f%d] %s (%08x) = %08x\n", frame, it.c_str(), a, v);
            last[i] = v;
        }
    }
}

unsigned g_frame_no = 0;
extern bool g_capture_pending;

// POKE="8006eb07=2,80081000=5": write these RAM bytes every frame (testing: e.g. keep lives)
static void poke_ram() {
    static std::vector<std::pair<uint32_t, uint8_t>> p;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (const char* e = getenv("POKE")) {
            std::string s = std::string(e) + ",";
            size_t a = 0, c;
            while ((c = s.find(',', a)) != std::string::npos) {
                unsigned ad = 0, v = 0;
                if (sscanf(s.substr(a, c - a).c_str(), "%x=%x", &ad, &v) == 2) p.push_back({ad, (uint8_t)v});
                a = c + 1;
            }
        }
    }
    static unsigned from = getenv("POKE_FROM") ? (unsigned)atoi(getenv("POKE_FROM")) : 0;
    if (g_frame_no < from) return;
    for (auto& x : p) g_ram[x.first & 0x1fffff] = x.second;
}

void platform_vblank() {
    static int frames = 0;
    static int vbl = 0;
    g_frame_no = ++vbl;
    if (g_capture_pending) {
        g_capture_pending = false;
        char fn[512];
        snprintf(fn, sizeof fn, "%s/ram_%06d.bin", getenv("OVL_CAPTURE"), vbl);
        if (FILE* f = fopen(fn, "wb")) { fwrite(g_ram, 1, 0x200000, f); fclose(f); }
    }
    scripted_input(vbl);
    poke_ram();
    rumble_frame();
    watch_ram(vbl);
    static int dump_at = -1;
    static char dump_fn[256];
    if (dump_at == -1) { dump_at = 0; if (getenv("RAM_DUMP_AT")) sscanf(getenv("RAM_DUMP_AT"), "%d:%255s", &dump_at, dump_fn); }
    if (dump_at && vbl == dump_at) { FILE* f = fopen(dump_fn, "wb"); if (f) { fwrite(g_ram, 1, 0x200000, f); fclose(f); } }
    static int limit = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 0;
    if (limit && ++frames >= limit) {
        fprintf(stderr, "[main] frame limit reached (%d frames, %.1f s emulated)\n", frames, g_cpu.cycles / (double)CPU_HZ);
        fflush(stderr);
        _Exit(0);
    }
    static int stop_at = getenv("STOP_AT_FRAME") ? atoi(getenv("STOP_AT_FRAME")) : 0;
    static int vb = 0;
#ifdef SIGTRAP
    if (stop_at && ++vb == stop_at) raise(SIGTRAP);
#endif
    hw_frame_pacing();
}

[[noreturn]] static void fatal(const std::string& msg) {
    fprintf(stderr, "%s\n", msg.c_str());
#if defined(_WIN32) || defined(USE_SDL)
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_NAME, msg.c_str(), nullptr);
#endif
    exit(1);
}

// A .cue in the folder, or a folder prepared by tools/prepare_disc.py
static std::string find_disc_in(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = e.path().extension().string();
        for (auto& ch : ext) ch = (char)tolower(ch);
        if (ext == ".cue" || ext == ".chd") return e.path().string();
    }
    if (fs::exists(dir / "track01.bin")) return dir.string();
    return {};
}

// Reads and checks the game executable from the open disc (empty string = ok)
static std::string check_exe(std::vector<uint8_t>& exe) {
    if (!cd_read_file(GAME_EXE_NAME, exe) || exe.size() < 0x800 || memcmp(exe.data(), "PS-X EXE", 8) != 0)
        return "This disc image does not contain " GAME_EXE_NAME ".\n"
               "This port needs the North American release of Torneko: The Last Hope (" GAME_SERIAL ").";
    uint32_t pc;
    memcpy(&pc, exe.data() + 0x10, 4);
    if (exe.size() != GAME_EXE_SIZE || pc != GAME_EXE_PC)
        return "This copy of " GAME_EXE_NAME " is a different build than the one this port was made from.";
    return {};
}

// ---------------------------------------------------------------- one-time disc import
#if defined(_WIN32) || defined(USE_SDL)
static SDL_Window* g_imp_win = nullptr;
static SDL_Renderer* g_imp_ren = nullptr;
static const char* g_prog_label = "Importing disc into assets.dat";
static void progress_open(const char* title) {
    SDL_Init(SDL_INIT_VIDEO);
    g_imp_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 108, 0);
    if (g_imp_win) g_imp_ren = SDL_CreateRenderer(g_imp_win, -1, 0);
}
static void progress_close() {
    if (g_imp_ren) SDL_DestroyRenderer(g_imp_ren);
    if (g_imp_win) SDL_DestroyWindow(g_imp_win);
    g_imp_ren = nullptr;
    g_imp_win = nullptr;
}
static void import_progress(int done, int total) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {}
    int pct = total ? done * 100 / total : 0;
    char t[96];
    snprintf(t, sizeof t, "%s - %d%%", g_prog_label, pct);
    if (g_imp_win) SDL_SetWindowTitle(g_imp_win, t);
    if (g_imp_ren) {
        SDL_SetRenderDrawColor(g_imp_ren, 24, 18, 32, 255);
        SDL_RenderClear(g_imp_ren);
        SDL_Rect frame = {20, 40, 440, 28}, bar = {24, 44, total ? 432 * done / total : 0, 20};
        SDL_SetRenderDrawColor(g_imp_ren, 120, 110, 140, 255);
        SDL_RenderDrawRect(g_imp_ren, &frame);
        SDL_SetRenderDrawColor(g_imp_ren, 240, 140, 40, 255);
        SDL_RenderFillRect(g_imp_ren, &bar);
        SDL_RenderPresent(g_imp_ren);
    }
    fprintf(stderr, "\r%s - %d%%", g_prog_label, pct);
}
#else
static const char* g_prog_label = "Importing disc into assets.dat";
static void progress_open(const char*) {}
static void progress_close() {}
static void import_progress(int done, int total) { fprintf(stderr, "\r%s - %d%%", g_prog_label, total ? done * 100 / total : 0); }
#endif

static void info(const std::string& msg) {
    fprintf(stderr, "%s\n", msg.c_str());
#if defined(_WIN32) || defined(USE_SDL)
    if (!getenv("NO_MSGBOX")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, GAME_NAME, msg.c_str(), nullptr);
#endif
}

// .chd or .cue/.bin -> assets.dat. The disc is checked first, then packed and verified sector by sector.
static void import_disc(const std::string& disc, const fs::path& pack) {
    if (!cd_init(disc.c_str())) fatal("Cannot read the disc image " + disc);
    std::vector<uint8_t> exe;
    std::string bad = check_exe(exe);
    if (!bad.empty()) fatal(bad);
    progress_open("Importing disc");
    std::string err;
    bool ok = cd_make_pack(pack.string().c_str(), import_progress, &err);
    fprintf(stderr, "\n");
    progress_close();
    cd_close_image();
    if (!ok) fatal("Importing the disc failed: " + err);
    std::error_code ec;
    double mb = (double)fs::file_size(pack, ec) / (1024.0 * 1024.0);
    char buf[64];
    snprintf(buf, sizeof buf, "%.0f MB", mb);
    info("Your disc was imported into assets.dat (" + std::string(buf) + ", verified lossless).\n\n"
         "The game now runs from assets.dat alone. The disc image (" + fs::path(disc).filename().string() +
         " and any .bin files) is no longer needed and can be deleted.");
}


// assets.dat -> .cue + .bin (one .bin per track), byte-identical to the imported disc image
static void export_disc(const fs::path& pack, const fs::path& dir) {
    DiscLayout lay;
    std::string err;
    if (!pack_open(pack.string(), &lay, &err)) fatal("Cannot open " + pack.string() + ": " + err);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string name = GAME_DISC_NAME;
    const bool two = lay.t2_sectors > 0;
    struct Part { std::string file; int first, count; };
    std::vector<Part> parts;
    parts.push_back({name + (two ? " (Track 1).bin" : ".bin"), 0, lay.t1_sectors});
    if (two) parts.push_back({name + " (Track 2).bin", lay.t1_sectors, lay.t2_start + lay.t2_sectors - lay.t1_sectors});
    const int total = lay.t2_start + lay.t2_sectors;
    int done = 0;
    g_prog_label = "Writing the disc image";
    progress_open("Writing the disc image");
    uint8_t sec[2352];
    for (auto& p : parts) {
        FILE* f = fopen((dir / p.file).string().c_str(), "wb");
        if (!f) { progress_close(); fatal("Cannot create " + (dir / p.file).string()); }
        for (int i = 0; i < p.count; i++) {
            if (!pack_read(p.first + i, sec)) { fclose(f); progress_close(); fatal("assets.dat is damaged (sector " + std::to_string(p.first + i) + ")"); }
            if (fwrite(sec, 1, 2352, f) != 2352) { fclose(f); progress_close(); fatal("Write error (disk full?) in " + p.file); }
            if (++done % 4096 == 0) import_progress(done, total);
        }
        fclose(f);
    }
    import_progress(total, total);
    progress_close();
    std::string cue = "FILE \"" + parts[0].file + "\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    if (two) {
        int gap = lay.t2_start - lay.t1_sectors;
        char idx[32];
        snprintf(idx, sizeof idx, "%02d:%02d:%02d", gap / 4500, gap / 75 % 60, gap % 75);
        cue += "FILE \"" + parts[1].file + "\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 " + idx + "\n";
    }
    FILE* f = fopen((dir / (name + ".cue")).string().c_str(), "wb");
    if (!f) fatal("Cannot create the .cue file");
    fwrite(cue.data(), 1, cue.size(), f);
    fclose(f);
    fprintf(stderr, "\n");
    info("Disc image written to " + dir.string() + ":\n" + name + ".cue + " + std::to_string(parts.size()) +
         " .bin file(s), " + std::to_string(total) + " sectors.");
}

int main(int argc, char** argv) {
    std::string self = argv[0];
    size_t slash = self.find_last_of("/\\");
    g_exe_dir = slash == std::string::npos ? "" : self.substr(0, slash + 1);
    config_load(argv[0]);
    {
        extern std::string g_interp_log_path;
        g_interp_log_path = g_exe_dir + "torneko.log";
        std::error_code ec;
        fs::remove(g_interp_log_path, ec);   // a fresh log each run
    }
    fs::path base = g_exe_dir.empty() ? fs::path(".") : fs::path(g_exe_dir);
    fs::path pack = base / "assets.dat";

    // torneko --import <cue> [out.dat] : build the packed file and exit
    if (argc > 2 && !strcmp(argv[1], "--import")) {
        fs::path p = argv[2];
        std::string disc = fs::is_directory(p) ? find_disc_in(p) : p.string();
        import_disc(disc, argc > 3 ? fs::path(argv[3]) : pack);
        return 0;
    }

    // torneko --export [folder] : write the disc back out of assets.dat as .cue/.bin and exit
    if (argc > 1 && !strcmp(argv[1], "--export")) {
        export_disc(pack, argc > 2 ? fs::path(argv[2]) : base / "disc image");
        return 0;
    }

    std::string disc;   // .cue (or prepared folder) to run from directly
    fs::path use_pack;
    std::error_code ec;
    if (argc > 1) {
        fs::path p = argv[1];
        std::string ext = p.extension().string();
        for (auto& ch : ext) ch = (char)tolower(ch);
        if (ext == ".dat") use_pack = p;
        else {
            disc = fs::is_directory(p, ec) ? find_disc_in(p) : p.string();
            if (!disc.empty() && !fs::exists(pack, ec)) { import_disc(disc, pack); use_pack = pack; disc.clear(); }
        }
    } else if (fs::exists(pack, ec)) {
        use_pack = pack;
    } else {
        std::string found = find_disc_in(base / "game");
        if (found.empty()) found = find_disc_in(base);
        if (!found.empty()) { import_disc(found, pack); use_pack = pack; }
    }

    if (!use_pack.empty()) {
        std::string err;
        if (!cd_init_pack(use_pack.string().c_str(), &err)) fatal(err);
    } else if (disc.empty() || !cd_init(disc.c_str())) {
        fatal(GAME_NAME " game data not found.\n\n"
              "First run: put your disc image (.chd, or .cue + .bin; NTSC-U " GAME_SERIAL ") next to " GAME_BIN " "
              "or in a folder named \"game\" (or drag it onto the exe). It is imported once "
              "into assets.dat, after which it can be deleted.");
    }

    std::vector<uint8_t> exe;
    std::string bad = check_exe(exe);
    if (!bad.empty()) fatal(bad);

    g_gte_widescreen = g_cfg.widescreen ? 0.75f : 1.0f;
    hw_init();
    gpu_init();
    spu_init();
    mdec_init();
    sio_init();
    bios_init(g_exe_dir.c_str());
    bios_run_exe(exe.data(), exe.size());
}
