#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "ajazz.h"
#include "imgconv.h"

// ---------------------------------------------------------------------------
// Minimal argument parsing helpers
// ---------------------------------------------------------------------------

[[noreturn]] static void die(const std::string& msg)
{
    std::cerr << "Error: " << msg << "\nRun 'ajazz --help' for usage.\n";
    std::exit(1);
}

static bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
static bool eq(const char* a, const char* b, const char* c) { return eq(a,b) || eq(a,c); }

// Match "--opt value" or "--opt=value".  Sets val and advances i if matched.
static bool take_opt(const std::vector<std::string>& args, int& i,
                     const char* shrt, const char* lng, std::string& val)
{
    const char* a = args[i].c_str();
    if (lng) {
        auto n = std::strlen(lng);
        if (std::strncmp(a, lng, n) == 0 && a[n] == '=') { val = a + n + 1; return true; }
    }
    if ((shrt && eq(a, shrt)) || (lng && eq(a, lng))) {
        if (i + 1 >= static_cast<int>(args.size()))
            die(std::string(lng ? lng : shrt) + " requires a value");
        val = args[++i];
        return true;
    }
    return false;
}

static int parse_int(const char* name, const std::string& s, int lo, int hi)
{
    std::size_t pos = 0;
    int v = 0;
    bool ok = false;
    try { v = std::stoi(s, &pos); ok = (pos == s.size()); } catch (...) {}
    if (!ok || v < lo || v > hi)
        die(std::string(name) + " must be an integer "
            + std::to_string(lo) + "-" + std::to_string(hi) + ", got: " + s);
    return v;
}

