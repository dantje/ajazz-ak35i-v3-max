#include "ajazz.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/hidraw.h>   // HIDIOCSFEATURE, HIDIOCGFEATURE, HIDIOCGRDESC*

namespace ajazz {

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------

namespace {

constexpr uint8_t CMD_SAVE         = 0x02;
constexpr uint8_t CMD_LIGHTING     = 0x13;
constexpr uint8_t CMD_CUSTOM_LIGHT = 0x23;
constexpr uint8_t CMD_START        = 0x18;
constexpr uint8_t CMD_TIME         = 0x28;
constexpr uint8_t CMD_IMAGE        = 0x72;
constexpr uint8_t CMD_DONE         = 0xF0;
constexpr int IMAGE_CHUNK_SIZE  = 4096;   // bytes per output report on FF68
constexpr int IMAGE_HEADER_SIZE = 256;    // gif_headlength from rgb-keyboard.xml

void make_cmd_packet(uint8_t* out, uint8_t cmd, uint8_t arg1 = 0, uint8_t arg2 = 1)
{
    std::memset(out, 0, PACKET_SIZE);
    out[0] = 0x04;
    out[1] = cmd;
    out[2] = arg1;
    out[8] = arg2;
}

// Mandatory inter-report gap — the SONiX firmware silently drops commands
// that arrive too soon.  The Windows driver uses 35 ms (cmd_delaytime in
// rgb-keyboard.xml).
void fw_delay()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
}

// ---------------------------------------------------------------------------
// Read the first Usage Page value from a hidraw device's HID report descriptor.
//
// HID short item encoding (HID spec §6.2.2.2):
//   byte = bTag[7:4] | bType[3:2] | bSize[1:0]
// Usage Page is a Global item (bType=01) with bTag=0000:
//   item & 0xFC == 0x04
// bSize: 01 = 1 byte follows, 10 = 2 bytes follow (little-endian).
// ---------------------------------------------------------------------------
uint16_t read_usage_page(int fd)
{
    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0 || desc_size <= 0)
        return 0;

    struct hidraw_report_descriptor desc = {};
    desc.size = static_cast<uint32_t>(desc_size);
    if (ioctl(fd, HIDIOCGRDESC, &desc) < 0)
        return 0;

    for (int i = 0; i < desc_size; ) {
        uint8_t item = desc.value[i++];
        int size = item & 0x03;

        if ((item & 0xFC) == 0x04) {          // Usage Page global item
            if (size == 1 && i < desc_size)
                return desc.value[i];
            if (size == 2 && i + 1 < desc_size)
                return static_cast<uint16_t>(desc.value[i]) |
                       (static_cast<uint16_t>(desc.value[i + 1]) << 8);
        }
        i += size;
    }
    return 0;
}

// Extract interface number from a HID_PHYS string like
// "usb-0000:00:1d.0-1.4/input3" → 3.  Returns -1 on failure.
int parse_interface_num(const std::string& phys)
{
    auto pos = phys.rfind("/input");
    if (pos == std::string::npos) return -1;
    try {
        return std::stoi(phys.substr(pos + 6));
    } catch (...) {
        return -1;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// discover()
// ---------------------------------------------------------------------------

std::vector<DeviceInfo> discover()
{
    namespace fs = std::filesystem;

    char vp[24];
    std::snprintf(vp, sizeof(vp), "%08X:%08X", VID, PID);
    const std::string needle(vp);   // e.g. "00000C45:00008009"

    const fs::path root{"/sys/class/hidraw"};
    if (!fs::exists(root))
        return {};

    std::vector<DeviceInfo> results;

    for (const auto& entry : fs::directory_iterator(root)) {
        std::ifstream f(entry.path() / "device" / "uevent");
        if (!f) continue;

        bool vid_pid_ok = false;
        int  iface_num  = -1;

        std::string line;
        while (std::getline(f, line)) {
            std::string upper = line;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

            if (upper.find(needle) != std::string::npos)
                vid_pid_ok = true;

            if (line.rfind("HID_PHYS=", 0) == 0)
                iface_num = parse_interface_num(line.substr(9));
        }

        if (!vid_pid_ok) continue;

        std::string dev_path = "/dev/" + entry.path().filename().string();

        // Open the device just long enough to read its report descriptor.
        uint16_t usage_page = 0;
        int fd = ::open(dev_path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            usage_page = read_usage_page(fd);
            ::close(fd);
        }

        results.push_back({dev_path, usage_page, iface_num});
    }

    // Sort by interface number for deterministic output
    std::sort(results.begin(), results.end(),
              [](const DeviceInfo& a, const DeviceInfo& b) {
                  return a.interface_num < b.interface_num;
              });

    return results;
}

// ---------------------------------------------------------------------------
// Light mode name table
// ---------------------------------------------------------------------------

bool parse_light_mode(const std::string& s, LightMode& out)
{
    static const std::unordered_map<std::string, LightMode> table = {
        {"off",       LightMode::Off},
        {"static",    LightMode::Static},
        {"singleon",  LightMode::SingleOn},
        {"singleoff", LightMode::SingleOff},
        {"glitter",   LightMode::Glitter},
        {"falling",   LightMode::Falling},
        {"colourful", LightMode::Colourful},
        {"colorful",  LightMode::Colourful},
        {"breath",    LightMode::Breath},
        {"spectrum",  LightMode::Spectrum},
        {"outward",   LightMode::Outward},
        {"scrolling", LightMode::Scrolling},
        {"rolling",   LightMode::Rolling},
        {"rotating",  LightMode::Rotating},
        {"explode",   LightMode::Explode},
        {"launch",    LightMode::Launch},
        {"ripples",   LightMode::Ripples},
        {"flowing",   LightMode::Flowing},
        {"pulsating", LightMode::Pulsating},
        {"tilt",      LightMode::Tilt},
        {"shuttle",   LightMode::Shuttle},
        {"custom",    LightMode::Custom},
        {"perkey",    LightMode::PerKey},
        {"per-key",   LightMode::PerKey},
    };

    if (!s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) ||
                       (s.size() > 2 && s[0] == '0' && s[1] == 'x'))) {
        try {
            unsigned long v = std::stoul(s, nullptr, 0);
            if (v <= 0xFF) { out = static_cast<LightMode>(v); return true; }
        } catch (...) {}
        return false;
    }

    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto it = table.find(lower);
    if (it != table.end()) { out = it->second; return true; }
    return false;
}

