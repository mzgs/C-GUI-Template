#include <cui/cui_app.h>

static void frame(cui_ctx *ui, void *user) {
    (void)ui;
    (void)user;
}

int main(void) {
    return cui_app_run(&(cui_app_desc){
        .title = "C GUI Template",
        .width = 900,
        .height = 600,
        .follow_system_theme = true,
    }, frame, NULL);
}