static bool is_raw(const std::string& p)
{
    if (p.size() < 4) return false;
    std::string ext = p.substr(p.size() - 4);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".raw";
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

static void usage_global()
{
    std::cout <<
        "Usage: ajazz [-v] [-q] <command> [options]\n"
        "\n"
        "Commands:\n"
        "  list        Discover and list HID interfaces\n"
        "  time        Sync display clock to system time\n"
        "  light       Set RGB key lighting\n"
        "  solid       Fill display with a solid colour\n"
        "  image       Upload a static image\n"
        "  animation   Upload an animated GIF\n"
        "\n"
        "Global options:\n"
        "  -v, --verbose   Print protocol debug messages\n"
        "  -q, --quiet     Suppress all output except errors\n"
        "  -h, --help      Show this help\n"
        "\n"
        "Run 'ajazz <command> --help' for command-specific options.\n";
}

// ---------------------------------------------------------------------------
// Subcommand helpers
// ---------------------------------------------------------------------------

static std::string usage_page_label(uint16_t up)
{
    switch (up) {
    case 0x0001: return "Boot keyboard";
    case 0x000C: return "Consumer / media keys";
    case ajazz::USAGE_PAGE_DISP: return "Display image data  <- display channel";
    case ajazz::USAGE_PAGE_CMD:  return "Command channel     <- command channel";
    default: char buf[16]; std::snprintf(buf, sizeof(buf), "0x%04X", up); return buf;
    }
}

static void open_and_run(bool verbose, bool quiet,
                         void (*fn)(ajazz::Keyboard&, const void*), const void* ctx)
{
    try {
        ajazz::Keyboard kbd(verbose, quiet);
        kbd.open();
        fn(kbd, ctx);
        kbd.close();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        std::exit(1);
    }
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------

static void cmd_list(const std::vector<std::string>& args)
{
    for (const auto& a : args)
        if (eq(a.c_str(), "-h", "--help")) {
            std::cout <<
                "Usage: ajazz list\n"
                "\n"
                "Discover and list all Ajazz keyboard HID interfaces.\n"
                "\n"
                "Options:\n"
                "  -h, --help   Show this help\n";
            return;
        }

    auto interfaces = ajazz::discover();
    if (interfaces.empty()) {
        std::cout << "No Ajazz keyboard found (VID=0x0C45 PID=0x8009).\n"
                     "Is the keyboard plugged in with the back switch set to USB?\n";
        std::exit(1);
    }
    std::cout << "Ajazz AK35I V3 MAX  (VID=0x0C45 PID=0x8009) — "
              << interfaces.size() << " interface(s) found:\n\n";
    for (const auto& d : interfaces) {
        char up[8]; std::snprintf(up, sizeof(up), "0x%04X", d.usage_page);
        std::cout << "  Interface " << d.interface_num
                  << "  " << std::left << std::setw(16) << d.path << std::right
                  << "  usage=" << up << "  " << usage_page_label(d.usage_page) << '\n';
    }
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------

static void cmd_time(const std::vector<std::string>& args, bool verbose, bool quiet)
{
    std::string at_str;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const char* a = args[i].c_str();
        std::string val;
        if (eq(a, "-h", "--help")) {
            std::cout <<
                "Usage: ajazz time [--at <time>]\n"
                "\n"
                "Sync the display RTC to local system time.\n"
                "After one sync the clock runs autonomously — no daemon needed.\n"
                "\n"
                "Options:\n"
                "  --at <time>   YYYY-MM-DDTHH:MM:SS  or  HH:MM:SS (today's date)\n"
                "  -h, --help    Show this help\n"
                "\n"
                "Examples:\n"
                "  ajazz time\n"
                "  ajazz time --at 14:30:00\n"
                "  ajazz time --at 2026-06-01T09:00:00\n";
            return;
        }
        else if (take_opt(args, i, nullptr, "--at", val)) at_str = val;
        else die("unknown option: " + args[i]);
    }

    std::tm t = {};
    const std::tm* tp = nullptr;

    if (!at_str.empty()) {
        int Y=0, M=0, D=0, h=0, m=0, s=0;
        if (std::sscanf(at_str.c_str(), "%d-%d-%dT%d:%d:%d", &Y,&M,&D,&h,&m,&s) == 6) {
            t.tm_year=Y-1900; t.tm_mon=M-1; t.tm_mday=D;
            t.tm_hour=h; t.tm_min=m; t.tm_sec=s; t.tm_isdst=-1;
            if (std::mktime(&t) == -1) die("invalid date/time: " + at_str);
        } else if (std::sscanf(at_str.c_str(), "%d:%d:%d", &h,&m,&s) == 3) {
            std::time_t now = std::time(nullptr);
            t = *std::localtime(&now);
            t.tm_hour=h; t.tm_min=m; t.tm_sec=s;
        } else {
            die("--at expects YYYY-MM-DDTHH:MM:SS or HH:MM:SS, got: " + at_str);
        }
        tp = &t;
    }

    open_and_run(verbose, quiet,
        [](ajazz::Keyboard& kbd, const void* ctx) {
            kbd.set_time(static_cast<const std::tm*>(ctx));
        }, tp);
}

// ---------------------------------------------------------------------------
// light
// ---------------------------------------------------------------------------

static void cmd_light(const std::vector<std::string>& args, bool verbose, bool quiet)
{
    std::string mode_str = "static";
    int r=255, g=255, b=255, brightness=5, speed=3, direction=0;
    bool rainbow = false;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const char* a = args[i].c_str();
        std::string val;
        if (eq(a, "-h", "--help")) {
            std::cout <<
                "Usage: ajazz light [-m mode] [-r R] [-g G] [-b B] [--rainbow]\n"
                "                   [--brightness N] [--speed N] [--direction N]\n"
                "\n"
                "Options:\n"
                "  -m, --mode <name>    Lighting mode (default: static)\n"
                "  -r, --red <0-255>    Red (default: 255)\n"
                "  -g, --green <0-255>  Green (default: 255)\n"
                "  -b, --blue <0-255>   Blue (default: 255)\n"
                "  --rainbow            Multicolour cycling (overrides RGB)\n"
                "  --brightness <0-5>   Brightness (default: 5)\n"
                "  --speed <0-5>        Animation speed (default: 3)\n"
                "  --direction <0-3>    0=left 1=down 2=up 3=right (default: 0)\n"
                "  -h, --help           Show this help\n"
                "\n"
                "Modes: off, static, singleon, singleoff, glitter, falling, colourful,\n"
                "       breath, spectrum, outward, scrolling, rolling, rotating, explode,\n"
                "       launch, ripples, flowing, pulsating, tilt, shuttle  (or 0x00-0x13)\n"
                "\n"
                "Examples:\n"
                "  ajazz light -m static -r 255 -g 0 -b 0\n"
                "  ajazz light -m breath --brightness 3\n"
                "  ajazz light -m spectrum --rainbow\n"
                "  ajazz light -m off\n";
            return;
        }
        else if (take_opt(args, i, "-m", "--mode",        val)) mode_str  = val;
        else if (take_opt(args, i, "-r", "--red",         val)) r         = parse_int("--red",         val, 0, 255);
        else if (take_opt(args, i, "-g", "--green",       val)) g         = parse_int("--green",       val, 0, 255);
        else if (take_opt(args, i, "-b", "--blue",        val)) b         = parse_int("--blue",        val, 0, 255);
        else if (take_opt(args, i, nullptr, "--brightness",val)) brightness = parse_int("--brightness", val, 0, 5);
        else if (take_opt(args, i, nullptr, "--speed",    val)) speed     = parse_int("--speed",       val, 0, 5);
        else if (take_opt(args, i, nullptr, "--direction",val)) direction = parse_int("--direction",   val, 0, 3);
        else if (eq(a, "--rainbow")) rainbow = true;
        else die("unknown option: " + args[i]);
    }

    ajazz::LightOptions opts;
    if (!ajazz::parse_light_mode(mode_str, opts.mode))
        die("unknown lighting mode '" + mode_str + "'");
    opts.r          = static_cast<uint8_t>(r);
    opts.g          = static_cast<uint8_t>(g);
    opts.b          = static_cast<uint8_t>(b);
    opts.rainbow    = rainbow;
    opts.brightness = static_cast<uint8_t>(brightness);
    opts.speed      = static_cast<uint8_t>(speed);
    opts.direction  = static_cast<uint8_t>(direction);

    open_and_run(verbose, quiet,
        [](ajazz::Keyboard& kbd, const void* ctx) {
            kbd.set_lighting(*static_cast<const ajazz::LightOptions*>(ctx));
        }, &opts);
}

// ---------------------------------------------------------------------------
// solid
// ---------------------------------------------------------------------------

static void cmd_solid(const std::vector<std::string>& args, bool verbose, bool quiet)
{
    int r=255, g=0, b=0;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const char* a = args[i].c_str();
        std::string val;
        if (eq(a, "-h", "--help")) {
            std::cout <<
                "Usage: ajazz solid [-r R] [-g G] [-b B]\n"
                "\n"
                "Fill the 240x135 display with a solid colour.\n"
                "\n"
                "Options:\n"
                "  -r, --red <0-255>    Red (default: 255)\n"
                "  -g, --green <0-255>  Green (default: 0)\n"
                "  -b, --blue <0-255>   Blue (default: 0)\n"
                "  -h, --help           Show this help\n"
                "\n"
                "Examples:\n"
                "  ajazz solid\n"
                "  ajazz solid -r 0 -g 255 -b 0\n"
                "  ajazz solid -r 0 -g 0 -b 0\n";
            return;
        }
        else if (take_opt(args, i, "-r", "--red",  val)) r    = parse_int("--red",   val, 0,   255);
        else if (take_opt(args, i, "-g", "--green",val)) g    = parse_int("--green", val, 0,   255);
        else if (take_opt(args, i, "-b", "--blue", val)) b    = parse_int("--blue",  val, 0,   255);
        else die("unknown option: " + args[i]);
    }

    if (!quiet)
        std::cout << "Solid: RGB(" << r << "," << g << "," << b << ")\n";

    constexpr int W = imgconv::DISPLAY_W, H = imgconv::DISPLAY_H;
    uint16_t px = (static_cast<uint16_t>(r & 0xF8) << 8)
                | (static_cast<uint16_t>(g & 0xFC) << 3)
                | (static_cast<uint16_t>(b >> 3));
    uint8_t lo = px & 0xFF, hi = px >> 8;
    std::vector<uint8_t> pixels(W * H * 2);
    for (int i = 0; i < W * H; ++i) { pixels[i*2] = lo; pixels[i*2+1] = hi; }

    struct Ctx { const uint8_t* data; size_t size; };
    Ctx ctx{ pixels.data(), pixels.size() };

    open_and_run(verbose, quiet,
        [](ajazz::Keyboard& kbd, const void* p) {
            auto& c = *static_cast<const Ctx*>(p);
            kbd.send_image(c.data, c.size, /*slot=*/1, /*save=*/true, /*add_header=*/true);
        }, &ctx);
}

