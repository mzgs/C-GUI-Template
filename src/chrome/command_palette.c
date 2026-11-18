#include "chrome_internal.h"

void chrome_draw_command_palette(app_chrome *chrome, cui_ctx *ui) {
    const char *settings_shortcut = cui_shortcut_label(ui, (cui_key_combo){ CUI_MOD_PRIMARY, CUI_KEY_COMMA });
    const char *find_shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_FIND].combo);
    const char *save_shortcut = cui_shortcut_label(ui, chrome->settings.keybindings[APP_ACTION_SAVE].combo);

    cui_command commands[] = {
        { "Open Settings", chrome_section_icons[SETTINGS_SECTION_GENERAL], "Navigation", settings_shortcut },
        { "Appearance Settings", chrome_section_icons[SETTINGS_SECTION_APPEARANCE], "Navigation", NULL },
        { "Keybindings", chrome_section_icons[SETTINGS_SECTION_KEYBINDINGS], "Navigation", NULL },
        { "About " APP_NAME, chrome_section_icons[SETTINGS_SECTION_ABOUT], "Navigation", NULL },
        { "Theme: System", chrome_theme_icons[APP_THEME_SYSTEM], "Appearance", NULL },
        { "Theme: Light", chrome_theme_icons[APP_THEME_LIGHT], "Appearance", NULL },
        { "Theme: Dark", chrome_theme_icons[APP_THEME_DARK], "Appearance", NULL },
        { "Toggle Fullscreen", CUI_ICON_MAXIMIZE_2, "Appearance", NULL },
        { "Find", CUI_ICON_SEARCH, "Actions", find_shortcut },
        { "Save", CUI_ICON_SAVE, "Actions", save_shortcut },
        { "Open Settings Folder", CUI_ICON_FOLDER_OPEN, "Actions", NULL },
    };
    int chosen = cui_command_palette(ui, &chrome->palette_open, commands, (int)(sizeof commands / sizeof commands[0]));
    switch (chosen) {
    case 0:
        chrome->settings_open = true;
        break;
    case 1:
        chrome->settings_section = SETTINGS_SECTION_APPEARANCE;
        chrome->settings_open = true;
        break;
    case 2:
        chrome->settings_section = SETTINGS_SECTION_KEYBINDINGS;
        chrome->settings_open = true;
        break;
    case 3:
        chrome->settings_section = SETTINGS_SECTION_ABOUT;
        chrome->settings_open = true;
        break;
    case 4:
    case 5:
    case 6: {
        static const app_theme_mode modes[] = { APP_THEME_SYSTEM, APP_THEME_LIGHT, APP_THEME_DARK };
        static const char *const messages[] = {
            "Theme set to System.", "Theme set to Light.", "Theme set to Dark."
        };
        chrome->settings.appearance.theme_mode = modes[chosen - 4];
        chrome->env_theme_override = false;
        chrome_apply_appearance(chrome, ui);
        if (chrome_save_settings(chrome, ui))
            app_chrome_notify(chrome, ui, messages[chosen - 4], CUI_TONE_INFO);
        break;
    }
    case 7:
        chrome_toggle_fullscreen();
        break;
    case 8:
        chrome_trigger_action(chrome, ui, APP_ACTION_FIND);
        break;
    case 9:
        chrome_trigger_action(chrome, ui, APP_ACTION_SAVE);
        break;
    case 10:
        chrome_open_settings_folder(chrome, ui);
        break;
    default:
        break;
    }
}
