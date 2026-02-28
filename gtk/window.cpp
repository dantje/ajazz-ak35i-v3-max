#include "window.h"

#include "lighting_page.h"
#include "time_page.h"
#include "image_page.h"
#include "animation_page.h"

AjazzWindow::AjazzWindow(AdwApplication* app)
{
    // -----------------------------------------------------------------------
    // Root window
    // -----------------------------------------------------------------------
    _window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(_window), "Ajazz Keyboard");
    gtk_window_set_default_size(GTK_WINDOW(_window), 480, 680);

    // -----------------------------------------------------------------------
    // Header bar with view-switcher as title widget (libadwaita 1.6+ style)
    // -----------------------------------------------------------------------
    _stack = adw_view_stack_new();

    GtkWidget* switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(_stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher),
                                 ADW_VIEW_SWITCHER_POLICY_WIDE);

    GtkWidget* header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);

    GtkWidget* reconnect_btn = gtk_button_new_with_label("Reconnect");
    gtk_widget_add_css_class(reconnect_btn, "flat");
    g_signal_connect(reconnect_btn, "clicked",
                     G_CALLBACK(_on_reconnect_clicked), this);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), reconnect_btn);

    // Bottom narrow switcher bar (reveals only when header switcher is too wide)
    GtkWidget* switcher_bar = adw_view_switcher_bar_new();
    adw_view_switcher_bar_set_stack(ADW_VIEW_SWITCHER_BAR(switcher_bar),
                                    ADW_VIEW_STACK(_stack));

    // -----------------------------------------------------------------------
    // Connection banner (visible until keyboard is found)
    // -----------------------------------------------------------------------
    _banner = adw_banner_new("Keyboard not connected");
    adw_banner_set_button_label(ADW_BANNER(_banner), "Reconnect");
    adw_banner_set_revealed(ADW_BANNER(_banner), TRUE);
    g_signal_connect(_banner, "button-clicked",
                     G_CALLBACK(_on_reconnect_clicked), this);

    // -----------------------------------------------------------------------
    // Content pages
    // -----------------------------------------------------------------------
    _lighting  = std::make_unique<LightingPage>(_dm);
    _time      = std::make_unique<TimePage>(_dm);
    _image     = std::make_unique<ImagePage>(_dm, GTK_WINDOW(_window));
    _animation = std::make_unique<AnimationPage>(_dm, GTK_WINDOW(_window));

    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(_stack),
        _lighting->root(),  "lighting",  "Lighting",  "preferences-color-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(_stack),
        _time->root(),      "time",      "Time",      "appointment-new-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(_stack),
        _image->root(),     "image",     "Image",     "image-x-generic-symbolic");
    adw_view_stack_add_titled_with_icon(ADW_VIEW_STACK(_stack),
        _animation->root(), "animation", "Animation", "media-playback-start-symbolic");

    // Pages start insensitive until keyboard is connected
    _set_pages_sensitive(false);

    // -----------------------------------------------------------------------
    // Layout: toolbar view wraps everything
    // -----------------------------------------------------------------------
    _toast_overlay = adw_toast_overlay_new();

    GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(content_box), _banner);
    gtk_box_append(GTK_BOX(content_box), _stack);
    gtk_widget_set_vexpand(_stack, TRUE);

    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(_toast_overlay), content_box);

    GtkWidget* toolbar_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), _toast_overlay);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar_view), switcher_bar);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(_window), toolbar_view);

    // -----------------------------------------------------------------------
    // Wire DeviceManager callbacks
    // -----------------------------------------------------------------------
    _dm.set_state_callback([this](DeviceManager::State s, const std::string& msg) {
        _on_state_changed(s, msg);
    });

    // Auto-connect on startup
    _dm.connect_async();

    gtk_window_present(GTK_WINDOW(_window));
}

void AjazzWindow::_on_state_changed(DeviceManager::State state,
                                    const std::string& msg)
{
    bool connected = (state == DeviceManager::State::Connected);
    bool busy      = (state == DeviceManager::State::Busy);

    adw_banner_set_revealed(ADW_BANNER(_banner), !connected && !busy);

    if (state == DeviceManager::State::Error) {
        adw_banner_set_title(ADW_BANNER(_banner),
                             ("Error: " + msg).c_str());
    } else if (!connected && !busy) {
        adw_banner_set_title(ADW_BANNER(_banner), "Keyboard not connected");
    }

    _set_pages_sensitive(connected);

    if (connected || state == DeviceManager::State::Error) {
        if (!msg.empty())
            show_toast(msg);
    }
}

void AjazzWindow::_set_pages_sensitive(bool sensitive)
{
    _lighting->set_sensitive(sensitive);
    _time->set_sensitive(sensitive);
    _image->set_sensitive(sensitive);
    _animation->set_sensitive(sensitive);
}

void AjazzWindow::show_toast(const std::string& msg)
{
    AdwToast* toast = adw_toast_new(msg.c_str());
    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(_toast_overlay), toast);
}

void AjazzWindow::_on_reconnect_clicked(GtkButton*, gpointer self)
{
    static_cast<AjazzWindow*>(self)->_dm.connect_async();
}