std::string light_mode_name(LightMode m)
{
    switch (m) {
    case LightMode::Off:       return "off";
    case LightMode::Static:    return "static";
    case LightMode::SingleOn:  return "singleon";
    case LightMode::SingleOff: return "singleoff";
    case LightMode::Glitter:   return "glitter";
    case LightMode::Falling:   return "falling";
    case LightMode::Colourful: return "colourful";
    case LightMode::Breath:    return "breath";
    case LightMode::Spectrum:  return "spectrum";
    case LightMode::Outward:   return "outward";
    case LightMode::Scrolling: return "scrolling";
    case LightMode::Rolling:   return "rolling";
    case LightMode::Rotating:  return "rotating";
    case LightMode::Explode:   return "explode";
    case LightMode::Launch:    return "launch";
    case LightMode::Ripples:   return "ripples";
    case LightMode::Flowing:   return "flowing";
    case LightMode::Pulsating: return "pulsating";
    case LightMode::Tilt:      return "tilt";
    case LightMode::Shuttle:   return "shuttle";
    case LightMode::Custom:    return "custom";
    default: {
        char buf[8]; std::snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(m));
        return buf;
    }
    }
}

// ---------------------------------------------------------------------------
// Key index table  (light_index values, 1-based)
// ---------------------------------------------------------------------------

static const std::vector<std::pair<std::string, uint8_t>>& key_index_table()
{
    static const std::vector<std::pair<std::string, uint8_t>> t = {
        // Function row
        {"esc",        1}, {"escape",     1},
        {"f1",         2}, {"f2",         3}, {"f3",         4}, {"f4",         5},
        {"f5",         6}, {"f6",         7}, {"f7",         8}, {"f8",         9},
        {"f9",        10}, {"f10",       11}, {"f11",       12}, {"f12",       13},
        {"print",    112}, {"prtsc",    112}, {"printscreen", 112},
        {"scrolllock",113}, {"scrlk",   113},
        {"pause",    115},
        // Number row
        {"grave",     19}, {"`",         19}, {"~",         19},
        {"1",         20}, {"2",         21}, {"3",         22}, {"4",         23},
        {"5",         24}, {"6",         25}, {"7",         26}, {"8",         27},
        {"9",         28}, {"0",         29},
        {"-",         30}, {"minus",     30},
        {"=",         31}, {"equals",    31},
        {"backspace", 103}, {"bksp",    103},
        {"insert",   116}, {"ins",      116},
        {"home",     117},
        {"pgup",     118}, {"pageup",   118},
        // Numpad top row
        {"numlock",   32}, {"nlck",      32},
        {"num/",      33}, {"numdiv",    33},
        {"num*",      34}, {"nummul",    34},
        {"num-",     122}, {"numsub",   122},
        // QWERTY row
        {"tab",       37},
        {"q",         38}, {"w",         39}, {"e",         40}, {"r",         41},
        {"t",         42}, {"y",         43}, {"u",         44}, {"i",         45},
        {"o",         46}, {"p",         47},
        {"[",         48}, {"lbracket",  48},
        {"]",         49}, {"rbracket",  49},
        {"\\",        67}, {"backslash", 67}, {"|",         67},
        {"delete",   119}, {"del",      119},
        {"end",      120},
        {"pgdn",     121}, {"pagedown", 121},
        // Numpad row 2
        {"num7",      50}, {"num8",      51}, {"num9",      52},
        {"num+",     123}, {"numadd",   123},
        // Home row
        {"capslock",  55}, {"caps",      55},
        {"a",         56}, {"s",         57}, {"d",         58}, {"f",         59},
        {"g",         60}, {"h",         61}, {"j",         62}, {"k",         63},
        {"l",         64},
        {";",         65}, {"semicolon", 65},
        {"'",         66}, {"quote",     66},
        {"enter",     85}, {"return",    85},
        // Numpad row 3
        {"num4",      68}, {"num5",      69}, {"num6",      70},
        // Shift row
        {"lshift",    73}, {"shift_l",   73},
        {"z",         74}, {"x",         75}, {"c",         76}, {"v",         77},
        {"b",         78}, {"n",         79}, {"m",         80},
        {",",         81}, {"comma",     81},
        {".",         82}, {"period",    82},
        {"/",         83}, {"slash",     83},
        {"rshift",    84}, {"shift_r",   84},
        {"up",       101}, {"uparrow",  101},
        // Numpad row 4
        {"num1",      86}, {"num2",      87}, {"num3",      88},
        {"numenter",  106},
        // Bottom row
        {"lctrl",     91}, {"ctrl_l",    91}, {"ctrl",      91},
        {"lwin",      92}, {"win_l",     92}, {"win",       92}, {"super",     92},
        {"lalt",      93}, {"alt_l",     93}, {"alt",       93},
        {"space",     94},
        {"ralt",      95}, {"alt_r",     95},
        {"fn",        96},
        {"app",       97}, {"menu",      97},
        {"rctrl",     98}, {"ctrl_r",    98},
        {"left",      99}, {"leftarrow",  99},
        {"down",     100}, {"downarrow", 100},
        {"right",    102}, {"rightarrow",102},
        // Numpad bottom
        {"num0",     104},
        {"numdot",   105}, {"numdel",   105},
    };
    return t;
}

