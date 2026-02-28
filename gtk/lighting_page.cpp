#include "lighting_page.h"

#include <adwaita.h>
#include <cstdint>

static const char* const MODE_NAMES[] = {
    "Off", "Static", "Single On", "Single Off", "Glitter", "Falling",
    "Colourful", "Breath", "Spectrum", "Outward", "Scrolling", "Rolling",
    "Rotating", "Explode", "Launch", "Ripples", "Flowing", "Pulsating",
    "Tilt", "Shuttle", nullptr
};

static const char* const DIR_NAMES[] = {
    "Left", "Down", "Up", "Right", nullptr
};

LightingPage::LightingPage(DeviceManager& dm) : _dm(dm)
{
    _root = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(_root), "Lighting");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(_root),
                                       "preferences-color-symbolic");

    // -----------------------------------------------------------------------
    // Effect group
    // -----------------------------------------------------------------------
    GtkWidget* grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp), "Effect");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(grp));

    // Mode
    _mode_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_mode_row), "Mode");
    GtkStringList* mode_model = gtk_string_list_new(MODE_NAMES);
    adw_combo_row_set_model(ADW_COMBO_ROW(_mode_row), G_LIST_MODEL(mode_model));
    adw_combo_row_set_selected(ADW_COMBO_ROW(_mode_row), 1); // Static
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), _mode_row);

    // Colour (GtkColorDialogButton inside an action row)
    GtkWidget* colour_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(colour_row), "Colour");
    GtkColorDialog* dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog, FALSE);
    _colour_btn = gtk_color_dialog_button_new(dialog);
    g_object_unref(dialog);
    gtk_widget_set_valign(_colour_btn, GTK_ALIGN_CENTER);
    GdkRGBA white = {1.0, 1.0, 1.0, 1.0};
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(_colour_btn), &white);
    adw_action_row_add_suffix(ADW_ACTION_ROW(colour_row), _colour_btn);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(colour_row), _colour_btn);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), colour_row);

    // Rainbow
    _rainbow_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_rainbow_row), "Rainbow");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(_rainbow_row),
                                "Multicolour cycling (overrides colour)");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), _rainbow_row);

    // -----------------------------------------------------------------------
    // Parameters group
    // -----------------------------------------------------------------------
    GtkWidget* params_grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(params_grp), "Parameters");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(params_grp));

    _bright_row = adw_spin_row_new_with_range(0, 5, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_bright_row), "Brightness");
    adw_spin_row_set_value(ADW_SPIN_ROW(_bright_row), 5);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(params_grp), _bright_row);

    _speed_row = adw_spin_row_new_with_range(0, 5, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_speed_row), "Speed");
    adw_spin_row_set_value(ADW_SPIN_ROW(_speed_row), 3);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(params_grp), _speed_row);

    _dir_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_dir_row), "Direction");
    GtkStringList* dir_model = gtk_string_list_new(DIR_NAMES);
    adw_combo_row_set_model(ADW_COMBO_ROW(_dir_row), G_LIST_MODEL(dir_model));
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(params_grp), _dir_row);

    // -----------------------------------------------------------------------
    // Apply button
    // -----------------------------------------------------------------------
    GtkWidget* btn_grp = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(btn_grp));

    _apply_btn = gtk_button_new_with_label("Apply");
    gtk_widget_add_css_class(_apply_btn, "pill");
    gtk_widget_add_css_class(_apply_btn, "suggested-action");
    gtk_widget_set_halign(_apply_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(_apply_btn, 8);
    gtk_widget_set_margin_bottom(_apply_btn, 8);
    g_signal_connect(_apply_btn, "clicked", G_CALLBACK(_on_apply), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(btn_grp), _apply_btn);
}

void LightingPage::set_sensitive(bool s)
{
    gtk_widget_set_sensitive(_mode_row,    s);
    gtk_widget_set_sensitive(_colour_btn,  s);
    gtk_widget_set_sensitive(_rainbow_row, s);
    gtk_widget_set_sensitive(_bright_row,  s);
    gtk_widget_set_sensitive(_speed_row,   s);
    gtk_widget_set_sensitive(_dir_row,     s);
    gtk_widget_set_sensitive(_apply_btn,   s);
}

void LightingPage::_on_apply(GtkButton*, gpointer self_ptr)
{
    auto* self = static_cast<LightingPage*>(self_ptr);

    ajazz::LightOptions opts;
    opts.mode = static_cast<ajazz::LightMode>(
        adw_combo_row_get_selected(ADW_COMBO_ROW(self->_mode_row)));

    const GdkRGBA* rgba =
        gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(self->_colour_btn));
    opts.r = static_cast<uint8_t>(rgba->red   * 255.0 + 0.5);
    opts.g = static_cast<uint8_t>(rgba->green * 255.0 + 0.5);
    opts.b = static_cast<uint8_t>(rgba->blue  * 255.0 + 0.5);

    opts.rainbow    = adw_switch_row_get_active(ADW_SWITCH_ROW(self->_rainbow_row));
    opts.brightness = static_cast<uint8_t>(
        adw_spin_row_get_value(ADW_SPIN_ROW(self->_bright_row)));
    opts.speed      = static_cast<uint8_t>(
        adw_spin_row_get_value(ADW_SPIN_ROW(self->_speed_row)));
    opts.direction  = static_cast<uint8_t>(
        adw_combo_row_get_selected(ADW_COMBO_ROW(self->_dir_row)));

    self->_dm.set_lighting_async(opts);
}
