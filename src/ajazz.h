#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace ajazz {

// ---------------------------------------------------------------------------
// USB device identification
// ---------------------------------------------------------------------------

constexpr uint16_t VID = 0x0C45;   // SONiX / Microdia
constexpr uint16_t PID = 0x8009;

// Known usage pages
constexpr uint16_t USAGE_PAGE_CMD  = 0xFF13;   // command / feature channel
constexpr uint16_t USAGE_PAGE_DISP = 0xFF68;   // display image channel

// HID report sizes
constexpr int PACKET_SIZE     = 64;   // payload delivered to device
constexpr int REPORT_BUF_SIZE = 65;   // HIDIOCSFEATURE/HIDIOCGFEATURE: report-ID + payload

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

struct DeviceInfo {
    std::string path;           // "/dev/hidrawN"
    uint16_t    usage_page;     // from HID report descriptor (0xFF13, 0xFF68, …)
    int         interface_num;  // from HID_PHYS /inputN suffix
};

// Enumerate all hidraw interfaces belonging to the Ajazz keyboard.
// Reads each device's HID report descriptor to determine the usage page.
// Returns an empty vector if the keyboard is not connected.
std::vector<DeviceInfo> discover();

// ---------------------------------------------------------------------------
// Lighting mode constants
// ---------------------------------------------------------------------------

enum class LightMode : uint8_t {
    Off       = 0x00,
    Static    = 0x01,
    SingleOn  = 0x02,
    SingleOff = 0x03,
    Glitter   = 0x04,
    Falling   = 0x05,
    Colourful = 0x06,
    Breath    = 0x07,
    Spectrum  = 0x08,
    Outward   = 0x09,
    Scrolling = 0x0A,
    Rolling   = 0x0B,
    Rotating  = 0x0C,
    Explode   = 0x0D,
    Launch    = 0x0E,
    Ripples   = 0x0F,
    Flowing   = 0x10,
    Pulsating = 0x11,
    Tilt      = 0x12,
    Shuttle   = 0x13,
    Custom    = 0x80,   // per-key custom mode
    PerKey    = 0x80,   // alias exposed via CLI ("perkey" / "per-key")
};

// Parse a lighting mode from a name (case-insensitive, e.g. "breath") or a
// numeric string (decimal or hex, e.g. "7" or "0x07").
// Returns true on success, false if not recognised.
bool parse_light_mode(const std::string& s, LightMode& out);

// Return the canonical lowercase name for a LightMode (e.g. "breath").
// Returns "0xNN" for unrecognised values.
std::string light_mode_name(LightMode m);

// ---------------------------------------------------------------------------
// Key index lookup  (light_index values, 1-based)
// ---------------------------------------------------------------------------

// Parse a key identifier to a light_index (1-based).  Accepts:
//   - key name  (case-insensitive): "w", "enter", "up", "num7", ...
//   - decimal number: "39"
//   - hex number:     "0x27"
// Returns true on success, false if not recognised.
bool parse_key_index(const std::string& s, uint8_t& out);

// Resolve a key specifier to a list of light_index values.
// Accepts: single key name, comma-separated list (e.g. "w,a,s,d"),
//          named group (e.g. "wasd", "frow", "all"), or decimal/hex index.
// Returns false (out unchanged) if any token is unrecognised.
bool resolve_key_set(const std::string& spec, std::vector<uint8_t>& out);

// Convert QMK-style 8-bit HSV (h, s, v each 0-255) to RGB (each 0-255).
// Hue 0=red, 85=green, 170=blue (maps 0-255 to 0-360 degrees).
void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                uint8_t& r, uint8_t& g, uint8_t& b);

// Return the shortest key name for a given light_index, or "" if unknown.
std::string index_to_key_name(uint8_t light_index);

// Parse a CSS color name (e.g. "rebeccapurple") or "#RRGGBB" hex to RGB.
// Returns false if the string is not a recognised color name or hex code.
bool parse_color_name(const std::string& s, uint8_t& r, uint8_t& g, uint8_t& b);

// Return the CSS name for an exact RGB match, or "" if none.
std::string color_name_from_rgb(uint8_t r, uint8_t g, uint8_t b);

// ---------------------------------------------------------------------------
// Lighting options
// ---------------------------------------------------------------------------

struct LightOptions {
    LightMode mode       = LightMode::Static;
    uint8_t   r          = 255;
    uint8_t   g          = 255;
    uint8_t   b          = 255;
    bool      rainbow    = false;   // enable multicolour cycling
    uint8_t   brightness = 5;       // 0–5
    uint8_t   speed      = 3;       // 0–5
    uint8_t   direction  = 0;       // 0=left 1=down 2=up 3=right
};

// ---------------------------------------------------------------------------
// Keyboard class
// ---------------------------------------------------------------------------

class Keyboard {
public:
    explicit Keyboard(bool verbose = false, bool quiet = false);
    ~Keyboard();

    // Non-copyable
    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    // Open the command and display interfaces by autodiscovery.
    // Calls discover() internally; throws std::runtime_error on failure.
    void open();
    void close();

    // Sync the display RTC to local time.
    // Pass nullptr (or call with no argument) to use the current system time.
    void set_time(const std::tm* tm = nullptr);

    // Set keyboard RGB lighting.
    void set_lighting(const LightOptions& opts);

    // Upload image data to the display.
    //
    // If header=true (default), data is raw pixel bytes (width*height*2
    // RGB565-LE) and a 256-byte header is prepended automatically (1 frame).
    //
    // If header=false, data already contains the 256-byte header followed
    // by pixel data for one or more frames (as produced by load_image /
    // load_animation in imgconv).
    //
    // slot: 1-based LCD slot index (default 1).
    // save: send CMD_SAVE after upload (default true).
    void send_image(const uint8_t* data, size_t size,
                    uint8_t slot = 1, bool save = true,
                    bool header = true);

    // Read live per-key RGB state via CMD 0xF5.
    // Returns 576 bytes: 144 x {pos, R, G, B}.
    std::vector<uint8_t> read_perkey_live();

    // Read stored per-key custom lighting from flash via CMD 0x22.
    // Returns 576 bytes: 144 x {pos, R, G, B}.
    std::vector<uint8_t> read_custom_light();

    struct KeyColor { uint8_t index, r, g, b; };

    // Upload per-key RGB: {index, R, G, B} x up to 143 keys.
    // Sends 9 data packets (576 bytes) to flash, then sets mode 0x80.
    void set_custom_lighting(const std::vector<KeyColor>& keys,
                             uint8_t brightness = 5);

private:
    bool _verbose;
    bool _quiet;
    int  _cmd_fd;    // fd for the FF13 command channel
    int  _disp_fd;   // fd for the FF68 display channel

    void _send_feature(const uint8_t* payload, bool read_ack = false) const;
    std::vector<uint8_t> _read_ack() const;    // returns 64-byte payload
    void _read_disp_ack() const;               // ACK read on display interface (300ms timeout)
    std::vector<uint8_t> _read_bulk(uint8_t cmd, size_t n_packets, uint8_t arg2 = 1) const;
    void _log(const std::string& msg) const;
};

} // namespace ajazz