bool parse_key_index(const std::string& s, uint8_t& out)
{
    if (s.empty()) return false;

    // Try numeric first (decimal or hex)
    if (std::isdigit(static_cast<unsigned char>(s[0])) ||
        (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
        try {
            unsigned long v = std::stoul(s, nullptr, 0);
            if (v >= 1 && v <= 189) { out = static_cast<uint8_t>(v); return true; }
        } catch (...) {}
        return false;
    }

    // Try key name (case-insensitive)
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    for (const auto& [name, idx] : key_index_table()) {
        if (name == lower) { out = idx; return true; }
    }
    return false;
}

std::string index_to_key_name(uint8_t light_index)
{
    std::string best;
    for (const auto& [name, idx] : key_index_table()) {
        if (idx == light_index) {
            if (best.empty() || name.size() < best.size())
                best = name;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Key groups
// ---------------------------------------------------------------------------

static const std::vector<std::pair<std::string, std::vector<std::string>>>&
key_groups_table()
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> t = {
        { "frow",    { "esc","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12" } },
        { "numrow",  { "`","1","2","3","4","5","6","7","8","9","0","-","=" } },
        { "qrow",    { "tab","q","w","e","r","t","y","u","i","o","p","[","]","\\" } },
        { "homerow", { "caps","a","s","d","f","g","h","j","k","l",";","'","enter" } },
        { "shiftrow",{ "lshift","z","x","c","v","b","n","m",",",".","/","rshift" } },
        { "bottom",  { "lctrl","win","lalt","space","ralt","fn","app","rctrl" } },
        { "arrows",  { "left","down","up","right" } },
        { "nav",     { "ins","home","pgup","del","end","pgdn" } },
        { "syskeys", { "print","scrolllock","pause" } },
        { "numpad",  { "numlock","num/","num*","num7","num8","num9","num-",
                       "num4","num5","num6","num+","num1","num2","num3",
                       "numenter","num0","numdot" } },
        { "wasd",    { "w","a","s","d" } },
        { "alphas",  { "q","w","e","r","t","y","u","i","o","p",
                       "a","s","d","f","g","h","j","k","l",
                       "z","x","c","v","b","n","m" } },
        { "mods",    { "lshift","rshift","lctrl","rctrl","lalt","ralt","win","fn","caps" } },
        { "all",     { "esc","f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12",
                       "print","scrolllock","pause",
                       "`","1","2","3","4","5","6","7","8","9","0","-","=","backspace",
                       "ins","home","pgup",
                       "numlock","num/","num*","num-",
                       "tab","q","w","e","r","t","y","u","i","o","p","[","]","\\",
                       "del","end","pgdn",
                       "num7","num8","num9","num+",
                       "caps","a","s","d","f","g","h","j","k","l",";","'","enter",
                       "num4","num5","num6",
                       "lshift","z","x","c","v","b","n","m",",",".","/","rshift","up",
                       "num1","num2","num3","numenter",
                       "lctrl","win","lalt","space","ralt","fn","app","rctrl",
                       "left","down","right",
                       "num0","numdot" } },
    };
    return t;
}

bool resolve_key_set(const std::string& spec, std::vector<uint8_t>& out)
{
    // Check named group first (exact match, case-insensitive)
    {
        std::string lower = spec;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        for (const auto& [gname, members] : key_groups_table()) {
            if (gname == lower) {
                for (const auto& m : members) {
                    uint8_t idx;
                    if (parse_key_index(m, idx))
                        out.push_back(idx);
                }
                return true;
            }
        }
    }

    // Split on commas and resolve each token
    std::istringstream ss(spec);
    std::string token;
    bool ok = true;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        uint8_t idx;
        if (!parse_key_index(token, idx)) { ok = false; break; }
        out.push_back(idx);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// HSV to RGB  (QMK-style 8-bit)
// ---------------------------------------------------------------------------

void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                uint8_t& r, uint8_t& g, uint8_t& b)
{
    if (s == 0) { r = g = b = v; return; }
    uint8_t region    = h / 43;
    uint8_t remainder = (h - region * 43) * 6;
    uint8_t p = static_cast<uint8_t>((uint16_t)v * (255 - s) / 255);
    uint8_t q = static_cast<uint8_t>((uint16_t)v * (255 - ((uint16_t)s * remainder >> 8)) / 255);
    uint8_t t = static_cast<uint8_t>((uint16_t)v * (255 - ((uint16_t)s * (255 - remainder) >> 8)) / 255);
    switch (region) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default: r=v; g=p; b=q; break;
    }
}

// ---------------------------------------------------------------------------
// CSS color names  (CSS Color Level 4 — 148 names + "off" alias)
// ---------------------------------------------------------------------------

bool parse_color_name(const std::string& s, uint8_t& r, uint8_t& g, uint8_t& b)
{
    // "#RRGGBB" or bare "RRGGBB" hex notation
    if ((s.size() == 7 && s[0] == '#') || s.size() == 6) {
        const std::string hex = (s[0] == '#') ? s.substr(1) : s;
        bool all_hex = true;
        for (char c : hex)
            if (!std::isxdigit(static_cast<unsigned char>(c))) { all_hex = false; break; }
        if (all_hex) {
            try {
                unsigned v = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                r = (v >> 16) & 0xFF;
                g = (v >>  8) & 0xFF;
                b =  v        & 0xFF;
                return true;
            } catch (...) {}
        }
    }

    static const std::unordered_map<std::string, uint32_t> css = {
        {"aliceblue",            0xF0F8FF}, {"antiquewhite",         0xFAEBD7},
        {"aqua",                 0x00FFFF}, {"aquamarine",           0x7FFFD4},
        {"azure",                0xF0FFFF}, {"beige",                0xF5F5DC},
        {"bisque",               0xFFE4C4}, {"black",                0x000000},
        {"blanchedalmond",       0xFFEBCD}, {"blue",                 0x0000FF},
        {"blueviolet",           0x8A2BE2}, {"brown",                0xA52A2A},
        {"burlywood",            0xDEB887}, {"cadetblue",            0x5F9EA0},
        {"chartreuse",           0x7FFF00}, {"chocolate",            0xD2691E},
        {"coral",                0xFF7F50}, {"cornflowerblue",       0x6495ED},
        {"cornsilk",             0xFFF8DC}, {"crimson",              0xDC143C},
        {"cyan",                 0x00FFFF}, {"darkblue",             0x00008B},
        {"darkcyan",             0x008B8B}, {"darkgoldenrod",        0xB8860B},
        {"darkgray",             0xA9A9A9}, {"darkgreen",            0x006400},
        {"darkgrey",             0xA9A9A9}, {"darkkhaki",            0xBDB76B},
        {"darkmagenta",          0x8B008B}, {"darkolivegreen",       0x556B2F},
        {"darkorange",           0xFF8C00}, {"darkorchid",           0x9932CC},
        {"darkred",              0x8B0000}, {"darksalmon",           0xE9967A},
        {"darkseagreen",         0x8FBC8F}, {"darkslateblue",        0x483D8B},
        {"darkslategray",        0x2F4F4F}, {"darkslategrey",        0x2F4F4F},
        {"darkturquoise",        0x00CED1}, {"darkviolet",           0x9400D3},
        {"deeppink",             0xFF1493}, {"deepskyblue",          0x00BFFF},
        {"dimgray",              0x696969}, {"dimgrey",              0x696969},
        {"dodgerblue",           0x1E90FF}, {"firebrick",            0xB22222},
        {"floralwhite",          0xFFFAF0}, {"forestgreen",          0x228B22},
        {"fuchsia",              0xFF00FF}, {"gainsboro",            0xDCDCDC},
        {"ghostwhite",           0xF8F8FF}, {"gold",                 0xFFD700},
        {"goldenrod",            0xDAA520}, {"gray",                 0x808080},
        {"green",                0x008000}, {"greenyellow",          0xADFF2F},
        {"grey",                 0x808080}, {"honeydew",             0xF0FFF0},
        {"hotpink",              0xFF69B4}, {"indianred",            0xCD5C5C},
        {"indigo",               0x4B0082}, {"ivory",                0xFFFFF0},
        {"khaki",                0xF0E68C}, {"lavender",             0xE6E6FA},
        {"lavenderblush",        0xFFF0F5}, {"lawngreen",            0x7CFC00},
        {"lemonchiffon",         0xFFFACD}, {"lightblue",            0xADD8E6},
        {"lightcoral",           0xF08080}, {"lightcyan",            0xE0FFFF},
        {"lightgoldenrodyellow", 0xFAFAD2}, {"lightgray",            0xD3D3D3},
        {"lightgreen",           0x90EE90}, {"lightgrey",            0xD3D3D3},
        {"lightpink",            0xFFB6C1}, {"lightsalmon",          0xFFA07A},
        {"lightseagreen",        0x20B2AA}, {"lightskyblue",         0x87CEFA},
        {"lightslategray",       0x778899}, {"lightslategrey",       0x778899},
        {"lightsteelblue",       0xB0C4DE}, {"lightyellow",          0xFFFFE0},
        {"lime",                 0x00FF00}, {"limegreen",            0x32CD32},
        {"linen",                0xFAF0E6}, {"magenta",              0xFF00FF},
        {"maroon",               0x800000}, {"mediumaquamarine",     0x66CDAA},
        {"mediumblue",           0x0000CD}, {"mediumorchid",         0xBA55D3},
        {"mediumpurple",         0x9370DB}, {"mediumseagreen",       0x3CB371},
        {"mediumslateblue",      0x7B68EE}, {"mediumspringgreen",    0x00FA9A},
        {"mediumturquoise",      0x48D1CC}, {"mediumvioletred",      0xC71585},
        {"midnightblue",         0x191970}, {"mintcream",            0xF5FFFA},
        {"mistyrose",            0xFFE4E1}, {"moccasin",             0xFFE4B5},
        {"navajowhite",          0xFFDEAD}, {"navy",                 0x000080},
        {"oldlace",              0xFDF5E6}, {"olive",                0x808000},
        {"olivedrab",            0x6B8E23}, {"orange",               0xFFA500},
        {"orangered",            0xFF4500}, {"orchid",               0xDA70D6},
        {"palegoldenrod",        0xEEE8AA}, {"palegreen",            0x98FB98},
        {"paleturquoise",        0xAFEEEE}, {"palevioletred",        0xDB7093},
        {"papayawhip",           0xFFEFD5}, {"peachpuff",            0xFFDAB9},
        {"peru",                 0xCD853F}, {"pink",                 0xFFC0CB},
        {"plum",                 0xDDA0DD}, {"powderblue",           0xB0E0E6},
        {"purple",               0x800080}, {"rebeccapurple",        0x663399},
        {"red",                  0xFF0000}, {"rosybrown",            0xBC8F8F},
        {"royalblue",            0x4169E1}, {"saddlebrown",          0x8B4513},
        {"salmon",               0xFA8072}, {"sandybrown",           0xF4A460},
        {"seagreen",             0x2E8B57}, {"seashell",             0xFFF5EE},
        {"sienna",               0xA0522D}, {"silver",               0xC0C0C0},
        {"skyblue",              0x87CEEB}, {"slateblue",            0x6A5ACD},
        {"slategray",            0x708090}, {"slategrey",            0x708090},
        {"snow",                 0xFFFAFA}, {"springgreen",          0x00FF7F},
        {"steelblue",            0x4682B4}, {"tan",                  0xD2B48C},
        {"teal",                 0x008080}, {"thistle",              0xD8BFD8},
        {"tomato",               0xFF6347}, {"turquoise",            0x40E0D0},
        {"violet",               0xEE82EE}, {"wheat",                0xF5DEB3},
        {"white",                0xFFFFFF}, {"whitesmoke",           0xF5F5F5},
        {"yellow",               0xFFFF00}, {"yellowgreen",          0x9ACD32},
        {"off",                  0x000000},
    };

    std::string lower = s;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto it = css.find(lower);
    if (it == css.end()) return false;
    r = (it->second >> 16) & 0xFF;
    g = (it->second >>  8) & 0xFF;
    b =  it->second        & 0xFF;
    return true;
}

std::string color_name_from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    // The firmware applies per-channel brightness transforms (level 5):
    //   R: v * 252 / 255    G: (v * 254) >> 8    B: (v * 254) >> 8
    // The reverse map is pre-cooked by applying this transform to each CSS
    // color, so CSS names are recognised exactly after the firmware round-trip.
    static const std::unordered_map<uint32_t, std::string> rev = []() {
        auto fwR = [](uint32_t v) -> uint32_t { return v * 252u / 255u; };
        auto fwG = [](uint32_t v) -> uint32_t { return (v * 254u) >> 8; };
        auto fwB = [](uint32_t v) -> uint32_t { return (v * 254u) >> 8; };
        // Ordered pairs: first occurrence wins for a given RGB value.
        static const std::pair<uint32_t, const char*> order[] = {
            {0xF0F8FF,"aliceblue"},      {0xFAEBD7,"antiquewhite"},
            {0x00FFFF,"cyan"},           {0x7FFFD4,"aquamarine"},
            {0xF0FFFF,"azure"},          {0xF5F5DC,"beige"},
            {0xFFE4C4,"bisque"},         {0x000000,"black"},
            {0xFFEBCD,"blanchedalmond"}, {0x0000FF,"blue"},
            {0x8A2BE2,"blueviolet"},     {0xA52A2A,"brown"},
            {0xDEB887,"burlywood"},      {0x5F9EA0,"cadetblue"},
            {0x7FFF00,"chartreuse"},     {0xD2691E,"chocolate"},
            {0xFF7F50,"coral"},          {0x6495ED,"cornflowerblue"},
            {0xFFF8DC,"cornsilk"},       {0xDC143C,"crimson"},
            {0x00008B,"darkblue"},       {0x008B8B,"darkcyan"},
            {0xB8860B,"darkgoldenrod"},  {0xA9A9A9,"darkgray"},
            {0x006400,"darkgreen"},      {0xBDB76B,"darkkhaki"},
            {0x8B008B,"darkmagenta"},    {0x556B2F,"darkolivegreen"},
            {0xFF8C00,"darkorange"},     {0x9932CC,"darkorchid"},
            {0x8B0000,"darkred"},        {0xE9967A,"darksalmon"},
            {0x8FBC8F,"darkseagreen"},   {0x483D8B,"darkslateblue"},
            {0x2F4F4F,"darkslategray"},  {0x00CED1,"darkturquoise"},
            {0x9400D3,"darkviolet"},     {0xFF1493,"deeppink"},
            {0x00BFFF,"deepskyblue"},    {0x696969,"dimgray"},
            {0x1E90FF,"dodgerblue"},     {0xB22222,"firebrick"},
            {0xFFFAF0,"floralwhite"},    {0x228B22,"forestgreen"},
            {0xFF00FF,"magenta"},        {0xDCDCDC,"gainsboro"},
            {0xF8F8FF,"ghostwhite"},     {0xFFD700,"gold"},
            {0xDAA520,"goldenrod"},      {0x808080,"gray"},
            {0x008000,"green"},          {0xADFF2F,"greenyellow"},
            {0xF0FFF0,"honeydew"},       {0xFF69B4,"hotpink"},
            {0xCD5C5C,"indianred"},      {0x4B0082,"indigo"},
            {0xFFFFF0,"ivory"},          {0xF0E68C,"khaki"},
            {0xE6E6FA,"lavender"},       {0xFFF0F5,"lavenderblush"},
            {0x7CFC00,"lawngreen"},      {0xFFFACD,"lemonchiffon"},
            {0xADD8E6,"lightblue"},      {0xF08080,"lightcoral"},
            {0xE0FFFF,"lightcyan"},      {0xFAFAD2,"lightgoldenrodyellow"},
            {0xD3D3D3,"lightgray"},      {0x90EE90,"lightgreen"},
            {0xFFB6C1,"lightpink"},      {0xFFA07A,"lightsalmon"},
            {0x20B2AA,"lightseagreen"},  {0x87CEFA,"lightskyblue"},
            {0x778899,"lightslategray"}, {0xB0C4DE,"lightsteelblue"},
            {0xFFFFE0,"lightyellow"},    {0x00FF00,"lime"},
            {0x32CD32,"limegreen"},      {0xFAF0E6,"linen"},
            {0x800000,"maroon"},         {0x66CDAA,"mediumaquamarine"},
            {0x0000CD,"mediumblue"},     {0xBA55D3,"mediumorchid"},
            {0x9370DB,"mediumpurple"},   {0x3CB371,"mediumseagreen"},
            {0x7B68EE,"mediumslateblue"},{0x00FA9A,"mediumspringgreen"},
            {0x48D1CC,"mediumturquoise"},{0xC71585,"mediumvioletred"},
            {0x191970,"midnightblue"},   {0xF5FFFA,"mintcream"},
            {0xFFE4E1,"mistyrose"},      {0xFFE4B5,"moccasin"},
            {0xFFDEAD,"navajowhite"},    {0x000080,"navy"},
            {0xFDF5E6,"oldlace"},        {0x808000,"olive"},
            {0x6B8E23,"olivedrab"},      {0xFFA500,"orange"},
            {0xFF4500,"orangered"},      {0xDA70D6,"orchid"},
            {0xEEE8AA,"palegoldenrod"},  {0x98FB98,"palegreen"},
            {0xAFEEEE,"paleturquoise"},  {0xDB7093,"palevioletred"},
            {0xFFEFD5,"papayawhip"},     {0xFFDAB9,"peachpuff"},
            {0xCD853F,"peru"},           {0xFFC0CB,"pink"},
            {0xDDA0DD,"plum"},           {0xB0E0E6,"powderblue"},
            {0x800080,"purple"},         {0x663399,"rebeccapurple"},
            {0xFF0000,"red"},            {0xBC8F8F,"rosybrown"},
            {0x4169E1,"royalblue"},      {0x8B4513,"saddlebrown"},
            {0xFA8072,"salmon"},         {0xF4A460,"sandybrown"},
            {0x2E8B57,"seagreen"},       {0xFFF5EE,"seashell"},
            {0xA0522D,"sienna"},         {0xC0C0C0,"silver"},
            {0x87CEEB,"skyblue"},        {0x6A5ACD,"slateblue"},
            {0x708090,"slategray"},      {0xFFFAFA,"snow"},
            {0x00FF7F,"springgreen"},    {0x4682B4,"steelblue"},
            {0xD2B48C,"tan"},            {0x008080,"teal"},
            {0xD8BFD8,"thistle"},        {0xFF6347,"tomato"},
            {0x40E0D0,"turquoise"},      {0xEE82EE,"violet"},
            {0xF5DEB3,"wheat"},          {0xFFFFFF,"white"},
            {0xF5F5F5,"whitesmoke"},     {0xFFFF00,"yellow"},
            {0x9ACD32,"yellowgreen"},
        };
        std::unordered_map<uint32_t, std::string> m;
        for (const auto& [rgb, name] : order) {
            uint32_t fr = fwR((rgb >> 16) & 0xFF);
            uint32_t fg = fwG((rgb >>  8) & 0xFF);
            uint32_t fb = fwB( rgb        & 0xFF);
            uint32_t key = (fr << 16) | (fg << 8) | fb;
            if (m.find(key) == m.end())
                m[key] = name;
        }
        return m;
    }();

    uint32_t key = (static_cast<uint32_t>(r) << 16) |
                   (static_cast<uint32_t>(g) << 8) | b;
    auto it = rev.find(key);
    return (it != rev.end()) ? it->second : std::string{};
}

// ---------------------------------------------------------------------------
// Keyboard implementation
// ---------------------------------------------------------------------------

Keyboard::Keyboard(bool verbose, bool quiet)
    : _verbose(verbose), _quiet(quiet), _cmd_fd(-1), _disp_fd(-1)
{}

Keyboard::~Keyboard()
{
    close();
}

void Keyboard::_log(const std::string& msg) const
{
    if (_verbose && !_quiet) std::cout << msg << '\n';
}

void Keyboard::open()
{
    auto interfaces = discover();
    if (interfaces.empty())
        throw std::runtime_error(
            "Ajazz keyboard not found (VID=0x0C45 PID=0x8009) — "
            "is it plugged in with the back switch set to USB?");

    std::string cmd_path, disp_path;
    for (const auto& d : interfaces) {
        if (d.usage_page == USAGE_PAGE_CMD  && cmd_path.empty())  cmd_path  = d.path;
        if (d.usage_page == USAGE_PAGE_DISP && disp_path.empty()) disp_path = d.path;
    }

    if (cmd_path.empty())
        throw std::runtime_error(
            "Command interface (usage page 0xFF13) not found among discovered interfaces");
    if (disp_path.empty())
        throw std::runtime_error(
            "Display interface (usage page 0xFF68) not found among discovered interfaces");

    _cmd_fd = ::open(cmd_path.c_str(), O_RDWR | O_CLOEXEC);
    if (_cmd_fd < 0)
        throw std::system_error(errno, std::generic_category(), "open " + cmd_path);
    _log("Opened command interface:  " + cmd_path);

    _disp_fd = ::open(disp_path.c_str(), O_RDWR | O_CLOEXEC);
    if (_disp_fd < 0) {
        ::close(_cmd_fd); _cmd_fd = -1;
        throw std::system_error(errno, std::generic_category(), "open " + disp_path);
    }
    _log("Opened display interface:  " + disp_path);
}

void Keyboard::close()
{
    if (_cmd_fd  >= 0) { ::close(_cmd_fd);  _cmd_fd  = -1; }
    if (_disp_fd >= 0) { ::close(_disp_fd); _disp_fd = -1; }
}

void Keyboard::_send_feature(const uint8_t* payload, bool read_ack) const
{
    uint8_t buf[REPORT_BUF_SIZE];
    buf[0] = 0x00;   // report-ID stripped by kernel; 64-byte payload forwarded to device
    std::memcpy(buf + 1, payload, PACKET_SIZE);

    if (ioctl(_cmd_fd, HIDIOCSFEATURE(REPORT_BUF_SIZE), buf) < 0)
        throw std::system_error(errno, std::generic_category(), "HIDIOCSFEATURE");

    fw_delay();

    if (read_ack) _read_ack();
}

std::vector<uint8_t> Keyboard::_read_ack() const
{
    uint8_t buf[REPORT_BUF_SIZE] = {};
    if (ioctl(_cmd_fd, HIDIOCGFEATURE(REPORT_BUF_SIZE), buf) < 0)
        _log("  (ACK read returned error, continuing)");
    fw_delay();
    // Return payload without the leading report-ID byte
    return std::vector<uint8_t>(buf + 1, buf + REPORT_BUF_SIZE);
}

void Keyboard::_read_disp_ack() const
{
    // The Windows driver uses ReadFile (input report) with a 300ms timeout
    // after each data chunk — NOT HidD_GetFeature.  On Linux hidraw this
    // maps to read() with poll() for timeout.  Timeout is non-fatal; the
    // read is best-effort pacing.
    struct pollfd pfd = { _disp_fd, POLLIN, 0 };
    int ret = poll(&pfd, 1, 300);   // 300ms timeout like Windows driver
    if (ret > 0) {
        uint8_t buf[64];
        ssize_t n = ::read(_disp_fd, buf, sizeof(buf));
        _log("  (display ACK: " + std::to_string(n) + " bytes)");
    } else {
        _log("  (display ACK timeout, continuing)");
    }
}

void Keyboard::set_time(const std::tm* tm_arg)
{
    std::tm t;
    if (tm_arg) {
        t = *tm_arg;
    } else {
        std::time_t now = std::time(nullptr);
        t = *std::localtime(&now);
    }

    char tbuf[32];
    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%dT%H:%M:%S", &t);
    _log(std::string("→ SET_TIME  ") + tbuf);

    uint8_t pkt[PACKET_SIZE];

    make_cmd_packet(pkt, CMD_START, 0x00, 0x01);
    _log("  → START");
    _send_feature(pkt, false);

    make_cmd_packet(pkt, CMD_TIME, 0x00, 0x01);
    _log("  → CMD_TIME");
    _send_feature(pkt, true);

    std::memset(pkt, 0, PACKET_SIZE);
    pkt[0]  = 0x00;
    pkt[1]  = 0x01;
    pkt[2]  = 0x5A;
    pkt[3]  = static_cast<uint8_t>(t.tm_year + 1900 - 2000);
    pkt[4]  = static_cast<uint8_t>(t.tm_mon + 1);
    pkt[5]  = static_cast<uint8_t>(t.tm_mday);
    pkt[6]  = static_cast<uint8_t>(t.tm_hour);
    pkt[7]  = static_cast<uint8_t>(t.tm_min);
    pkt[8]  = static_cast<uint8_t>(t.tm_sec);
    pkt[9]  = 0x00;
    pkt[10] = 0x04;   // unknown; always 0x04 in reference implementation
    pkt[62] = 0xAA;
    pkt[63] = 0x55;
    _log("  → TIME data");
    _send_feature(pkt, false);

    make_cmd_packet(pkt, CMD_SAVE, 0x00, 0x00);
    _log("  → SAVE");
    _send_feature(pkt, true);

    _log("Time sync complete.");
}

void Keyboard::set_lighting(const LightOptions& opts)
{
    uint8_t pkt[PACKET_SIZE];

    make_cmd_packet(pkt, CMD_LIGHTING, 0x00, 0x01);
    _log("  → CMD_LIGHTING");
    _send_feature(pkt, true);   // ACK required before sending data

    std::memset(pkt, 0, PACKET_SIZE);
    pkt[0]  = static_cast<uint8_t>(opts.mode);
    pkt[1]  = opts.r;
    pkt[2]  = opts.g;
    pkt[3]  = opts.b;
    pkt[8]  = opts.rainbow ? 0x01 : 0x00;
    pkt[9]  = opts.brightness;
    pkt[10] = opts.speed;
    pkt[11] = opts.direction;
    pkt[62] = 0x55;
    pkt[63] = 0xAA;
    _log("  → LIGHTING data");
    _send_feature(pkt, true);   // ACK required before SAVE

    make_cmd_packet(pkt, CMD_SAVE, 0x00, 0x00);
    _log("  → SAVE");
    _send_feature(pkt, true);

    _log("Lighting set.");
}

void Keyboard::send_image(const uint8_t* data, size_t size,
                          uint8_t slot, bool save, bool add_header)
{
    std::vector<uint8_t> buf;

    if (add_header) {
        // Prepend 256-byte header for a single static frame
        buf.resize(IMAGE_HEADER_SIZE, 0xFF);
        buf[0] = 1;    // 1 frame
        buf[1] = 1;    // frame 0 delay = 1 (minimum; ~2ms units)
        buf.insert(buf.end(), data, data + size);
    } else {
        // Data already contains header + pixel data (from load_image / load_animation)
        buf.assign(data, data + size);
    }

    const size_t total     = buf.size();
    const size_t n_full    = total / IMAGE_CHUNK_SIZE;
    const size_t remainder = total % IMAGE_CHUNK_SIZE;
    const size_t n_chunks  = n_full + (remainder ? 1 : 0);

    if (add_header) {
        _log("  Image: " + std::to_string(size) + " bytes pixel data + " +
             std::to_string(IMAGE_HEADER_SIZE) + " byte header = " +
             std::to_string(total) + " bytes total, " +
             std::to_string(n_chunks) + " chunks");
    } else {
        int n_frames = buf[0];
        _log("  Image: " + std::to_string(total) + " bytes (" +
             std::to_string(n_frames) + " frame(s), header included), " +
             std::to_string(n_chunks) + " chunks");
    }

    uint8_t pkt[PACKET_SIZE];

    // Step 1: CMD_START
    make_cmd_packet(pkt, CMD_START, 0x00, 0x01);
    _log("  → START");
    _send_feature(pkt, true);

    // Step 2: CMD_IMAGE with slot index and chunk count
    make_cmd_packet(pkt, CMD_IMAGE, slot,
                    static_cast<uint8_t>(n_chunks & 0xFF));
    pkt[9] = static_cast<uint8_t>(n_chunks >> 8);
    _log("  → CMD_IMAGE  slot=" + std::to_string(slot) +
         " chunks=" + std::to_string(n_chunks));
    _send_feature(pkt, true);

    // Step 3: send data chunks as output reports on the display interface.
    // After each chunk, read ACK from display interface (like the Windows driver).
    uint8_t chunk_buf[1 + IMAGE_CHUNK_SIZE];
    chunk_buf[0] = 0x00;   // report-ID

    auto print_progress = [&](size_t done) {
        if (_verbose || _quiet) return;
        constexpr int BAR = 40;
        int filled = static_cast<int>(done * BAR / n_chunks);
        int pct    = static_cast<int>(done * 100 / n_chunks);
        std::cout << "\r  [";
        for (int j = 0; j < BAR; ++j) std::cout << (j < filled ? '#' : '.');
        std::cout << "] " << std::setw(3) << pct << "%  "
                  << done << "/" << n_chunks << " chunks"
                  << std::flush;
    };

    for (size_t i = 0; i < n_full; ++i) {
        std::memcpy(chunk_buf + 1, buf.data() + i * IMAGE_CHUNK_SIZE, IMAGE_CHUNK_SIZE);
        _log("  → chunk " + std::to_string(i + 1) + "/" +
             std::to_string(n_chunks));
        if (::write(_disp_fd, chunk_buf, sizeof(chunk_buf)) < 0)
            throw std::system_error(errno, std::generic_category(), "write image chunk");
        _read_disp_ack();
        print_progress(i + 1);
    }

    if (remainder) {
        std::memset(chunk_buf + 1, 0, IMAGE_CHUNK_SIZE);
        std::memcpy(chunk_buf + 1, buf.data() + n_full * IMAGE_CHUNK_SIZE, remainder);
        _log("  → chunk " + std::to_string(n_chunks) + "/" +
             std::to_string(n_chunks) + " (padded)");
        if (::write(_disp_fd, chunk_buf, sizeof(chunk_buf)) < 0)
            throw std::system_error(errno, std::generic_category(), "write image chunk (padded)");
        _read_disp_ack();
        print_progress(n_chunks);
    }
    if (!_verbose && !_quiet) std::cout << '\n';

    // Step 4: CMD_SAVE
    if (save) {
        make_cmd_packet(pkt, CMD_SAVE, 0x00, 0x00);
        _log("  → SAVE");
        _send_feature(pkt, true);
    } else {
        _log("  (SAVE skipped)");
    }

    _log("Image upload complete.");
}

// ---------------------------------------------------------------------------
// Bulk read helper
// ---------------------------------------------------------------------------

std::vector<uint8_t> Keyboard::_read_bulk(uint8_t cmd, size_t n_packets, uint8_t arg2) const
{
    uint8_t pkt[PACKET_SIZE];
    make_cmd_packet(pkt, cmd, 0x00, arg2);
    _log("  → READ cmd=0x" + [&]{ std::ostringstream s; s << std::hex << std::setw(2)
                                    << std::setfill('0') << (int)cmd; return s.str(); }()
         + " packets=" + std::to_string(n_packets));
    _send_feature(pkt, false);

    _read_ack();   // first HIDIOCGFEATURE is always a command echo; discard it

    std::vector<uint8_t> result;
    result.reserve(n_packets * PACKET_SIZE);
    for (size_t i = 0; i < n_packets; ++i) {
        auto chunk = _read_ack();
        result.insert(result.end(), chunk.begin(), chunk.end());
    }

    // After the last data packet, firmware transitions to state 3 (wait_save).
    // Send CMD_SAVE + CMD_DONE to reset the state machine back to state 0.
    uint8_t rst[PACKET_SIZE];
    make_cmd_packet(rst, CMD_SAVE, 0x00, 0x00);
    _send_feature(rst, true);
    make_cmd_packet(rst, CMD_DONE, 0x00, 0x00);
    _send_feature(rst, true);

    return result;
}

// ---------------------------------------------------------------------------
// Per-key read commands
// ---------------------------------------------------------------------------

std::vector<uint8_t> Keyboard::read_perkey_live()
{
    return _read_bulk(0xF5, 9, 9);
}

std::vector<uint8_t> Keyboard::read_custom_light()
{
    uint8_t pkt[PACKET_SIZE];
    make_cmd_packet(pkt, CMD_START, 0x00, 0x01);
    _log("  → START (before CMD_READ_CUSTOM_LIGHT)");
    _send_feature(pkt, true);
    return _read_bulk(0x22, 9, 9);
}

// ---------------------------------------------------------------------------
// Custom per-key lighting
// ---------------------------------------------------------------------------

void Keyboard::set_custom_lighting(const std::vector<KeyColor>& keys, uint8_t brightness)
{
    uint8_t pkt[PACKET_SIZE];

    // Phase 1: Upload per-key colour data to flash
    // Sequence: CMD_START → CMD_CUSTOM_LIGHT MM=9 → 9×64B → CMD_SAVE → CMD_DONE

    make_cmd_packet(pkt, CMD_START, 0x00, 0x01);
    _log("  → START (before CMD_CUSTOM_LIGHT)");
    _send_feature(pkt, true);

    make_cmd_packet(pkt, CMD_CUSTOM_LIGHT, 0x00, 0x09);
    _log("  → CMD_CUSTOM_LIGHT MM=9 (576 bytes)");
    _send_feature(pkt, true);

    // Build 576-byte payload: 144 entries × {index, R, G, B}.
    // Each entry's index byte must equal its position (0-143).
    uint8_t data[9 * PACKET_SIZE] = {};

    // Fill all 144 positions with valid-but-black entries
    for (int pos = 0; pos < 144; ++pos)
        data[pos * 4] = static_cast<uint8_t>(pos);

    // Overlay specified key colors
    for (const auto& k : keys) {
        if (k.index < 144) {
            int off = k.index * 4;
            data[off + 0] = k.index;
            data[off + 1] = k.r;
            data[off + 2] = k.g;
            data[off + 3] = k.b;
        }
    }

    for (int i = 0; i < 9; ++i) {
        _log("  → CUSTOM_LIGHT data " + std::to_string(i + 1) + "/9");
        _send_feature(data + i * PACKET_SIZE, true);
    }

    make_cmd_packet(pkt, CMD_SAVE, 0x00, 0x00);
    _log("  → SAVE (custom light)");
    _send_feature(pkt, true);

    make_cmd_packet(pkt, CMD_DONE, 0x00, 0x00);
    _log("  → DONE");
    _send_feature(pkt, true);

    // Phase 2: Switch to lighting mode 0x80 (per-key custom)
    // Sequence: CMD_LIGHTING MM=1 → data → CMD_SAVE

    make_cmd_packet(pkt, CMD_LIGHTING, 0x00, 0x01);
    _log("  → CMD_LIGHTING");
    _send_feature(pkt, true);

    std::memset(pkt, 0, PACKET_SIZE);
    pkt[0]  = 0x80;        // mode = 0x80 (per-key custom)
    pkt[9]  = brightness;  // brightness (0-5)
    pkt[62] = 0x55;        // delimiter
    pkt[63] = 0xAA;
    _log("  → LIGHTING data (mode=0x80, brightness=" + std::to_string(brightness) + ")");
    _send_feature(pkt, true);

    make_cmd_packet(pkt, CMD_SAVE, 0x00, 0x00);
    _log("  → SAVE (mode)");
    _send_feature(pkt, true);

    _log("Custom lighting set.");
}

} // namespace ajazz
