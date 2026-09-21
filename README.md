# Mia HID Pordo 12.0

A lightweight GTK3 + hidapi desktop application for interacting with USB HID devices. It parses the device **Report Descriptor**, creates a tab per **Report ID**, and lets you send **Output** reports, **Set/Get Feature** reports, and **Read Input** reports — in **HEX** or **ASCII** mode.

> **Mia HID Pordo** = "Bridge to HID" — a small utility to talk to any HID device (mice, keyboards, custom firmware, dev boards, …).

---

## ✨ Features

- 🔍 **Automatic device enumeration** via `hid_enumerate`
- 🧩 **Report Descriptor parser** — computes input/output/feature sizes per Report ID
- 🗂️ **One tab per Report ID** (or a single `Default` tab if the device has no Report IDs)
- 📥 **Read Input** — non-blocking manual read with 300 ms timeout
- 📤 **Send Output** — with automatic Report ID prefix
- ⚙️ **Get Feature / Set Feature** reports
- 🔁 **Background reader thread** that streams incoming Input reports into the active tab
- 🔠 **HEX / ASCII mode toggle** — for both display and input parsing
- 🧵 **Thread-safe** — all HID I/O is serialized with a mutex, UI updates via `g_idle_add`
- 📜 **Log limiting** — keeps the last 500 lines per tab to stay responsive
- 🧹 **Clear logs** and refresh buttons

---

## 📸 Overview

```
┌───────────────────────────────────────────────────────────┐
│  [⟳]  [ Device combo ▾ ]  [Connect]  [HEX]  [Clear]       │
├───────────────────────────────────────────────────────────┤
│  ┌─ Report ID:0x01 ─┬─ Report ID:0x02 ─┬─ ... ─┐          │
│  │  [IN] RECV (4 bytes): 01 02 03 04           │          │
│  │  [OUT] SENT (4 bytes): 05 06 07 08          │          │
│  │  [FEAT] GET (2 bytes): AA BB                │          │
│  │                                             │          │
│  │  [ hex/text entry........ ] [Send] [Read]   │          │
│  │  [ feature entry......... ] [Set] [Get]     │          │
│  └─────────────────────────────────────────────┘          │
├───────────────────────────────────────────────────────────┤
│  Connected. Descriptor: 212 bytes, 3 tab(s)               │
└───────────────────────────────────────────────────────────┘
```

---

## 🛠️ Requirements

| Dependency | Version |
|------------|---------|
| GTK+       | 3.24 or newer |
| hidapi     | any recent version (`hidapi-hidraw` or `hidapi-libusb`) |
| GLib       | 2.50+ (for `g_atomic_int_*`, `GThread`) |
| C compiler | GCC / Clang with C99 support |

### Install dependencies

**Debian / Ubuntu / Mint**
```bash
sudo apt install build-essential pkg-config libgtk-3-dev libhidapi-dev
```

**Fedora / RHEL**
```bash
sudo dnf install gcc pkg-config gtk3-devel hidapi-devel
```

**Arch / Manjaro**
```bash
sudo pacman -S base-devel pkgconf gtk3 hidapi
```

**macOS (Homebrew)**
```bash
brew install gtk+3 hidapi pkg-config
```

**Windows (MSYS2)**
```bash
pacman -S mingw-w64-x86_64-gtk3 mingw-w64-x86_64-hidapi mingw-w64-x86_64-gcc pkg-config
```

---

## 🔨 Build

```bash
gcc -O2 -Wall -Wextra -pthread main.c -o mia_hid_pordo \
    $(pkg-config --cflags --libs gtk+-3.0 hidapi-hidraw) \
    $(pkg-config --cflags --libs gthread-2.0)
```

> On some distros the pkg-config name is `hidapi-libusb` or just `hidapi` instead of `hidapi-hidraw`. Try one of:
> ```bash
> pkg-config --list-all | grep -i hid
> ```


## 🚀 Usage

