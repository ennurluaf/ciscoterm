/*
 * ui.c - ncurses TUI layout, panels, and rendering
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"
#include "modes.h"
#include "parser.h"

/* ------------------------------------------------------------------ */
/* Window geometry helpers                                              */
/* ------------------------------------------------------------------ */

static void get_geometry(int rows, int cols,
                         int *left_w, int *right_w,
                         int *top_h,  int *bot_h)
{
    *right_w = cols / 3;
    *left_w  = cols - *right_w;
    *top_h   = (rows - 1) / 2;   /* -1 for status bar */
    *bot_h   = (rows - 1) - *top_h;
}

/* ------------------------------------------------------------------ */
/* Initialization / cleanup                                             */
/* ------------------------------------------------------------------ */

int ui_init(UIState *ui) {
    memset(ui, 0, sizeof(*ui));

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Terminal does not support colors\n");
        return -1;
    }
    start_color();
    use_default_colors();

    init_pair(CP_NORMAL,        COLOR_WHITE,   -1);
    init_pair(CP_SELECTED,      COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_BORDER,        COLOR_CYAN,    -1);
    init_pair(CP_STATUS_NORMAL, COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_STATUS_INSERT, COLOR_BLACK,   COLOR_GREEN);
    init_pair(CP_STATUS_CMD,    COLOR_BLACK,   COLOR_YELLOW);
    init_pair(CP_DEVICE_ROUTER, COLOR_GREEN,   -1);
    init_pair(CP_DEVICE_SWITCH, COLOR_YELLOW,  -1);
    init_pair(CP_LINK,          COLOR_BLUE,    -1);
    init_pair(CP_HIGHLIGHT,     COLOR_BLACK,   COLOR_CYAN);

    getmaxyx(stdscr, ui->term_rows, ui->term_cols);

    int lw, rw, th, bh;
    get_geometry(ui->term_rows, ui->term_cols, &lw, &rw, &th, &bh);

    ui->win_topology = newwin(th,  lw, 0,  0);
    ui->win_console  = newwin(bh,  lw, th, 0);
    ui->win_sidebar  = newwin(ui->term_rows - 1, rw, 0, lw);
    ui->win_status   = newwin(1,   ui->term_cols, ui->term_rows - 1, 0);

    if (!ui->win_topology || !ui->win_console ||
        !ui->win_sidebar  || !ui->win_status) {
        endwin();
        return -1;
    }

    scrollok(ui->win_console, FALSE); /* we manage scrolling manually */
    keypad(ui->win_console,  TRUE);
    keypad(ui->win_topology, TRUE);
    keypad(ui->win_sidebar,  TRUE);

    snprintf(ui->status_msg, sizeof(ui->status_msg),
             "ciscoterm v0.1  |  :help for commands");
    return 0;
}

void ui_cleanup(UIState *ui) {
    if (ui->win_topology) delwin(ui->win_topology);
    if (ui->win_console)  delwin(ui->win_console);
    if (ui->win_sidebar)  delwin(ui->win_sidebar);
    if (ui->win_status)   delwin(ui->win_status);
    endwin();
}

void ui_resize(UIState *ui) {
    endwin();
    refresh();
    getmaxyx(stdscr, ui->term_rows, ui->term_cols);

    int lw, rw, th, bh;
    get_geometry(ui->term_rows, ui->term_cols, &lw, &rw, &th, &bh);

    wresize(ui->win_topology, th,  lw);
    wresize(ui->win_console,  bh,  lw);
    wresize(ui->win_sidebar,  ui->term_rows - 1, rw);
    wresize(ui->win_status,   1,   ui->term_cols);

    mvwin(ui->win_topology, 0,  0);
    mvwin(ui->win_console,  th, 0);
    mvwin(ui->win_sidebar,  0,  lw);
    mvwin(ui->win_status,   ui->term_rows - 1, 0);

    clearok(stdscr, TRUE);
}

/* ------------------------------------------------------------------ */
/* Console buffer                                                        */
/* ------------------------------------------------------------------ */