// ---------------------------------------------------------------------------
// image
// ---------------------------------------------------------------------------

static void cmd_image(const std::vector<std::string>& args, bool verbose, bool quiet)
{
    std::string file;
    bool fill=false;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const char* a = args[i].c_str();
        std::string val;
        if (eq(a, "-h", "--help")) {
            std::cout <<
                "Usage: ajazz image <file> [--fill]\n"
                "\n"
                "Upload a static image to the 240x135 TFT display.\n"
                "Accepts PNG, JPG, BMP, GIF (first frame), or .raw (RGB565 with header).\n"
                "\n"
                "Options:\n"
                "  --fill          Scale-to-fill and centre-crop (default: letterbox)\n"
                "  -h, --help      Show this help\n"
                "\n"
                "Examples:\n"
                "  ajazz image photo.jpg\n"
                "  ajazz image logo.png --fill\n"
                "  ajazz image pre-converted.raw\n";
            return;
        }
        else if (eq(a, "--fill"))    fill    = true;
        else if (a[0] != '-' && file.empty()) file = a;
        else die("unknown option: " + args[i]);
    }
    if (file.empty()) die("image: file argument required");

    std::vector<uint8_t> pixels;
    if (is_raw(file)) {
        std::ifstream f(file, std::ios::binary);
        if (!f) die("cannot open: " + file);
        pixels.assign(std::istreambuf_iterator<char>(f), {});
        if (pixels.size() < 256) die(".raw file too small (" + std::to_string(pixels.size()) + " bytes)");
        if (!quiet)
            std::cout << "Raw file: " << static_cast<int>(pixels[0])
                      << " frame(s), " << pixels.size() << " bytes\n";
    } else {
        try {
            auto mode = fill ? imgconv::FitMode::Fill : imgconv::FitMode::Fit;
            pixels = imgconv::load_image(file, mode, quiet);
        } catch (const std::exception& e) { die(e.what()); }
    }

    struct Ctx { const uint8_t* data; size_t size; };
    Ctx ctx{ pixels.data(), pixels.size() };

    open_and_run(verbose, quiet,
        [](ajazz::Keyboard& kbd, const void* p) {
            auto& c = *static_cast<const Ctx*>(p);
            kbd.send_image(c.data, c.size, /*slot=*/1, /*save=*/true, /*add_header=*/false);
        }, &ctx);
}

