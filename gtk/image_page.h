#pragma once

#include <gtk/gtk.h>
#include <string>
#include "device_manager.h"

class ImagePage {
public:
    ImagePage(DeviceManager& dm, GtkWindow* parent);

    GtkWidget* root() { return _root; }
    void set_sensitive(bool s);

private:
    GtkWidget*    _root       = nullptr;  // AdwPreferencesPage
    GtkWidget*    _file_row   = nullptr;  // AdwActionRow showing selected file
    GtkWidget*    _fill_row   = nullptr;  // AdwSwitchRow
    GtkWidget*    _preview    = nullptr;  // GtkPicture
    GtkWidget*    _upload_btn = nullptr;

    DeviceManager& _dm;
    GtkWindow*     _parent;
    std::string    _selected_path;

    void _open_file_dialog();
    void _set_file(const std::string& path);

    static void _on_browse(GtkButton*, gpointer self);
    static void _on_upload(GtkButton*, gpointer self);
    static void _file_dialog_cb(GObject* src, GAsyncResult* res, gpointer self);
};