void ui_append_console(UIState *ui, const char *text) {
    int n = strlen(text);
    if (ui->console_len + n >= CONSOLE_SCROLL_BUF - 1) {
        /* Shift buffer left by half */
        int keep = CONSOLE_SCROLL_BUF / 2;
        memmove(ui->console_buf,
                ui->console_buf + (ui->console_len - keep), keep);
        ui->console_len = keep;
    }
    memcpy(ui->console_buf + ui->console_len, text, n);
    ui->console_len += n;
    ui->console_buf[ui->console_len] = '\0';
    ui->console_scroll = 0; /* jump to bottom on new data */
}

void ui_set_status(UIState *ui, const char *msg) {
    snprintf(ui->status_msg, sizeof(ui->status_msg), "%s", msg);
}

/* ------------------------------------------------------------------ */
/* Topology panel                                                        */
/* ------------------------------------------------------------------ */

/* Simple ASCII topology renderer */
void ui_draw_topology(UIState *ui, AppState *app) {
    WINDOW *w = ui->win_topology;
    int rows, cols;
    getmaxyx(w, rows, cols);
    (void)cols;

    werase(w);

    /* Border */
    int focused = (ui->focus == 0);
    wattron(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " TOPOLOGY ");
    wattroff(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));

    Topology *topo = &app->topology;

    if (topo->device_count == 0) {
        wattron(w, A_DIM);
        mvwprintw(w, rows / 2, 4, "No topology loaded.");
        mvwprintw(w, rows / 2 + 1, 4, "Use :load <file.topo> to load one.");
        wattroff(w, A_DIM);
        wnoutrefresh(w);
        return;
    }

    /* Draw links first (dashed lines) */
    wattron(w, COLOR_PAIR(CP_LINK));
    for (int i = 0; i < topo->link_count; i++) {
        Link *lnk = &topo->links[i];
        Device *a = NULL, *b = NULL;
        for (int d = 0; d < topo->device_count; d++) {
            if (strcmp(topo->devices[d].id, lnk->from_device) == 0) a = &topo->devices[d];
            if (strcmp(topo->devices[d].id, lnk->to_device)   == 0) b = &topo->devices[d];
        }
        if (!a || !b) continue;

        /* Simple horizontal connector between device render positions */
        int ay = a->ui_row + 1, ax = a->ui_col + (int)strlen(a->hostname) + 2;
        int by = b->ui_row + 1, bx = b->ui_col - 1;
        if (ax < bx && ay == by) {
            for (int x = ax; x <= bx; x++) mvwaddch(w, ay, x, '-');
        } else {
            /* just draw a marker if layout doesn't allow simple line */
            mvwprintw(w, ay, ax, "~~");
        }
        /* Show network label in the middle */
        if (lnk->network[0] && ax + 3 < bx) {
            int mx = (ax + bx) / 2 - (int)strlen(lnk->network) / 2;
            wattron(w, A_DIM);
            mvwprintw(w, ay - 1, mx, "%s", lnk->network);
            wattroff(w, A_DIM);
        }
    }
    wattroff(w, COLOR_PAIR(CP_LINK));

    /* Draw devices */
    for (int i = 0; i < topo->device_count; i++) {
        Device *d = &topo->devices[i];
        int row = d->ui_row + 1;  /* +1 for border */
        int col = d->ui_col + 1;

        if (row <= 0 || row >= rows - 1) continue;

        int is_selected  = (i == app->selected_device);
        int is_connected = (strcmp(d->id, app->connected_device) == 0);

        int cp = (d->type == DEVICE_ROUTER) ? CP_DEVICE_ROUTER : CP_DEVICE_SWITCH;
        if (is_selected)  { wattron(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD); }
        else              { wattron(w, COLOR_PAIR(cp)); }

        /* Device icon + name */
        const char *icon = (d->type == DEVICE_ROUTER) ? "[R]" : "[S]";
        mvwprintw(w, row, col, "%s %s", icon, d->hostname);

        if (is_connected) {
            wattron(w, COLOR_PAIR(CP_STATUS_INSERT));
            mvwprintw(w, row, col + 5 + (int)strlen(d->hostname), " *CON*");
            wattroff(w, COLOR_PAIR(CP_STATUS_INSERT));
        }

        if (is_selected)  { wattroff(w, COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD); }
        else              { wattroff(w, COLOR_PAIR(cp)); }

        /* Show interface list below device name */
        for (int j = 0; j < d->iface_count && row + 1 + j < rows - 1; j++) {
            wattron(w, A_DIM);
            mvwprintw(w, row + 1 + j, col + 5,
                      "  %s %s", d->interfaces[j].name,
                      d->interfaces[j].ip[0] ? d->interfaces[j].ip : "");
            wattroff(w, A_DIM);
        }
    }

    /* Navigation hint */
    wattron(w, A_DIM);
    mvwprintw(w, rows - 2, 2, "j/k: select  Enter: connect  Tab: switch panel");
    wattroff(w, A_DIM);

    wnoutrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Console panel                                                         */
