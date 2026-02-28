#include "animation_page.h"

#include <adwaita.h>

AnimationPage::AnimationPage(DeviceManager& dm, GtkWindow* parent)
    : _dm(dm), _parent(parent)
{
    _root = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(_root), "Animation");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(_root),
                                       "media-playback-start-symbolic");

    // -----------------------------------------------------------------------
    // File selection group
    // -----------------------------------------------------------------------
    GtkWidget* grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp), "Animated GIF");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(grp),
        "Upload an animated GIF. Capped at 141 frames. "
        "A full upload takes about 78 seconds.");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(grp));

    _file_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_file_row), "File");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(_file_row), "No file selected");
    GtkWidget* browse_btn = gtk_button_new_with_label("Browse…");
    gtk_widget_set_valign(browse_btn, GTK_ALIGN_CENTER);
    g_signal_connect(browse_btn, "clicked", G_CALLBACK(_on_browse), this);
    adw_action_row_add_suffix(ADW_ACTION_ROW(_file_row), browse_btn);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(_file_row), browse_btn);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), _file_row);

    _fill_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_fill_row), "Scale to Fill");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(_fill_row),
                                "Crop to fill (off = letterbox)");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), _fill_row);

    // -----------------------------------------------------------------------
    // Upload button + progress bar
    // -----------------------------------------------------------------------
    GtkWidget* btn_grp = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(btn_grp));

    _upload_btn = gtk_button_new_with_label("Upload to Keyboard");
    gtk_widget_add_css_class(_upload_btn, "pill");
    gtk_widget_add_css_class(_upload_btn, "suggested-action");
    gtk_widget_set_halign(_upload_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(_upload_btn, FALSE);
    gtk_widget_set_margin_top(_upload_btn, 8);
    g_signal_connect(_upload_btn, "clicked", G_CALLBACK(_on_upload), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(btn_grp), _upload_btn);

    _progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(_progress), TRUE);
    gtk_widget_set_margin_top(_progress,    4);
    gtk_widget_set_margin_bottom(_progress, 12);
    gtk_widget_set_visible(_progress, FALSE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(btn_grp), _progress);
}

void AnimationPage::set_sensitive(bool s)
{
    gtk_widget_set_sensitive(_file_row, s);
    gtk_widget_set_sensitive(_fill_row, s);
    gtk_widget_set_sensitive(_upload_btn, s && !_selected_path.empty());
}

void AnimationPage::_set_file(const std::string& path)
{
    _selected_path = path;

    std::string name = path;
    auto slash = path.rfind('/');
    if (slash != std::string::npos) name = path.substr(slash + 1);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(_file_row), name.c_str());

    if (gtk_widget_get_sensitive(_file_row))
        gtk_widget_set_sensitive(_upload_btn, TRUE);
}

void AnimationPage::_on_progress(size_t done, size_t total)
{
    gtk_widget_set_visible(_progress, TRUE);
    double frac = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(_progress), frac);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zu / %zu chunks", done, total);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(_progress), buf);

    if (done >= total)
        gtk_widget_set_visible(_progress, FALSE);
}

void AnimationPage::_on_browse(GtkButton*, gpointer self_ptr)
{
    static_cast<AnimationPage*>(self_ptr)->_open_file_dialog();
}

void AnimationPage::_open_file_dialog()
{
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Animated GIF");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Animated GIF / RAW");
    gtk_file_filter_add_mime_type(filter, "image/gif");
    gtk_file_filter_add_pattern(filter, "*.raw");

    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    g_object_unref(filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);

    gtk_file_dialog_open(dialog, _parent, nullptr, _file_dialog_cb, this);
    g_object_unref(dialog);
}

void AnimationPage::_file_dialog_cb(GObject* src, GAsyncResult* res, gpointer self_ptr)
{
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, nullptr);
    if (!file) return;
    char* path = g_file_get_path(file);
    if (path) {
        static_cast<AnimationPage*>(self_ptr)->_set_file(path);
        g_free(path);
    }
    g_object_unref(file);
}

void AnimationPage::_on_upload(GtkButton*, gpointer self_ptr)
{
    auto* self = static_cast<AnimationPage*>(self_ptr);
    if (self->_selected_path.empty()) return;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->_progress), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(self->_progress), "Loading…");
    gtk_widget_set_visible(self->_progress, TRUE);

    auto mode = adw_switch_row_get_active(ADW_SWITCH_ROW(self->_fill_row))
                ? imgconv::FitMode::Fill
                : imgconv::FitMode::Fit;

    self->_dm.upload_animation_async(
        self->_selected_path, mode,
        [self](size_t done, size_t total) { self->_on_progress(done, total); });
}
