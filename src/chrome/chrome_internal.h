#ifndef CHROME_INTERNAL_H
#define CHROME_INTERNAL_H

#include "app_chrome.h"

bool app_contains_ignore_case(const char *text, const char *query);

bool chrome_default_file_dialog(const cui_file_dialog_opts *opts, char *out, size_t cap);
void chrome_apply_appearance(app_chrome *chrome, cui_ctx *ui);
void chrome_reload_appearance(app_chrome *chrome, cui_ctx *ui);
bool chrome_save_settings(app_chrome *chrome, cui_ctx *ui);
void chrome_open_settings_folder(app_chrome *chrome, cui_ctx *ui);
void chrome_toggle_fullscreen(void);
void chrome_trigger_action(app_chrome *chrome, cui_ctx *ui, app_action action);

void chrome_draw_command_palette(app_chrome *chrome, cui_ctx *ui);
void chrome_draw_settings_dialog(app_chrome *chrome, cui_ctx *ui);

#endif