1. **Select a device** from the combo box (click 🔄 to refresh).
2. Click **Connect**.
   - The Report Descriptor is read and parsed.
   - One tab is created per detected Report ID (or a single `Default` tab).
3. In the active tab you get:
   - **Read** — wait up to 300 ms for an Input report.
   - **Send** — send an Output report.
   - **Set Feature / Get Feature** — feature-report round trips.
   - A live log of every IN / OUT / FEAT / READ event.
4. Toggle **HEX** to switch between:
   - **ASCII mode** — the entry text is sent byte-for-byte, printable bytes shown as-is.
   - **HEX mode** — parse `01 02 FF`, `01,02,FF`, `01:02:FF`, or `0x01 0x02` and display bytes as hex.

---

## 🔐 Permissions (Linux)

To access HID devices as a normal user, add a udev rule. Example for a device with VID `1234` and PID `5678`:

```bash
sudo tee /etc/udev/rules.d/99-hid-pordo.rules >/dev/null <<'EOF'
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1234", ATTRS{idProduct}=="5678", MODE="0666"
# or, to allow a specific group:
# SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1234", MODE="0660", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then unplug / replug the device. Alternatively, run with `sudo` (not recommended).

> ⚠️ **Warning:** Writing to arbitrary HID devices can brick firmware or interfere with the system keyboard/mouse. Use with care — especially with `hid_write` on your own input devices.

---

## 📂 Project Structure

```
.
├── main.c            # Full application (GTK3 UI + hidapi logic)
├── window1.glade     # GTK Builder UI definition
├── README.md
└── Makefile          # (optional)
```

### Key components in `main.c`

| Section | Purpose |
|--------|---------|
| `parse_report_descriptor` | Walks HID Report Descriptor items, keeps a global-item stack (Push/Pop), and computes total input/output/feature bits per Report ID. |
| `ReportBOX` | Per-Report-ID UI state (terminal buffer, entries, sizes, log count). |
| `App` | Global application state: device handle, mutex, tabs, reader thread, generation counter. |
| `sync_op_thread` / `sync_op_done` | Runs Output / Feature-Set / Feature-Get on a worker thread; result delivered on the GTK main loop. |
| `manual_read_thread` | Non-blocking read loop for the "Read" button (300 ms total). |
| `reader_thread_func` | Always-on non-blocking poller that streams Input reports to the correct tab. |
| `tabs_generation` | Atomic counter used to invalidate callbacks after a disconnect / reconnect. |

---

## ⚠️ Known Limitations

- **Non-blocking reads only** — the reader thread polls every 1 ms. On very high-throughput devices this may drop reports. Switch to a blocking `hid_read_timeout` if you need lossless capture.
- **No report-descriptor caching** — every connect re-reads it.
- **Single device at a time** — one open HID handle per application instance.
- **GTK3 only** — not yet ported to GTK4.

---

## 🤝 Contributing

Pull requests are welcome!

1. Fork the repo.
2. Create a topic branch: `git checkout -b feature/my-feature`.
3. Keep the existing code style (C99, 4-space indent, `g_*` GLib idioms).
4. Open a PR with a clear description of the change.

Bug reports should include:
- OS + version
- `lsusb -v` output for the device (redact serial numbers if needed)
- The Report Descriptor dump (you can grab it from the app log)
- Steps to reproduce

---

## 📜 License

This project is licensed under the **GNU General Public License v3.0**.

```
Mia HID Pordo — GTK3 + hidapi HID utility
Copyright (C) 2025  <your name>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```

See the [`LICENSE`](LICENSE) file for the full text.

---

## 🙏 Acknowledgements

- [GTK](https://www.gtk.org/) — the UI toolkit
- [hidapi](https://github.com/libusb/hidapi) — cross-platform HID access
- The HID 1.11 specification for the Report Descriptor format

---

## 📝 Version

**Mia HID Pordo 10.0** — see `main.c` for the current implementation.
