#include "time_page.h"

#include <adwaita.h>
#include <ctime>

TimePage::TimePage(DeviceManager& dm) : _dm(dm)
{
    _root = adw_preferences_page_new();
    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(_root), "Time");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(_root),
                                       "appointment-new-symbolic");

    // -----------------------------------------------------------------------
    // Clock sync group
    // -----------------------------------------------------------------------
    GtkWidget* grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(grp), "Display Clock");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(grp),
        "The keyboard has an autonomous RTC. After one sync it keeps time "
        "independently — no daemon required.");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(grp));

    // Sync now action row
    GtkWidget* sync_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sync_row),
                                  "Sync to System Time");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(sync_row),
                                "Sets the display clock to the current local time");

    _sync_btn = gtk_button_new_with_label("Sync Now");
    gtk_widget_add_css_class(_sync_btn, "suggested-action");
    gtk_widget_set_valign(_sync_btn, GTK_ALIGN_CENTER);
    g_signal_connect(_sync_btn, "clicked", G_CALLBACK(_on_sync_now), this);
    adw_action_row_add_suffix(ADW_ACTION_ROW(sync_row), _sync_btn);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(sync_row), _sync_btn);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(grp), sync_row);

    // -----------------------------------------------------------------------
    // Manual time group (expandable)
    // -----------------------------------------------------------------------
    GtkWidget* manual_grp = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(manual_grp),
                                    "Manual Time Entry");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(manual_grp));

    _hour_spin = adw_spin_row_new_with_range(0, 23, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_hour_spin), "Hour");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(manual_grp), _hour_spin);

    _min_spin = adw_spin_row_new_with_range(0, 59, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_min_spin), "Minute");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(manual_grp), _min_spin);

    _sec_spin = adw_spin_row_new_with_range(0, 59, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(_sec_spin), "Second");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(manual_grp), _sec_spin);

    GtkWidget* apply_grp = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(_root),
                              ADW_PREFERENCES_GROUP(apply_grp));

    _manual_btn = gtk_button_new_with_label("Set Time");
    gtk_widget_add_css_class(_manual_btn, "pill");
    gtk_widget_set_halign(_manual_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(_manual_btn, 8);
    gtk_widget_set_margin_bottom(_manual_btn, 8);
    g_signal_connect(_manual_btn, "clicked", G_CALLBACK(_on_manual_apply), this);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(apply_grp), _manual_btn);
}

void TimePage::set_sensitive(bool s)
{
    gtk_widget_set_sensitive(_sync_btn,   s);
    gtk_widget_set_sensitive(_hour_spin,  s);
    gtk_widget_set_sensitive(_min_spin,   s);
    gtk_widget_set_sensitive(_sec_spin,   s);
    gtk_widget_set_sensitive(_manual_btn, s);
}

void TimePage::_on_sync_now(GtkButton*, gpointer self_ptr)
{
    static_cast<TimePage*>(self_ptr)->_dm.sync_time_async();
}

void TimePage::_on_manual_apply(GtkButton*, gpointer self_ptr)
{
    auto* self = static_cast<TimePage*>(self_ptr);

    std::time_t now = std::time(nullptr);
    std::tm t = *std::localtime(&now);
    t.tm_hour = static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(self->_hour_spin)));
    t.tm_min  = static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(self->_min_spin)));
    t.tm_sec  = static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(self->_sec_spin)));

    self->_dm.sync_time_async(&t);
}
