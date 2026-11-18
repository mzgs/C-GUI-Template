#include <cui/cui_app.h>
#include <cui/cui.h>
#include "app_chrome.h"

typedef struct {
    app_chrome chrome;
    /* Add persistent application state here. */
} app_state;

static void on_action(cui_ctx *ui, app_action action, void *user) {
    app_state *app = user;
    switch (action) {
    case APP_ACTION_FIND:
        app_chrome_notify(&app->chrome, ui, "Find triggered.", CUI_TONE_INFO);
        break;
    case APP_ACTION_SAVE:
        app_chrome_notify(&app->chrome, ui, "Save triggered.", CUI_TONE_SUCCESS);
        break;
    default:
        break;
    }
}

static void frame(cui_ctx *ui, void *user) {
    app_state *app = user;
    app_chrome_begin_frame(&app->chrome, ui);

    /* Add your UI here. */

    app_chrome_end_frame(&app->chrome, ui);
}

int main(int argc, char **argv) {
    static app_state app;
    int exit_code = 0;
    if (!app_chrome_init(&app.chrome, argc, argv, &exit_code))
        return exit_code;
    app_chrome_set_action_handler(&app.chrome, on_action, &app);

    cui_app_desc desc = app_chrome_app_desc(&app.chrome);
    int status = cui_app_run(&desc, frame, &app);
    app_chrome_shutdown(&app.chrome);
    return status;
}
