/*
 * modes.h - Vim-like mode state machine
 */

#ifndef MODES_H
#define MODES_H

#include "ui.h"
#include "serial.h"
#include "parser.h"

typedef enum {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_COMMAND
} AppMode;

typedef struct AppState {
    int running;
    int needs_resize;

    AppMode    mode;
    UIState    ui;
    SerialConn serial;
    Topology   topology;

    /* Currently selected device index in topology array */
    int selected_device;

    /* ID of device currently connected via console */
    char connected_device[64];

    /* Raspberry Pi management IP */
    char pi_ip[64];

    /* Topology file path (for :save without args) */
    char topo_path[256];
} AppState;

void modes_init(AppState *app);
void modes_handle_key(AppState *app, int ch);

/* Command mode dispatcher */
void modes_exec_command(AppState *app, const char *cmd);

#endif /* MODES_H */
