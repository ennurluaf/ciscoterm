/*
 * ciscoterm - Cisco IOS TUI Console Manager
 * main.c - Entry point, initialization, main loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>

#include "ui.h"
#include "modes.h"
#include "serial.h"
#include "parser.h"

static AppState g_app;

static void handle_resize(int sig) {
    (void)sig;
    g_app.needs_resize = 1;
}

static void handle_sigint(int sig) {
    (void)sig;
    g_app.running = 0;
}

static void cleanup(void) {
    serial_close(&g_app.serial);
    ui_cleanup(&g_app.ui);
    topology_free(&g_app.topology);
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    memset(&g_app, 0, sizeof(g_app));
    g_app.running = 1;
    g_app.mode = MODE_NORMAL;

    /* Parse CLI arguments */
    const char *topo_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            topo_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [-f topology.topo]\n", argv[0]);
            printf("  -f FILE   Load topology file on startup\n");
            return 0;
        }
    }

    /* Initialize subsystems */
    if (ui_init(&g_app.ui) != 0) {
        fprintf(stderr, "Failed to initialize UI\n");
        return 1;
    }

    modes_init(&g_app);

    /* Load topology if specified */
    if (topo_file) {
        if (parser_load(topo_file, &g_app.topology) != 0) {
            ui_set_status(&g_app.ui, "Warning: failed to load topology file");
        } else {
            snprintf(g_app.ui.status_msg, sizeof(g_app.ui.status_msg),
                     "Loaded: %s (%d devices)", topo_file, g_app.topology.device_count);
        }
    } else {
        ui_set_status(&g_app.ui, "ciscoterm ready — :help for commands, i for insert mode");
    }

    /* Signal handlers */
    signal(SIGWINCH, handle_resize);
    signal(SIGINT,  handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* Main loop */
    while (g_app.running) {
        if (g_app.needs_resize) {
            ui_resize(&g_app.ui);
            g_app.needs_resize = 0;
        }

        /* Drain serial input into bottom-left panel */
        if (g_app.serial.fd > 0) {
            char buf[256];
            int n = serial_read(&g_app.serial, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                ui_append_console(&g_app.ui, buf);
            }
        }

        /* Draw everything */
        ui_draw(&g_app.ui, &g_app);

        /* Handle keyboard input (100ms timeout) */
        int ch = ui_getch_timeout(&g_app.ui, 100);
        if (ch != ERR) {
            modes_handle_key(&g_app, ch);
        }
    }

    cleanup();
    printf("ciscoterm exited cleanly.\n");
    return 0;
}
