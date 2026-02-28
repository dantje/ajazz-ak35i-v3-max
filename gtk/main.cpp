#include <adwaita.h>
#include <memory>

#include "window.h"

static void on_activate(GtkApplication* app, gpointer)
{
    // AjazzWindow constructs itself, presents the window, and kicks off
    // DeviceManager::connect_async().  It is heap-allocated and leaks
    // intentionally — it lives for the duration of the application.
    new AjazzWindow(ADW_APPLICATION(app));
}

int main(int argc, char* argv[])
{
    auto* app = adw_application_new("com.github.ajazz.AjazzGtk",
                                    G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
