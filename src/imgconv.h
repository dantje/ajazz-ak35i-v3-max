#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace imgconv {

constexpr int DISPLAY_W   = 240;
constexpr int DISPLAY_H   = 135;
constexpr int FRAME_BYTES = DISPLAY_W * DISPLAY_H * 2;   // RGB565-LE
constexpr int HEADER_SIZE = 256;
constexpr int MAX_FRAMES  = 141;   // Windows driver XML limit (gif_maxframes)

enum class FitMode {
    Fit,    // letterbox: preserve AR, pad with black (default)
    Fill,   // scale-to-fill: preserve AR, centre-crop
};

// Convert a single RGB888 frame (w × h, 3 bytes/pixel) to 240×135 RGB565-LE.
std::vector<uint8_t> frame_to_rgb565(const uint8_t* rgb, int w, int h, FitMode mode);

// Load a static image (first frame only for animated GIFs).
// Returns 256-byte header + one frame of RGB565-LE data.
std::vector<uint8_t> load_image(const std::string& path,
                                 FitMode mode = FitMode::Fit,
                                 bool quiet = false);

// Load an animated GIF (or any image as a 1-frame animation).
// Returns 256-byte header + N frames of RGB565-LE data.
std::vector<uint8_t> load_animation(const std::string& path,
                                     FitMode mode = FitMode::Fit,
                                     int max_frames = MAX_FRAMES,
                                     bool quiet = false);

} // namespace imgconv
