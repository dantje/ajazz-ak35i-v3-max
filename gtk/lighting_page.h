#pragma once

#include <gtk/gtk.h>
#include "device_manager.h"

class LightingPage {
public:
    explicit LightingPage(DeviceManager& dm);

    GtkWidget* root() { return _root; }
    void set_sensitive(bool s);

private:
    GtkWidget*    _root        = nullptr;  // AdwPreferencesPage
    GtkWidget*    _mode_row    = nullptr;  // AdwComboRow
    GtkWidget*    _colour_btn  = nullptr;  // GtkColorDialogButton
    GtkWidget*    _rainbow_row = nullptr;  // AdwSwitchRow
    GtkWidget*    _bright_row  = nullptr;  // AdwSpinRow
    GtkWidget*    _speed_row   = nullptr;  // AdwSpinRow
    GtkWidget*    _dir_row     = nullptr;  // AdwComboRow
    GtkWidget*    _apply_btn   = nullptr;

    DeviceManager& _dm;

    static void _on_apply(GtkButton*, gpointer self);
};
