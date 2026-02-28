#include "image_page.h"

#include <adwaita.h>

ImagePage::ImagePage(DeviceManager& dm, GtkWindow* parent)
    : _dm(dm), _parent(parent)
{
    _root = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(_root), "Image");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(_root),
                                       "image-x-generic-symbolic");

    // -----------------------------------------------------------------------
    // File selection group
    // -----------------------------------------------------------------------
    GtkWidget* grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp), "Static Image");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(grp),
        "Accepts PNG, JPG, BMP, GIF (first frame only). "
        "The image is scaled to 240×135 pixels.");
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
    // Preview group
    // -----------------------------------------------------------------------
    GtkWidget* prev_grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(prev_grp), "Preview");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(prev_grp));

    _preview = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(_preview), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_size_request(_preview, 240, 135);
    gtk_widget_set_margin_top(_preview,    8);
    gtk_widget_set_margin_bottom(_preview, 8);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(prev_grp), _preview);

    // -----------------------------------------------------------------------
    // Upload button
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
    gtk_widget_set_margin_bottom(_upload_btn, 8);
    g_signal_connect(_upload_btn, "clicked", G_CALLBACK(_on_upload), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(btn_grp), _upload_btn);
}

void ImagePage::set_sensitive(bool s)
{
    gtk_widget_set_sensitive(_file_row,  s);
    gtk_widget_set_sensitive(_fill_row,  s);
    // Upload button only sensitive if a file is also selected
    gtk_widget_set_sensitive(_upload_btn, s && !_selected_path.empty());
}

void ImagePage::_set_file(const std::string& path)
{
    _selected_path = path;

    // Show just the filename in the subtitle
    std::string name = path;
    auto slash = path.rfind('/');
    if (slash != std::string::npos) name = path.substr(slash + 1);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(_file_row), name.c_str());

    // Update preview
    gtk_picture_set_filename(GTK_PICTURE(_preview), path.c_str());

    // Enable upload button (widget must already be sensitive)
    if (gtk_widget_get_sensitive(_file_row))
        gtk_widget_set_sensitive(_upload_btn, TRUE);
}

void ImagePage::_on_browse(GtkButton*, gpointer self_ptr)
{
    static_cast<ImagePage*>(self_ptr)->_open_file_dialog();
}

void ImagePage::_open_file_dialog()
{
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Image");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/png");
    gtk_file_filter_add_mime_type(filter, "image/jpeg");
    gtk_file_filter_add_mime_type(filter, "image/bmp");
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

void ImagePage::_file_dialog_cb(GObject* src, GAsyncResult* res, gpointer self_ptr)
{
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, nullptr);
    if (!file) return;
    char* path = g_file_get_path(file);
    if (path) {
        static_cast<ImagePage*>(self_ptr)->_set_file(path);
        g_free(path);
    }
    g_object_unref(file);
}

void ImagePage::_on_upload(GtkButton*, gpointer self_ptr)
{
    auto* self = static_cast<ImagePage*>(self_ptr);
    if (self->_selected_path.empty()) return;

    auto mode = adw_switch_row_get_active(ADW_SWITCH_ROW(self->_fill_row))
                ? imgconv::FitMode::Fill
                : imgconv::FitMode::Fit;
    self->_dm.upload_image_async(self->_selected_path, mode);
}
