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
web/               phone file manager and chunk upload client
shared/            default shared directory
```

## Features

- `GET /` serves the single page file manager.
- `GET /api/files` lists files and folders recursively.
- `GET /files/<path>` downloads nested files as a stream.
- `POST /api/uploads?name=<filename>&path=<folder>&size=<bytes>` starts a chunk upload session. Optional `id=<32 hex characters>` makes starting a session retryable.
- `PUT /api/uploads/<id>?offset=<bytes>` sends up to 8 MiB of raw file bytes at the acknowledged offset.
- `GET /api/uploads/<id>` returns file size, next offset, chunk size, and completion status.
- `POST /api/uploads/<id>/complete` publishes the finished file after checking its size.
- `DELETE /api/uploads/<id>` cancels an unfinished upload and removes its temporary file.
- The TUI shows server status, local addresses, request count, latest request, and client address.

Uploads no longer have a 2 GiB file cap. The browser slices files into 8 MiB chunks and shows progress based on acknowledged bytes. The server writes chunks using 64-bit offsets; only the per-request body is capped at 8 MiB. Available disk space and filesystem limits determine practical maximum file size.

Failed chunks are retried automatically. After interruption or a page refresh, select the **same file in the same destination folder** to resume. Sessions live in server memory, so resuming requires the server to remain running. Normal shutdown removes unfinished temporary files. A crash may leave hidden `.trpshare-upload-*` files, which can be removed manually. No separate client binary is needed.

 Filenames and folder paths are restricted to safe path components, and symbolic links are excluded from browsing and transfer. This is intended for a trusted local network; do not expose it to the public internet.

The first version handles one request at a time and closes each connection after its response. The HTTP request parser supports `Content-Length` request bodies only (no chunked transfer encoding or multipart parsing).

## Verification

```sh
make test        # Python protocol tests and Node browser uploader tests
make test-large  # Also uploads a real 2 GiB + 17 byte file, then removes it
```

The tests check binary assembly, nested downloads, interrupted chunks, duplicate retries, lost responses, empty files, cancellation, and path validation.
