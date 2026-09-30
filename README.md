# TrpShare

A small C++17 LAN file transfer server with a phone friendly web interface and an ncurses terminal dashboard.

## Build

On macOS, install the Xcode Command Line Tools if a C++ compiler or curses headers are missing. On Debian or Ubuntu, install ncurses development files with `sudo apt install libncurses-dev`.

```sh
make
./trpshare --share ./shared --port 8080
```

Open the printed `http://<laptop-lan-ip>:8080` address on a phone connected to the same Wi-Fi or hotspot. The server binds to all IPv4 interfaces. Allow the port through your laptop firewall if needed. In the terminal dashboard, press `q` to stop.

Run `make clean` to remove object files, or `make fclean` to remove the executable too.

## Project layout

```text
include/trpshare/  public headers
src/               server, HTTP, file service, TUI, and entry point
web/index.html     phone web interface
shared/            default shared directory
```

## Features

- `GET /` serves the single page file manager.
- `GET /api/files` lists files and folders recursively.
- `GET /files/<path>` downloads nested files as a stream.
- `POST /api/upload?name=<filename>&path=<folder>` uploads raw file bytes into the selected folder.
- The TUI shows server status, local addresses, request count, latest request, and client address.

Uploads are capped at 2 GiB. Filenames and folder paths are restricted to safe path components, and symbolic links are excluded from browsing and transfer. This is intended for a trusted local network; do not expose it to the public internet.

The first version handles one request at a time and closes each connection after its response. The HTTP request parser supports `Content-Length` request bodies only (no chunked transfer encoding or multipart parsing).
