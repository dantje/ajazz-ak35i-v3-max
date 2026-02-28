#include "device_manager.h"

#include <glib.h>

DeviceManager::DeviceManager()  = default;

DeviceManager::~DeviceManager()
{
    // Wait for any in-flight worker to finish before destroying state.
    if (_worker.joinable())
        _worker.join();
    if (_kbd)
        _kbd->close();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Post fn to the GTK main thread via GLib's idle mechanism.
void DeviceManager::_post(std::function<void()> fn)
{
    auto* p = new std::function<void()>(std::move(fn));
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
        [](gpointer d) -> gboolean {
            auto* fn = static_cast<std::function<void()>*>(d);
            (*fn)();
            delete fn;
            return G_SOURCE_REMOVE;
        }, p, nullptr);
}

// Must be called on the GTK main thread.
void DeviceManager::_set_state(State s, const std::string& msg)
{
    _state = s;
    if (_state_cb)
        _state_cb(s, msg);
}

// Spawn a worker thread for one operation.  Joins any previous thread first
// (it should already have finished since we only allow one Busy at a time).
void DeviceManager::_run_async(std::function<void()> work)
{
    if (_state == State::Busy)
        return;
    if (_worker.joinable())
        _worker.join();
    _set_state(State::Busy);
    _worker = std::thread(std::move(work));
}

// ---------------------------------------------------------------------------
// Public async operations
// ---------------------------------------------------------------------------

void DeviceManager::connect_async()
{
    _run_async([this]() {
        try {
            auto kbd = std::make_unique<ajazz::Keyboard>(/*verbose=*/false, /*quiet=*/true);
            kbd->open();
            std::lock_guard<std::mutex> lk(_mutex);
            _kbd = std::move(kbd);
            _post([this]() { _set_state(State::Connected, "Keyboard connected"); });
        } catch (const std::exception& e) {
            std::string msg = e.what();
            _post([this, msg]() {
                _kbd.reset();
                _set_state(State::Error, msg);
            });
        }
    });
}

void DeviceManager::sync_time_async(const std::tm* tm)
{
    // Copy tm so we own the data on the worker thread.
    std::tm t_copy = {};
    bool use_now = (tm == nullptr);
    if (!use_now) t_copy = *tm;

    _run_async([this, use_now, t_copy]() mutable {
        try {
            std::lock_guard<std::mutex> lk(_mutex);
            _kbd->set_time(use_now ? nullptr : &t_copy);
            _post([this]() { _set_state(State::Connected, "Time synced"); });
        } catch (const std::exception& e) {
            std::string msg = e.what();
            _post([this, msg]() { _set_state(State::Error, msg); });
        }
    });
}

void DeviceManager::set_lighting_async(const ajazz::LightOptions& opts)
{
    _run_async([this, opts]() {
        try {
            std::lock_guard<std::mutex> lk(_mutex);
            _kbd->set_lighting(opts);
            _post([this]() { _set_state(State::Connected, "Lighting updated"); });
        } catch (const std::exception& e) {
            std::string msg = e.what();
            _post([this, msg]() { _set_state(State::Error, msg); });
        }
    });
}

void DeviceManager::upload_image_async(const std::string& path, imgconv::FitMode mode)
{
    _run_async([this, path, mode]() {
        try {
            std::vector<uint8_t> pixels = imgconv::load_image(path, mode, /*quiet=*/true);
            std::lock_guard<std::mutex> lk(_mutex);
            _kbd->send_image(pixels.data(), pixels.size(),
                             /*slot=*/1, /*save=*/true, /*header=*/false);
            _post([this]() { _set_state(State::Connected, "Image uploaded"); });
        } catch (const std::exception& e) {
            std::string msg = e.what();
            _post([this, msg]() { _set_state(State::Error, msg); });
        }
    });
}

void DeviceManager::upload_animation_async(const std::string& path,
                                           imgconv::FitMode mode,
                                           ProgressCb progress)
{
    _run_async([this, path, mode, progress]() {
        try {
            std::vector<uint8_t> pixels =
                imgconv::load_animation(path, mode, imgconv::MAX_FRAMES, /*quiet=*/true);

            auto chunk_progress = [this, progress](size_t done, size_t total) {
                if (progress)
                    _post([progress, done, total]() { progress(done, total); });
            };

            std::lock_guard<std::mutex> lk(_mutex);
            _kbd->send_image(pixels.data(), pixels.size(),
                             /*slot=*/1, /*save=*/true, /*header=*/false,
                             chunk_progress);
            _post([this]() { _set_state(State::Connected, "Animation uploaded"); });
        } catch (const std::exception& e) {
            std::string msg = e.what();
            _post([this, msg]() { _set_state(State::Error, msg); });
        }
    });
}
