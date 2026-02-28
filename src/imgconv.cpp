#include "imgconv.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

#include <stb_image.h>
#include <stb_image_resize2.h>

namespace imgconv {

// ---------------------------------------------------------------------------
// frame_to_rgb565
// ---------------------------------------------------------------------------

std::vector<uint8_t> frame_to_rgb565(const uint8_t* rgb, int w, int h, FitMode mode)
{
    double sx    = static_cast<double>(DISPLAY_W) / w;
    double sy    = static_cast<double>(DISPLAY_H) / h;
    double scale = (mode == FitMode::Fit) ? std::min(sx, sy) : std::max(sx, sy);

    int sw = std::max(1, static_cast<int>(std::round(w * scale)));
    int sh = std::max(1, static_cast<int>(std::round(h * scale)));

    // Resize source to sw × sh
    std::vector<uint8_t> resized(sw * sh * 3);
    stbir_resize_uint8_linear(rgb, w, h, 0,
                               resized.data(), sw, sh, 0,
                               STBIR_RGB);

    // Build 240×135 canvas (black)
    std::vector<uint8_t> canvas(DISPLAY_W * DISPLAY_H * 3, 0);

    if (mode == FitMode::Fit) {
        int ox = (DISPLAY_W - sw) / 2;
        int oy = (DISPLAY_H - sh) / 2;
        for (int y = 0; y < sh; ++y) {
            if (oy + y < 0 || oy + y >= DISPLAY_H) continue;
            const uint8_t* src = resized.data() + y * sw * 3;
            uint8_t*       dst = canvas.data() + (oy + y) * DISPLAY_W * 3 + ox * 3;
            int copy_w = std::min(sw, DISPLAY_W - ox);
            if (copy_w > 0) std::memcpy(dst, src, copy_w * 3);
        }
    } else {
        // Centre-crop resized image into the 240×135 canvas
        int cx      = std::max(0, (sw - DISPLAY_W) / 2);
        int cy      = std::max(0, (sh - DISPLAY_H) / 2);
        int copy_w  = std::min(DISPLAY_W,  sw - cx);
        int copy_h  = std::min(DISPLAY_H,  sh - cy);
        for (int y = 0; y < copy_h; ++y) {
            const uint8_t* src = resized.data() + (cy + y) * sw * 3 + cx * 3;
            uint8_t*       dst = canvas.data() + y * DISPLAY_W * 3;
            std::memcpy(dst, src, copy_w * 3);
        }
    }

    // RGB888 → RGB565-LE
    std::vector<uint8_t> out(FRAME_BYTES);
    for (int i = 0; i < DISPLAY_W * DISPLAY_H; ++i) {
        uint8_t  r   = canvas[i * 3];
        uint8_t  g   = canvas[i * 3 + 1];
        uint8_t  b   = canvas[i * 3 + 2];
        uint16_t val = (static_cast<uint16_t>(r & 0xF8) << 8)
                     | (static_cast<uint16_t>(g & 0xFC) << 3)
                     | (b >> 3);
        out[i * 2]     = val & 0xFF;
        out[i * 2 + 1] = val >> 8;
    }
    return out;
}

// ---------------------------------------------------------------------------
// load_image  (single frame)
// ---------------------------------------------------------------------------

std::vector<uint8_t> load_image(const std::string& path, FitMode mode, bool quiet)
{
    int w, h, n;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!pixels)
        throw std::runtime_error(
            "Cannot load image '" + path + "': " + stbi_failure_reason());

    if (!quiet)
        std::cout << "Image: " << w << "x" << h << " → " << DISPLAY_W << "x" << DISPLAY_H
                  << " (" << (mode == FitMode::Fit ? "fit" : "fill") << ")\n";

    auto frame = frame_to_rgb565(pixels, w, h, mode);
    stbi_image_free(pixels);

    std::vector<uint8_t> result(HEADER_SIZE + FRAME_BYTES, 0xFF);
    result[0] = 1;   // 1 frame
    result[1] = 1;   // minimum delay
    std::memcpy(result.data() + HEADER_SIZE, frame.data(), FRAME_BYTES);
    return result;
}

// ---------------------------------------------------------------------------
// load_animation  (animated GIF or fallback to single frame)
// ---------------------------------------------------------------------------

std::vector<uint8_t> load_animation(const std::string& path, FitMode mode, int max_frames, bool quiet)
{
    // Read file into memory
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open '" + path + "'");
    std::vector<uint8_t> file_data(std::istreambuf_iterator<char>(f), {});

    int w = 0, h = 0, z = 0, comp = 0;
    int* delays = nullptr;
    stbi_uc* frames = stbi_load_gif_from_memory(
        file_data.data(), static_cast<int>(file_data.size()),
        &delays, &w, &h, &z, &comp, 3);

    if (!frames) {
        // Not an animated GIF (or not a GIF at all) — treat as single frame
        if (delays) stbi_image_free(delays);
        return load_image(path, mode, quiet);
    }

    int n = std::min(z, max_frames);
    if (!quiet) {
        std::cout << "Animated GIF: " << z << " frame(s) (" << w << "x" << h << ")"
                  << ", uploading " << n
                  << " (" << (mode == FitMode::Fit ? "fit" : "fill") << ")\n";
        if (z > max_frames)
            std::cout << "  WARNING: GIF has " << z << " frames; truncating to "
                      << max_frames << " (--max-frames limit; firmware delay table caps at ~190)\n";
    }

    std::vector<uint8_t> result(HEADER_SIZE + static_cast<size_t>(n) * FRAME_BYTES, 0xFF);
    result[0] = static_cast<uint8_t>(n);

    bool delay_clamped = false;
    for (int i = 0; i < n; ++i) {
        int delay_ms  = delays ? delays[i] : 50;
        int delay_val = std::max(1, std::min(255, delay_ms / 2));
        if (delay_ms / 2 > 255) delay_clamped = true;
        result[1 + i] = static_cast<uint8_t>(delay_val);

        const uint8_t* frame_rgb = frames + static_cast<ptrdiff_t>(i) * w * h * 3;
        auto frame = frame_to_rgb565(frame_rgb, w, h, mode);
        std::memcpy(result.data() + HEADER_SIZE
                    + static_cast<size_t>(i) * FRAME_BYTES,
                    frame.data(), FRAME_BYTES);

        if (!quiet && ((i + 1) % 10 == 0 || i == n - 1))
            std::cout << "  frame " << (i + 1) << "/" << n
                      << "  delay=" << delay_ms << "ms\n";
    }
    if (!quiet && delay_clamped)
        std::cout << "  WARNING: some frame delays exceed 510 ms and were clamped to 510 ms\n"
                  << "  (delay is stored as 1 byte in 2 ms units; max = 255 × 2 = 510 ms)\n";

    stbi_image_free(frames);
    stbi_image_free(delays);
    return result;
}

} // namespace imgconv
