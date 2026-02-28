#pragma once

#include <adwaita.h>
#include <memory>

#include "device_manager.h"

class LightingPage;
class TimePage;
class ImagePage;
class AnimationPage;

// ---------------------------------------------------------------------------
// AjazzWindow
//
// Main application window.  Owns the DeviceManager and all four content pages.
// Wires together the DeviceManager state callbacks with per-page sensitivity.
// ---------------------------------------------------------------------------

class AjazzWindow {
public:
    explicit AjazzWindow(AdwApplication* app);

    GtkWidget* widget() { return _window; }

private:
    // GTK widgets
    GtkWidget* _window       = nullptr;
    GtkWidget* _banner       = nullptr;   // AdwBanner: "not connected"
    GtkWidget* _stack        = nullptr;   // AdwViewStack
    GtkWidget* _toast_overlay = nullptr;  // AdwToastOverlay

    // Business logic
    DeviceManager _dm;

    // Content pages (owned here, widgets owned by GTK)
    std::unique_ptr<LightingPage>   _lighting;
    std::unique_ptr<TimePage>       _time;
    std::unique_ptr<ImagePage>      _image;
    std::unique_ptr<AnimationPage>  _animation;

    void _on_state_changed(DeviceManager::State state, const std::string& msg);
    void _set_pages_sensitive(bool sensitive);
    void show_toast(const std::string& msg);

    static void _on_reconnect_clicked(GtkButton*, gpointer self);
};