/* ------------------------------------------------------------------ */

void ui_draw_console(UIState *ui) {
    WINDOW *w = ui->win_console;
    int rows, cols;
    getmaxyx(w, rows, cols);

    werase(w);

    int focused = (ui->focus == 1);
    wattron(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " CONSOLE ");
    wattroff(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));

    int inner_rows = rows - 2;
    int inner_cols = cols - 2;

    /* Split console_buf into lines */
    char *lines[512];
    int   line_count = 0;
    char  tmp[CONSOLE_SCROLL_BUF];
    strncpy(tmp, ui->console_buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *p = tmp;
    char *start = p;
    while (*p && line_count < 512) {
        if (*p == '\n') {
            *p = '\0';
            lines[line_count++] = start;
            start = p + 1;
        }
        p++;
    }
    if (*start) lines[line_count++] = start;

    /* Show last inner_rows lines (minus scroll offset) */
    int first = line_count - inner_rows - ui->console_scroll;
    if (first < 0) first = 0;

    for (int i = 0; i < inner_rows && first + i < line_count; i++) {
        char cropped[256];
        snprintf(cropped, sizeof(cropped), "%.*s", inner_cols, lines[first + i]);
        mvwprintw(w, i + 1, 1, "%s", cropped);
    }

    if (ui->console_scroll > 0) {
        wattron(w, A_REVERSE);
        mvwprintw(w, 1, cols - 12, " SCROLL %3d", ui->console_scroll);
        wattroff(w, A_REVERSE);
    }

    wnoutrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Sidebar panel                                                         */
/* ------------------------------------------------------------------ */

void ui_draw_sidebar(UIState *ui, AppState *app) {
    WINDOW *w = ui->win_sidebar;
    int rows, cols;
    getmaxyx(w, rows, cols);
    (void)rows;

    werase(w);

    int focused = (ui->focus == 2);
    wattron(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " CONFIG ");
    wattroff(w, COLOR_PAIR(focused ? CP_BORDER : CP_NORMAL));

    int row = 2;

    /* Pi IP */
    wattron(w, A_BOLD);
    mvwprintw(w, row++, 2, "Pi IP Address:");
    wattroff(w, A_BOLD);
    mvwprintw(w, row++, 2, "  %s",
              app->pi_ip[0] ? app->pi_ip : "(not set)");
    row++;

    /* Connected device */
    wattron(w, A_BOLD);
    mvwprintw(w, row++, 2, "Console Connection:");
    wattroff(w, A_BOLD);

    if (app->connected_device[0]) {
        wattron(w, COLOR_PAIR(CP_STATUS_INSERT) | A_BOLD);
        mvwprintw(w, row++, 2, "  CONNECTED: %s", app->connected_device);
        wattroff(w, COLOR_PAIR(CP_STATUS_INSERT) | A_BOLD);
        mvwprintw(w, row++, 2, "  Port: %s", app->serial.device_path[0]
                                              ? app->serial.device_path : "?");
    } else {
        wattron(w, A_DIM);
        mvwprintw(w, row++, 2, "  (not connected)");
        wattroff(w, A_DIM);
    }
    row++;

    /* Selected device metadata */
    Topology *topo = &app->topology;
    if (topo->device_count > 0 && app->selected_device < topo->device_count) {
        Device *d = &topo->devices[app->selected_device];

        wattron(w, A_BOLD);
        mvwprintw(w, row++, 2, "Selected Device:");
        wattroff(w, A_BOLD);

        mvwprintw(w, row++, 2, "  ID:       %s", d->id);
        mvwprintw(w, row++, 2, "  Hostname: %s", d->hostname);
        mvwprintw(w, row++, 2, "  Type:     %s",
                  d->type == DEVICE_ROUTER ? "Router" : "Switch");
        mvwprintw(w, row++, 2, "  Console:  %s",
                  d->console_port[0] ? d->console_port : "(none)");
        row++;

        wattron(w, A_BOLD);
        mvwprintw(w, row++, 2, "Interfaces:");
        wattroff(w, A_BOLD);
        for (int i = 0; i < d->iface_count; i++) {
            char line[64];
            snprintf(line, sizeof(line), "  %-10s %s",
                     d->interfaces[i].name,
                     d->interfaces[i].ip[0] ? d->interfaces[i].ip : "no ip");
            mvwprintw(w, row++, 2, "%.*s", cols - 4, line);
        }
        row++;

        if (d->config_file[0]) {
            wattron(w, A_BOLD);
            mvwprintw(w, row++, 2, "Config File:");
            wattroff(w, A_BOLD);
            mvwprintw(w, row++, 2, "  %s", d->config_file);
        }
    }

    /* Commands quick-ref */
    int qr_row = rows - 10;
    if (qr_row > row + 1) {
        wattron(w, A_DIM);
        mvwprintw(w, qr_row++, 2, "─── Quick Commands ─────────");
        mvwprintw(w, qr_row++, 2, ":connect <ID>");
        mvwprintw(w, qr_row++, 2, ":push-config <ID>");
        mvwprintw(w, qr_row++, 2, ":load <file.topo>");
        mvwprintw(w, qr_row++, 2, ":save <file.topo>");
        mvwprintw(w, qr_row++, 2, ":set pi-ip <ip>");
        mvwprintw(w, qr_row++, 2, ":disconnect");
        mvwprintw(w, qr_row++, 2, ":help  :quit");
        wattroff(w, A_DIM);
    }

    wnoutrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Status bar                                                            */
/* ------------------------------------------------------------------ */

void ui_draw_statusbar(UIState *ui, AppState *app) {
    WINDOW *w = ui->win_status;
    int cols = 0, _rows_unused = 0;
    getmaxyx(w, _rows_unused, cols);
    (void)_rows_unused;

    werase(w);

    const char *mode_str;
    int mode_cp;
    switch (app->mode) {
        case MODE_INSERT:  mode_str = " INSERT ";  mode_cp = CP_STATUS_INSERT; break;
        case MODE_COMMAND: mode_str = " COMMAND "; mode_cp = CP_STATUS_CMD;    break;
        default:           mode_str = " NORMAL ";  mode_cp = CP_STATUS_NORMAL; break;
    }

    wattron(w, COLOR_PAIR(mode_cp) | A_BOLD);
    mvwprintw(w, 0, 0, "%s", mode_str);
    wattroff(w, COLOR_PAIR(mode_cp) | A_BOLD);

    int offset = strlen(mode_str) + 1;

    if (app->mode == MODE_COMMAND) {
        wattron(w, A_BOLD);
        mvwprintw(w, 0, offset, ":%.*s", cols - offset - 2, ui->cmd_buf);
        wattroff(w, A_BOLD);
        /* Show cursor in command mode */
        curs_set(1);
        wmove(w, 0, offset + 1 + ui->cmd_len);
    } else {
        curs_set(0);
        mvwprintw(w, 0, offset, "%.*s", cols - offset - 1, ui->status_msg);
    }

    wnoutrefresh(w);
}

/* ------------------------------------------------------------------ */
/* Main draw dispatch                                                    */
/* ------------------------------------------------------------------ */

void ui_draw(UIState *ui, AppState *app) {
    ui_draw_topology(ui, app);
    ui_draw_console(ui);
    ui_draw_sidebar(ui, app);
    ui_draw_statusbar(ui, app);
    doupdate();
}

/* ------------------------------------------------------------------ */
/* Input                                                                 */
/* ------------------------------------------------------------------ */

int ui_getch_timeout(UIState *ui, int ms) {
    WINDOW *active = NULL;
    switch (ui->focus) {
        case 0: active = ui->win_topology; break;
        case 1: active = ui->win_console;  break;
        case 2: active = ui->win_sidebar;  break;
        default: active = stdscr;
    }
    wtimeout(active, ms);
    return wgetch(active);
}
