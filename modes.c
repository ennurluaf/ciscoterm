/*
 * modes.c - Vim-like mode state machine (Normal / Insert / Command)
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "modes.h"
#include "ui.h"
#include "serial.h"
#include "parser.h"

/* ------------------------------------------------------------------ */
/* Init                                                                  */
/* ------------------------------------------------------------------ */

void modes_init(AppState *app) {
    app->mode            = MODE_NORMAL;
    app->selected_device = 0;
    app->connected_device[0] = '\0';
    app->pi_ip[0]        = '\0';
    app->topo_path[0]    = '\0';
}

/* ------------------------------------------------------------------ */
/* Normal mode key handling                                              */
/* ------------------------------------------------------------------ */

static void handle_normal(AppState *app, int ch) {
    UIState  *ui   = &app->ui;
    Topology *topo = &app->topology;

    switch (ch) {
        /* ---- Mode transitions ---- */
        case 'i':
            /* Only enter insert if console is focused & connected */
            if (ui->focus == 1 && app->serial.fd > 0) {
                app->mode = MODE_INSERT;
                ui_set_status(ui, "-- INSERT -- (Esc to exit)");
            } else if (app->serial.fd <= 0) {
                ui_set_status(ui, "Not connected — use :connect <ID> first");
            } else {
                ui->focus = 1;
                ui_set_status(ui, "Console focused — press i again to enter INSERT");
            }
            break;

        case ':':
            app->mode = MODE_COMMAND;
            memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
            ui->cmd_len = 0;
            break;

        /* ---- Panel focus ---- */
        case '\t':
            ui->focus = (ui->focus + 1) % 3;
            break;

        /* ---- Device navigation (topology panel) ---- */
        case 'j': case KEY_DOWN:
            if (topo->device_count > 0)
                app->selected_device = (app->selected_device + 1) % topo->device_count;
            break;

        case 'k': case KEY_UP:
            if (topo->device_count > 0) {
                app->selected_device--;
                if (app->selected_device < 0)
                    app->selected_device = topo->device_count - 1;
            }
            break;

        /* ---- Quick connect (Enter on selected device) ---- */
        case '\n': case KEY_ENTER:
            if (topo->device_count > 0) {
                Device *d = &topo->devices[app->selected_device];
                if (d->console_port[0]) {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd), "connect %s", d->id);
                    modes_exec_command(app, cmd);
                } else {
                    ui_set_status(ui, "Device has no console port defined");
                }
            }
            break;

        /* ---- Console scroll ---- */
        case KEY_PPAGE: /* Page Up */
            ui->console_scroll += (ui->win_console ? getmaxy(ui->win_console) - 3 : 5);
            break;
        case KEY_NPAGE: /* Page Down */
            ui->console_scroll -= (ui->win_console ? getmaxy(ui->win_console) - 3 : 5);
            if (ui->console_scroll < 0) ui->console_scroll = 0;
            break;

        case 'q':
            app->running = 0;
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Insert mode key handling                                              */
/* ------------------------------------------------------------------ */

static void handle_insert(AppState *app, int ch) {
    UIState *ui = &app->ui;

    if (ch == 27) { /* Escape */
        app->mode = MODE_NORMAL;
        ui_set_status(ui, "-- NORMAL --");
        return;
    }

    if (app->serial.fd <= 0) {
        app->mode = MODE_NORMAL;
        ui_set_status(ui, "Serial disconnected — back to NORMAL");
        return;
    }

    /* Translate ncurses key codes to bytes */
    char buf[4];
    int  n = 0;

    if (ch == KEY_ENTER || ch == '\n') {
        buf[0] = '\r'; n = 1;
    } else if (ch == KEY_BACKSPACE || ch == 127) {
        buf[0] = '\b'; n = 1;
    } else if (ch == KEY_UP) {
        buf[0] = '\x1b'; buf[1] = '['; buf[2] = 'A'; n = 3;
    } else if (ch == KEY_DOWN) {
        buf[0] = '\x1b'; buf[1] = '['; buf[2] = 'B'; n = 3;
    } else if (ch >= 0 && ch < 256) {
        buf[0] = (char)ch; n = 1;
    }

    if (n > 0) serial_write(&app->serial, buf, n);
}

/* ------------------------------------------------------------------ */
/* Command mode key handling                                             */
/* ------------------------------------------------------------------ */

