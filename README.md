# HID Tool (TARGET)

A GTK3 application for inspecting and communicating with USB HID devices via `hidapi`, with configuration stored through `libconfig`.

### Overview
`TARGET` is a GTK3 application for inspecting and communicating with USB HID devices through `hidapi`. It enumerates HID devices, opens the selected device, reads the HID Report Descriptor, parses it, and creates one notebook tab for each Report ID that contains Input, Output, or Feature reports.

### Features
- Enumerate USB HID devices and display `VID:PID  Manufacturer Product`.
- Store a stable device identifier `VID:PID[:Serial]` for restoring the last device.
- Open a device by path with `hid_open_path`.
- Read the HID Report Descriptor with `hid_get_report_descriptor`.
- Parse Report Size, Report Count, Report ID, Push/Pop, and Input/Output/Feature items.
- Create tabs per Report ID with byte sizes for Input, Output, and Feature reports.
- Send Output reports with automatic padding/truncation warnings.
- Read Input reports manually with `hid_get_input_report`.
- Background reader thread for Input reports when the device has Input reports.
- Get and Set Feature reports.
- HEX and ASCII modes for display and input.
- Per-tab log with metadata highlighting and a maximum of 500 lines.
- Show parsed HID Report Descriptor in the active tab.
- Auto-connect option on startup.
- Save and restore configuration with `libconfig`.

### Dependencies
- GTK+ 3
- hidapi
- libconfig
- GLib / GIO
- GCC or Clang
- make
- pkg-config
- glib-compile-resources

On Debian/Ubuntu-like systems:
```sh
sudo apt install build-essential pkg-config libgtk-3-dev libhidapi-dev libconfig-dev libglib2.0-dev
```
Package names may differ. On some systems use `libhidapi-libusb1-dev` or `libhidapi-hidraw0-dev`.

### Build
The source expects two compile-time macros:
- `TARGET` – used for GResource paths and config file name.
- `WINTITLE` – window title.

Example with a Makefile:
```sh
make TARGET=hidtool WINTITLE="HID Tool"
```

Manual build example:
```sh
glib-compile-resources resources/resources.gresource.xml \
  --target=resources.c --generate-source

gcc -O2 -Wall -o hidtool main.c resources.c \
  -DTARGET='"hidtool"' -DWINTITLE='"HID Tool"' \
  $(pkg-config --cflags --libs gtk+-3.0 hidapi-libusb libconfig glib-2.0)
```
Change `hidapi-libusb` to `hidapi-hidraw` or `hidapi` depending on your platform.

### Usage
1. Run the program.
2. Select a HID device from the drop-down list.
3. Click **Connect**.
4. The Report Descriptor is read and tabs are created for each Report ID.
5. Use the controls:
   - **Send** – send an Output report.
   - **Read** – manually read an Input report.
   - **Set Feature** – send a Feature report.
   - **Get Feature** – read a Feature report.
6. Toggle **HEX** to switch between hexadecimal bytes and ASCII text.
7. Click **Show Descriptor** to log the parsed HID Report Descriptor.
8. Click **Refresh** to re-enumerate HID devices.
9. Enable **Auto** to connect automatically on startup.
10. Click **Clear** to clear the logs in all tabs.

### Configuration
The configuration file is stored at:
```text
~/.config/<TARGET>.cfg
```
It uses the libconfig format. Supported keys:
- `hex_mode` – `true`/`false`
- `autoconnect` – `true`/`false`
- `window_width` – integer
- `window_height` – integer
- `window_x` – integer
- `window_y` – integer
- `last_device` – string, e.g. `1234:5678:SN123`

### Notes
- On Linux, access to HID devices may require udev rules or elevated permissions.
- Devices with only Output or Feature reports are supported; the background reader is not started for them.
- The reader thread uses non-blocking `hid_read` and sends data to the GTK main thread with `g_idle_add`.
- Manual Input read uses `hid_get_input_report` and treats the first returned byte as the Report ID.
- Report ID `0` is used as the default report when the descriptor does not declare Report IDs.