// ---------------------------------------------------------------------------
// animation
// ---------------------------------------------------------------------------

static void cmd_animation(const std::vector<std::string>& args, bool verbose, bool quiet)
{
    std::string file;
    bool fill = false;

    for (int i = 0; i < static_cast<int>(args.size()); ++i) {
        const char* a = args[i].c_str();
        std::string val;
        if (eq(a, "-h", "--help")) {
            std::cout <<
                "Usage: ajazz animation <file> [--fill]\n"
                "\n"
                "Upload an animated GIF to the 240x135 TFT display.\n"
                "Accepts animated GIF or .raw (RGB565 with header). Capped at 141 frames.\n"
                "\n"
                "Options:\n"
                "  --fill          Scale-to-fill and centre-crop (default: letterbox)\n"
                "  -h, --help      Show this help\n"
                "\n"
                "Note: the firmware writes directly to flash — all uploads persist.\n"
                "A 141-frame animation takes ~78 s to upload.\n"
                "\n"
                "Examples:\n"
                "  ajazz animation anim.gif\n"
                "  ajazz animation anim.gif --fill\n"
                "  ajazz animation pre-converted.raw\n";
            return;
        }
        else if (eq(a, "--fill")) fill = true;
        else if (a[0] != '-' && file.empty()) file = a;
        else die("unknown option: " + args[i]);
    }
    if (file.empty()) die("animation: file argument required");

    std::vector<uint8_t> pixels;
    if (is_raw(file)) {
        std::ifstream f(file, std::ios::binary);
        if (!f) die("cannot open: " + file);
        pixels.assign(std::istreambuf_iterator<char>(f), {});
        if (pixels.size() < 256) die(".raw file too small (" + std::to_string(pixels.size()) + " bytes)");
        int file_frames = static_cast<int>(pixels[0]);
        int use_frames  = std::min(file_frames, imgconv::MAX_FRAMES);
        if (use_frames < file_frames) {
            pixels.resize(256 + static_cast<size_t>(use_frames) * imgconv::FRAME_BYTES);
            pixels[0] = static_cast<uint8_t>(use_frames);
            if (!quiet)
                std::cout << "  Note: truncated from " << file_frames
                          << " to " << use_frames << " frames (141-frame limit)\n";
        }
        if (!quiet)
            std::cout << "Raw file: " << use_frames << " frame(s), "
                      << pixels.size() << " bytes\n";
    } else {
        try {
            auto mode = fill ? imgconv::FitMode::Fill : imgconv::FitMode::Fit;
            pixels = imgconv::load_animation(file, mode, imgconv::MAX_FRAMES, quiet);
        } catch (const std::exception& e) { die(e.what()); }
    }

    if (!quiet) {
        size_t chunks = (pixels.size() + 4095) / 4096;
        std::cout << "Upload: " << static_cast<int>(pixels[0]) << " frame(s), "
                  << pixels.size() / 1024 << " KB, " << chunks << " chunks"
                  << " (~" << static_cast<int>(chunks * 0.035 + 0.5) << " s)\n";
    }

    struct Ctx { const uint8_t* data; size_t size; };
    Ctx ctx{ pixels.data(), pixels.size() };

    open_and_run(verbose, quiet,
        [](ajazz::Keyboard& kbd, const void* p) {
            auto& c = *static_cast<const Ctx*>(p);
            kbd.send_image(c.data, c.size, /*slot=*/1, /*save=*/true, /*add_header=*/false);
        }, &ctx);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    bool verbose = false, quiet = false;
    std::string subcmd;
    std::vector<std::string> rest;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (eq(a, "-v", "--verbose"))                verbose = true;
        else if (eq(a, "-q", "--quiet"))                  quiet   = true;
        else if (subcmd.empty() && eq(a, "-h", "--help")) { usage_global(); return 0; }
        else if (subcmd.empty() && a[0] == '-')           die("unknown option: " + std::string(a));
        else if (subcmd.empty())                          subcmd = a;
        else                                              rest.push_back(a);
    }
    // Collect remaining args that came after the subcommand
    // (already done above — options after subcmd go into rest via the loop)

    if (subcmd.empty()) { usage_global(); return 1; }

    if      (subcmd == "list")      cmd_list(rest);
    else if (subcmd == "time")      cmd_time(rest, verbose, quiet);
    else if (subcmd == "light")     cmd_light(rest, verbose, quiet);
    else if (subcmd == "solid")     cmd_solid(rest, verbose, quiet);
    else if (subcmd == "image")     cmd_image(rest, verbose, quiet);
    else if (subcmd == "animation") cmd_animation(rest, verbose, quiet);
    else die("unknown command: " + subcmd);

    return 0;
}
