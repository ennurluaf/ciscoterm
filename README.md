# ciscoterm — Documentation

## Overview

**ciscoterm** is a Vim-like TUI application for configuring Cisco network devices
from a Raspberry Pi running Ubuntu. It provides a split-screen terminal with:

- **Topology panel** — ASCII network map
- **Console panel** — raw serial IOS terminal
- **Sidebar** — device metadata and quick-reference


---

## Build Instructions

### Dependencies (Raspberry Pi / Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y libncurses5-dev libncursesw5-dev gcc make
```

Or use the convenience target:

```bash
make deps
```

### Compile

```bash
make          # release build
make debug    # debug build with AddressSanitizer
```

### Run

```bash
./ciscoterm                    # start empty
./ciscoterm -f lab.topo        # load topology on startup
```

### Install system-wide

```bash
sudo make install              # copies to /usr/local/bin/ciscoterm
```

---

## Keybindings

### Normal Mode (default)

| Key            | Action                                      |
|----------------|---------------------------------------------|
| `j` / `↓`      | Select next device in topology              |
| `k` / `↑`      | Select previous device in topology          |
| `Enter`        | Connect to selected device's console port   |
| `Tab`          | Cycle panel focus (Topology → Console → Sidebar) |
| `i`            | Enter INSERT mode (console must be focused & connected) |
| `:`            | Enter COMMAND mode                          |
| `PgUp`         | Scroll console output up                    |
| `PgDn`         | Scroll console output down                  |
| `q`            | Quit                                        |

### Insert Mode

| Key            | Action                                      |
|----------------|---------------------------------------------|
| Any printable  | Send character to serial device             |
| `Enter`        | Send carriage return (`\r`) to device       |
| `Backspace`    | Send backspace to device                    |
| `↑` / `↓`      | Send ANSI arrow key sequences               |
| `Esc`          | Return to NORMAL mode                       |

### Command Mode

Triggered by `:`. Type a command and press `Enter`. Press `Esc` to cancel.

---

## Commands

```
:connect <ID>           Connect serial to device ID's console port
:disconnect             Close current serial connection
:push-config <ID>       Send device's config file line-by-line to console
:load <file.topo>       Load a topology file
:save [file.topo]       Save current topology (uses last loaded path if omitted)
:set pi-ip <ip>         Set the Raspberry Pi's management IP (display only)
:set baud <rate>        Set baud rate for next connection (9600, 115200, etc.)
:help                   Show command summary in status bar
:quit  /  :q            Exit ciscoterm
```

---

## .topo File Format

The `.topo` format is an INI-style text file with three section types.

### Device Section

```ini
[device <ID>]
hostname = R1-Core
type     = router        ; router | switch
console  = /dev/ttyUSB0  ; serial device path
config   = configs/R1.cfg ; IOS config to push
ui_row   = 2             ; row in topology panel (0-based)
ui_col   = 4             ; column in topology panel
```

### Interface Section

```ini
[interface <device_ID> <interface_name>]
ip = 10.0.12.1/30
```

### Link Section

```ini
[link]
from    = R1:Fa0/0
to      = R2:Fa0/0
network = 10.0.12.0/30
```

### Comments

Lines starting with `;` or `#` are ignored.

---

## Serial Console

ciscoterm uses `/dev/ttyUSBx` devices via a standard USB-to-RJ45 console cable
(Cisco rollover cable).

### Default settings

- Baud: **9600** (Cisco default)
- Data bits: **8**
- Parity: **None**
- Stop bits: **1**
- Flow control: **None**

### Permissions

```bash
# Add your user to the dialout group (log out/in after):
sudo usermod -aG dialout $USER

# Or temporarily:
sudo chmod 666 /dev/ttyUSB0
```

### Identify USB serial adapters

```bash
ls /dev/ttyUSB*
dmesg | grep tty
```

---

## Pushing Configs

The `:push-config <ID>` command reads the device's `config` file and sends each
line to the console with an 80ms inter-line delay. This prevents IOS from
dropping lines when pasting.

**Tip:** Start by entering config mode on the device first (via INSERT mode):

```
R1# conf t
R1(config)#
```

Then run `:push-config R1`.

---

## Architecture

```
main.c          Entry point, main loop, signal handling
ui.c / ui.h     ncurses layout, panel rendering, input dispatch
modes.c / .h    State machine: Normal / Insert / Command
serial.c / .h   termios serial open/read/write/close
parser.c / .h   .topo file load/save, Topology/Device/Link structs
```

### Threading model

ciscoterm uses a **single-threaded event loop** with a 100ms `wgetch` timeout:

1. Check serial fd for available bytes (non-blocking `read`)
2. Append any received bytes to the console buffer
3. Redraw all panels (`doupdate`)
4. Wait up to 100ms for keyboard input
5. Dispatch key to current mode handler

This is sufficient for 9600 baud (≈ 1 KB/s). For 115200 baud or bulk config
pushes, consider adding a background read thread in a future version.

---

## Extending ciscoterm

### Adding SSH support

1. Add `ssh_conn.c / ssh_conn.h` wrapping **libssh2**
2. Add a `conn_type` field to `Device` (`CONN_SERIAL`, `CONN_SSH`, `CONN_TELNET`)
3. In `:connect`, branch on `conn_type` to call `serial_open` or `ssh_connect`
4. Wrap both under a common `conn_read` / `conn_write` abstraction used by the
   main loop and INSERT mode

### Adding config templates

1. Add a `templates/` directory with partial IOS snippets
2. Add `:apply-template <name> <device>` command in `modes_exec_command`
3. Substitute variables (hostname, IP, etc.) from the `Device` struct before pushing

### Improving topology rendering

The current renderer places devices at `(ui_row, ui_col)` from the `.topo` file.
To add automatic layout:

1. Implement a simple force-directed or hierarchical layout algorithm in `ui.c`
2. Compute positions from the `Link` adjacency list on `:load`
3. Store computed positions back in `Device.ui_row / ui_col` for saving

### Adding NETCONF/RESTCONF

1. Use **libcurl** for RESTCONF HTTP calls
2. Parse responses with a lightweight JSON library (e.g. **cJSON** — single `.c` file)
3. Add `:get-config <ID>` and `:edit-config <ID>` commands

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `Error: could not open /dev/ttyUSB0` | Check cable, run `ls /dev/ttyUSB*`, verify dialout group |
| Garbage characters on console | Baud rate mismatch — use `:set baud 115200` then reconnect |
| Screen garbled after resize | Terminal resize signal handled; if persistent, run `reset` |
| Config push drops lines | Increase delay in `modes.c` `push_config_file()` (currently 80ms) |
| ncurses link error | Install `libncurses5-dev`: `sudo apt-get install libncurses5-dev` |
