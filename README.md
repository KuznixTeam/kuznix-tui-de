# kuznix-tui-de: TUI Desktop Environment

## Requirements

- C++17 or newer
- ncurses
- Meson build system

## Build & Install

```sh
meson setup build
ninja -C build
sudo ninja -C build install
```

## Usage

- Run `kuznix-tui-de` from a TTY.
- Navigate with arrow keys, ENTER to launch, `Ctrl+F` to filter, `q` to quit.

## Features

- Lists all binaries in `/bin`, `/usr/local/bin`, `/usr/local/sbin`, `/opt/*/bin`, `/opt/*/sbin`, and `~/.local/{bin,sbin}`
- Keyboard-driven navigation
- Filter application list with `Ctrl+F`
