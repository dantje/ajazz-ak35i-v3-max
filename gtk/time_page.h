#pragma once

#include <gtk/gtk.h>
#include "device_manager.h"

class TimePage {
public:
    explicit TimePage(DeviceManager& dm);

    GtkWidget* root() { return _root; }
    void set_sensitive(bool s);

private:
    GtkWidget*    _root      = nullptr;  // AdwPreferencesPage
    GtkWidget*    _sync_btn  = nullptr;
    GtkWidget*    _hour_spin = nullptr;  // GtkSpinButton inside expander
    GtkWidget*    _min_spin  = nullptr;
    GtkWidget*    _sec_spin  = nullptr;
    GtkWidget*    _manual_btn = nullptr;

    DeviceManager& _dm;

    static void _on_sync_now(GtkButton*, gpointer self);
    static void _on_manual_apply(GtkButton*, gpointer self);
};