static void handle_command(AppState *app, int ch) {
    UIState *ui = &app->ui;

    if (ch == 27) { /* Escape */
        app->mode = MODE_NORMAL;
        memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
        ui->cmd_len = 0;
        ui_set_status(ui, "-- NORMAL --");
        return;
    }

    if (ch == '\n' || ch == KEY_ENTER) {
        app->mode = MODE_NORMAL;
        char cmd[CMD_BUF_LEN];
        strncpy(cmd, ui->cmd_buf, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        memset(ui->cmd_buf, 0, sizeof(ui->cmd_buf));
        ui->cmd_len = 0;
        modes_exec_command(app, cmd);
        return;
    }

    if ((ch == KEY_BACKSPACE || ch == 127) && ui->cmd_len > 0) {
        ui->cmd_buf[--ui->cmd_len] = '\0';
        return;
    }

    if (ch >= 32 && ch < 127 && ui->cmd_len < CMD_BUF_LEN - 1) {
        ui->cmd_buf[ui->cmd_len++] = (char)ch;
        ui->cmd_buf[ui->cmd_len]   = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Main key dispatcher                                                   */
/* ------------------------------------------------------------------ */

void modes_handle_key(AppState *app, int ch) {
    switch (app->mode) {
        case MODE_NORMAL:  handle_normal(app, ch);  break;
        case MODE_INSERT:  handle_insert(app, ch);  break;
        case MODE_COMMAND: handle_command(app, ch); break;
    }
}

/* ------------------------------------------------------------------ */
/* Command executor  (:connect, :push-config, :load, :save, ...)        */
/* ------------------------------------------------------------------ */

/* Helper: push a config file to the serial port, line by line */
static void push_config_file(AppState *app, const char *path) {
    UIState *ui = &app->ui;

    if (app->serial.fd <= 0) {
        ui_set_status(ui, "Error: not connected to serial port");
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: cannot open config file: %s", path);
        ui_set_status(ui, msg);
        return;
    }

    char line[512];
    int  lines_sent = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Strip trailing newline, add \r\n for IOS */
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        line[len]   = '\r';
        line[len+1] = '\n';
        line[len+2] = '\0';

        serial_write(&app->serial, line, len + 2);
        ui_append_console(ui, line);
        lines_sent++;

        /* Small delay so IOS can process each line */
        usleep(80000); /* 80ms */
    }
    fclose(f);

    char msg[256];
    snprintf(msg, sizeof(msg), "Pushed %d lines from %s", lines_sent, path);
    ui_set_status(ui, msg);
}

static Device *find_device(Topology *topo, const char *id) {
    for (int i = 0; i < topo->device_count; i++)
        if (strcmp(topo->devices[i].id, id) == 0)
            return &topo->devices[i];
    return NULL;
}

void modes_exec_command(AppState *app, const char *cmd) {
    UIState  *ui   = &app->ui;
    Topology *topo = &app->topology;

    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;

    /* ---- :quit / :q ---- */
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0 ||
        strcmp(cmd, "q!") == 0) {
        app->running = 0;
        return;
    }

    /* ---- :help ---- */
    if (strcmp(cmd, "help") == 0) {
        ui_set_status(ui,
            "Commands: connect <ID>, disconnect, push-config <ID>, "
            "load <f>, save [f], set pi-ip <ip>, quit");
        return;
    }

    /* ---- :connect <ID> ---- */
    if (strncmp(cmd, "connect ", 8) == 0) {
        const char *id = cmd + 8;
        Device *d = find_device(topo, id);
        if (!d) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Error: device '%s' not found", id);
            ui_set_status(ui, msg);
            return;
        }
        if (!d->console_port[0]) {
            ui_set_status(ui, "Error: device has no console port defined");
            return;
        }
        serial_close(&app->serial);
        if (serial_open(&app->serial, d->console_port, 9600) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Error: could not open %s", d->console_port);
            ui_set_status(ui, msg);
            return;
        }
        strncpy(app->connected_device, id, sizeof(app->connected_device) - 1);
        char msg[128];
        snprintf(msg, sizeof(msg), "Connected to %s (%s) — press i for INSERT mode",
                 d->hostname, d->console_port);
        ui_set_status(ui, msg);
        ui->focus = 1;
        return;
    }

    /* ---- :disconnect ---- */
    if (strcmp(cmd, "disconnect") == 0) {
        serial_close(&app->serial);
        app->connected_device[0] = '\0';
        ui_set_status(ui, "Disconnected");
        return;
    }

    /* ---- :push-config <ID> ---- */
    if (strncmp(cmd, "push-config ", 12) == 0) {
        const char *id = cmd + 12;
        Device *d = find_device(topo, id);
        if (!d) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Error: device '%s' not found", id);
            ui_set_status(ui, msg);
            return;
        }
        if (!d->config_file[0]) {
            ui_set_status(ui, "Error: no config_file defined for this device");
            return;
        }
        push_config_file(app, d->config_file);
        return;
    }

    /* ---- :load <file> ---- */
    if (strncmp(cmd, "load ", 5) == 0) {
        const char *path = cmd + 5;
        topology_free(topo);
        if (parser_load(path, topo) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Error: failed to load '%s'", path);
            ui_set_status(ui, msg);
        } else {
            strncpy(app->topo_path, path, sizeof(app->topo_path) - 1);
            char msg[256];
            snprintf(msg, sizeof(msg), "Loaded '%s' — %d devices, %d links",
                     path, topo->device_count, topo->link_count);
            ui_set_status(ui, msg);
            app->selected_device = 0;
        }
        return;
    }

    /* ---- :save [file] ---- */
    if (strncmp(cmd, "save", 4) == 0) {
        const char *path = (strlen(cmd) > 5) ? cmd + 5 : app->topo_path;
        if (!path || !path[0]) {
            ui_set_status(ui, "Error: specify a filename — :save <file>");
            return;
        }
        if (parser_save(path, topo) != 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Error: failed to save '%s'", path);
            ui_set_status(ui, msg);
        } else {
            strncpy(app->topo_path, path, sizeof(app->topo_path) - 1);
            char msg[128];
            snprintf(msg, sizeof(msg), "Saved topology to '%s'", path);
            ui_set_status(ui, msg);
        }
        return;
    }

    /* ---- :set pi-ip <ip> ---- */
    if (strncmp(cmd, "set pi-ip ", 10) == 0) {
        strncpy(app->pi_ip, cmd + 10, sizeof(app->pi_ip) - 1);
        char msg[128];
        snprintf(msg, sizeof(msg), "Pi IP set to %s", app->pi_ip);
        ui_set_status(ui, msg);
        return;
    }

    /* ---- :set baud <rate> ---- */
    if (strncmp(cmd, "set baud ", 9) == 0) {
        int baud = atoi(cmd + 9);
        if (baud > 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Baud set to %d (reconnect to apply)", baud);
            ui_set_status(ui, msg);
        }
        return;
    }

    /* ---- Unknown command ---- */
    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown command: %s", cmd);
    ui_set_status(ui, msg);
}
