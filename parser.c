/*
 * parser.c - .topo file format parser/writer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"

/* ------------------------------------------------------------------ */
/* String utilities                                                      */
/* ------------------------------------------------------------------ */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* Split "device:iface" into two separate strings */
static void split_colon(const char *src, char *left, int lsz, char *right, int rsz) {
    const char *colon = strchr(src, ':');
    if (colon) {
        int n = colon - src;
        if (n >= lsz) n = lsz - 1;
        strncpy(left, src, n); left[n] = '\0';
        strncpy(right, colon + 1, rsz - 1); right[rsz - 1] = '\0';
    } else {
        strncpy(left, src, lsz - 1); left[lsz - 1] = '\0';
        right[0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Parser state                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    SEC_NONE,
    SEC_DEVICE,
    SEC_INTERFACE,
    SEC_LINK,
    SEC_META
} Section;

/* ------------------------------------------------------------------ */
/* Load                                                                  */
/* ------------------------------------------------------------------ */

int parser_load(const char *path, Topology *topo) {
    memset(topo, 0, sizeof(*topo));

    FILE *f = fopen(path, "r");
    if (!f) {
        perror("parser_load: fopen");
        return -1;
    }

    Section  sec      = SEC_NONE;
    Device  *cur_dev  = NULL;
    Link    *cur_link = NULL;
    char     dev_id[MAX_ID_LEN]   = {0};
    char     iface_name[MAX_NAME_LEN] = {0};

    char line[512];
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *p = trim(line);

        /* Skip blank lines and comments */
        if (!*p || *p == '#' || *p == ';') continue;

        /* Section header */
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            char *header = trim(p + 1);

            if (strncmp(header, "device ", 7) == 0) {
                if (topo->device_count >= MAX_DEVICES) {
                    fprintf(stderr, "parser: too many devices (max %d)\n", MAX_DEVICES);
                    continue;
                }
                sec = SEC_DEVICE;
                char *id = trim(header + 7);
                cur_dev = &topo->devices[topo->device_count++];
                memset(cur_dev, 0, sizeof(*cur_dev));
                strncpy(cur_dev->id, id, MAX_ID_LEN - 1);
                strncpy(dev_id, id, MAX_ID_LEN - 1);
                cur_link = NULL;

            } else if (strncmp(header, "interface ", 10) == 0) {
                sec = SEC_INTERFACE;
                char rest[128];
                strncpy(rest, trim(header + 10), sizeof(rest) - 1);
                rest[sizeof(rest) - 1] = '\0';
                /* format: "device_id iface_name" */
                char *sp = strchr(rest, ' ');
                if (sp) {
                    *sp = '\0';
                    strncpy(dev_id, trim(rest), MAX_ID_LEN - 1);
                    strncpy(iface_name, trim(sp + 1), MAX_NAME_LEN - 1);
                }
                /* Find device */
                cur_dev = NULL;
                for (int i = 0; i < topo->device_count; i++) {
                    if (strcmp(topo->devices[i].id, dev_id) == 0) {
                        cur_dev = &topo->devices[i];
                        break;
                    }
                }
                cur_link = NULL;

            } else if (strcmp(header, "link") == 0) {
                if (topo->link_count >= MAX_LINKS) continue;
                sec = SEC_LINK;
                cur_link = &topo->links[topo->link_count++];
                memset(cur_link, 0, sizeof(*cur_link));
                cur_dev = NULL;

            } else if (strcmp(header, "meta") == 0) {
                sec = SEC_META;
                cur_dev  = NULL;
                cur_link = NULL;
            } else {
                sec = SEC_NONE;
            }
            continue;
        }

        /* Key = value */
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(p);
        char *val = trim(eq + 1);

        /* Strip inline comments */
        char *semi = strchr(val, ';');
        if (semi) { *semi = '\0'; val = trim(val); }

        switch (sec) {
            case SEC_DEVICE:
                if (!cur_dev) break;
                if      (strcmp(key, "hostname") == 0)
                    strncpy(cur_dev->hostname, val, MAX_NAME_LEN - 1);
                else if (strcmp(key, "type") == 0)
                    cur_dev->type = (strcmp(val, "switch") == 0) ? DEVICE_SWITCH : DEVICE_ROUTER;
                else if (strcmp(key, "console") == 0)
                    strncpy(cur_dev->console_port, val, MAX_PATH_LEN - 1);
                else if (strcmp(key, "config") == 0)
                    strncpy(cur_dev->config_file, val, MAX_PATH_LEN - 1);
                else if (strcmp(key, "ui_row") == 0)
                    cur_dev->ui_row = atoi(val);
                else if (strcmp(key, "ui_col") == 0)
                    cur_dev->ui_col = atoi(val);
                break;

            case SEC_INTERFACE:
                if (!cur_dev) break;
                if (strcmp(key, "ip") == 0) {
                    /* Find or create interface entry */
                    Interface *iface = NULL;
                    for (int i = 0; i < cur_dev->iface_count; i++) {
                        if (strcmp(cur_dev->interfaces[i].name, iface_name) == 0) {
                            iface = &cur_dev->interfaces[i];
                            break;
                        }
                    }
                    if (!iface && cur_dev->iface_count < MAX_IFACES) {
                        iface = &cur_dev->interfaces[cur_dev->iface_count++];
                        strncpy(iface->name, iface_name, MAX_NAME_LEN - 1);
                    }
                    if (iface)
                        strncpy(iface->ip, val, MAX_IP_LEN - 1);
                }
                break;

            case SEC_LINK:
                if (!cur_link) break;
                if (strcmp(key, "from") == 0)
                    split_colon(val, cur_link->from_device, MAX_ID_LEN,
                                     cur_link->from_iface,  MAX_NAME_LEN);
                else if (strcmp(key, "to") == 0)
                    split_colon(val, cur_link->to_device, MAX_ID_LEN,
                                     cur_link->to_iface,   MAX_NAME_LEN);
                else if (strcmp(key, "network") == 0)
                    strncpy(cur_link->network, val, MAX_IP_LEN - 1);
                break;

            case SEC_META:
                /* reserved for future meta fields */
                break;

            default: break;
        }
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Save                                                                  */
/* ------------------------------------------------------------------ */

int parser_save(const char *path, const Topology *topo) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("parser_save: fopen");
        return -1;
    }

    fprintf(f, "; ciscoterm topology file\n");
    fprintf(f, "; Generated automatically — edit with care\n\n");

    for (int i = 0; i < topo->device_count; i++) {
        const Device *d = &topo->devices[i];
        fprintf(f, "[device %s]\n", d->id);
        fprintf(f, "hostname = %s\n", d->hostname);
        fprintf(f, "type     = %s\n", d->type == DEVICE_SWITCH ? "switch" : "router");
        if (d->console_port[0])
            fprintf(f, "console  = %s\n", d->console_port);
        if (d->config_file[0])
            fprintf(f, "config   = %s\n", d->config_file);
        fprintf(f, "ui_row   = %d\n", d->ui_row);
        fprintf(f, "ui_col   = %d\n", d->ui_col);
        fprintf(f, "\n");

        for (int j = 0; j < d->iface_count; j++) {
            fprintf(f, "[interface %s %s]\n", d->id, d->interfaces[j].name);
            if (d->interfaces[j].ip[0])
                fprintf(f, "ip = %s\n", d->interfaces[j].ip);
            fprintf(f, "\n");
        }
    }

    for (int i = 0; i < topo->link_count; i++) {
        const Link *lnk = &topo->links[i];
        fprintf(f, "[link]\n");
        fprintf(f, "from    = %s:%s\n", lnk->from_device, lnk->from_iface);
        fprintf(f, "to      = %s:%s\n", lnk->to_device,   lnk->to_iface);
        if (lnk->network[0])
            fprintf(f, "network = %s\n", lnk->network);
        fprintf(f, "\n");
    }

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Free                                                                  */
/* ------------------------------------------------------------------ */

void topology_free(Topology *topo) {
    memset(topo, 0, sizeof(*topo));
}
