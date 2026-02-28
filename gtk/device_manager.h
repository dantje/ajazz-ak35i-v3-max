#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "ajazz.h"
#include "imgconv.h"

// ---------------------------------------------------------------------------
// DeviceManager
//
// Thread-safe wrapper around ajazz::Keyboard.  All hardware operations run on
// a worker thread.  Callbacks are always invoked on the GTK main thread via
// g_idle_add, so widgets can be updated directly inside them.
//
// Rule: DeviceManager must outlive any in-flight worker thread (i.e. it must
// live for the duration of the application).
// ---------------------------------------------------------------------------

class DeviceManager {
public:
    enum class State { Disconnected, Connected, Busy, Error };

    using StateCb    = std::function<void(State, const std::string& message)>;
    using ProgressCb = std::function<void(size_t done, size_t total)>;

    DeviceManager();
    ~DeviceManager();

    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    void set_state_callback(StateCb cb)    { _state_cb    = std::move(cb); }

    State state() const { return _state; }

    void connect_async();
    void sync_time_async(const std::tm* tm = nullptr);
    void set_lighting_async(const ajazz::LightOptions& opts);
    void upload_image_async(const std::string& path, imgconv::FitMode mode);
    void upload_animation_async(const std::string& path, imgconv::FitMode mode,
                                ProgressCb progress = nullptr);

private:
    std::unique_ptr<ajazz::Keyboard> _kbd;
    State             _state = State::Disconnected;
    StateCb           _state_cb;
    std::thread       _worker;
    std::mutex        _mutex;   // serialises access to _kbd and _state

    void _run_async(std::function<void()> work);
    void _post(std::function<void()> fn);
    void _set_state(State s, const std::string& msg = "");
};
