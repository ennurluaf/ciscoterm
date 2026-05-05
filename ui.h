/*
 * ui.h - ncurses TUI layout and rendering
 */

#ifndef UI_H
#define UI_H

#include <ncurses.h>

/* Forward declarations */
typedef struct AppState AppState;
struct Topology;

#define CONSOLE_SCROLL_BUF 4096
#define STATUS_MSG_LEN      256
#define CMD_BUF_LEN         256

/* Color pair IDs */
#define CP_NORMAL       1
#define CP_SELECTED     2
#define CP_BORDER       3
#define CP_STATUS_NORMAL 4
#define CP_STATUS_INSERT 5
#define CP_STATUS_CMD    6
#define CP_DEVICE_ROUTER 7
#define CP_DEVICE_SWITCH 8
#define CP_LINK          9
#define CP_HIGHLIGHT     10

typedef struct {
    WINDOW *win_topology;   /* top-left: topology map     */
    WINDOW *win_console;    /* bottom-left: IOS terminal  */
    WINDOW *win_sidebar;    /* right: config panel        */
    WINDOW *win_status;     /* bottom status bar          */

    int term_rows, term_cols;

    /* Console output buffer */
    char console_buf[CONSOLE_SCROLL_BUF];
    int  console_len;
    int  console_scroll;    /* lines scrolled up from bottom */

    /* Status bar */
    char status_msg[STATUS_MSG_LEN];

    /* Command line buffer (command mode) */
    char cmd_buf[CMD_BUF_LEN];
    int  cmd_len;

    /* Active panel focus: 0=topology, 1=console, 2=sidebar */
    int focus;
} UIState;

int  ui_init(UIState *ui);
void ui_cleanup(UIState *ui);
void ui_resize(UIState *ui);
void ui_draw(UIState *ui, AppState *app);
void ui_set_status(UIState *ui, const char *msg);
void ui_append_console(UIState *ui, const char *text);
int  ui_getch_timeout(UIState *ui, int ms);

/* Internal draw helpers (used by modes too) */
void ui_draw_topology(UIState *ui, AppState *app);
void ui_draw_console(UIState *ui);
void ui_draw_sidebar(UIState *ui, AppState *app);
void ui_draw_statusbar(UIState *ui, AppState *app);

#endif /* UI_H */
