#include "ajazz.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

constexpr uint8_t CMD_SAVE     = 0x02;
constexpr uint8_t CMD_LIGHTING = 0x13;
constexpr uint8_t CMD_START    = 0x18;
constexpr uint8_t CMD_TIME     = 0x28;
constexpr uint8_t CMD_IMAGE    = 0x72;
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
    };

    if (!s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) ||
                       (s.size() > 2 && s[0] == '0' && s[1] == 'x'))) {
        try {
            unsigned long v = std::stoul(s, nullptr, 0);
            if (v <= 0x13) { out = static_cast<LightMode>(v); return true; }
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

} // namespace ajazz
