/*
 * parser.h - .topo file format loader/saver
 *
 * File format (INI-like):
 *
 *   [device <ID>]
 *   hostname = R1
 *   type     = router          ; router | switch
 *   console  = /dev/ttyUSB0
 *   config   = configs/R1.cfg
 *   ui_row   = 2
 *   ui_col   = 4
 *
 *   [interface <device_ID> <name>]
 *   ip       = 192.168.1.1/24
 *
 *   [link]
 *   from     = R1:Fa0/0
 *   to       = R2:Fa0/0
 *   network  = 192.168.1.0/24
 *
 *   [meta]
 *   pi_ip    = 192.168.100.1
 */

#ifndef PARSER_H
#define PARSER_H

#define MAX_DEVICES    32
#define MAX_LINKS      64
#define MAX_IFACES     16
#define MAX_ID_LEN     32
#define MAX_NAME_LEN   64
#define MAX_PATH_LEN  128
#define MAX_IP_LEN     20

typedef enum {
    DEVICE_ROUTER = 0,
    DEVICE_SWITCH
} DeviceType;

typedef struct {
    char name[MAX_NAME_LEN];
    char ip[MAX_IP_LEN];
} Interface;

typedef struct {
    char       id[MAX_ID_LEN];
    char       hostname[MAX_NAME_LEN];
    DeviceType type;
    char       console_port[MAX_PATH_LEN]; /* e.g. /dev/ttyUSB0 */
    char       config_file[MAX_PATH_LEN];  /* path to IOS config  */

    Interface  interfaces[MAX_IFACES];
    int        iface_count;

    /* UI layout hints (row/col within topology panel) */
    int ui_row;
    int ui_col;
} Device;

typedef struct {
    char from_device[MAX_ID_LEN];
    char from_iface[MAX_NAME_LEN];
    char to_device[MAX_ID_LEN];
    char to_iface[MAX_NAME_LEN];
    char network[MAX_IP_LEN];
} Link;

typedef struct {
    Device devices[MAX_DEVICES];
    int    device_count;

    Link   links[MAX_LINKS];
    int    link_count;
} Topology;

/**
 * Load topology from file. Returns 0 on success.
 */
int  parser_load(const char *path, Topology *topo);

/**
 * Save topology to file. Returns 0 on success.
 */
int  parser_save(const char *path, const Topology *topo);

/**
 * Free any heap resources held by topo (currently none,
 * but call this to stay future-proof).
 */
void topology_free(Topology *topo);

#endif /* PARSER_H */
