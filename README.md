# Mia HID Pordo

A GTK3 application for inspecting and communicating with USB HID devices via `hidapi`, with configuration stored through `libconfig`.

- [English](#english)
- [Esperanto](#esperanto)
- [Azərbaycan Türkcəsi](#azərbaycan-türkcəsi)

---

## English

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

---

## Esperanto

### Superrigardo
`TARGET` estas GTK3-a aplikaĵo por inspekti kaj komuniki kun USB HID-aparatoj per `hidapi`. Ĝi listigas HID-aparatojn, malfermas la elektitan aparaton, legas la HID Report Descriptor, analizas ĝin, kaj kreas langeton en la kajero por ĉiu Report ID, kiu enhavas Input-, Output- aŭ Feature-raportojn.

### Trajtoj
- Listigas USB HID-aparatojn kaj montras `VID:PID  Fabrikanto Produkto`.
- Konservas stabilan identigilon `VID:PID[:Seria]` por restarigi la lastan aparaton.
- Malfermas aparaton per vojo per `hid_open_path`.
- Legas la HID Report Descriptor per `hid_get_report_descriptor`.
- Analizas Report Size, Report Count, Report ID, Push/Pop kaj Input/Output/Feature-elementojn.
- Kreas langetojn por ĉiu Report ID kun bajtaj grandecoj por Input, Output kaj Feature.
- Sendas Output-raportojn kun avertoj pri aldonado/tranĉado.
- Legas Input-raportojn mane per `hid_get_input_report`.
- Fona leganta fadeno por Input-raportoj, se la aparato havas Input-raportojn.
- Get kaj Set por Feature-raportoj.
- HEX- kaj ASCII-reĝimoj por montrado kaj enigo.
- Protokolo por ĉiu langeto kun elstarigo de metadatenoj kaj maksimume 500 linioj.
- Montras analizitan HID Report Descriptor en la aktiva langeto.
- Aŭtomata konekto ĉe lanĉo.
- Konservas kaj restarigas agordon per `libconfig`.

### Dependecoj
- GTK+ 3
- hidapi
- libconfig
- GLib / GIO
- GCC aŭ Clang
- make
- pkg-config
- glib-compile-resources

Sur Debian/Ubuntu-similaj sistemoj:
```sh
sudo apt install build-essential pkg-config libgtk-3-dev libhidapi-dev libconfig-dev libglib2.0-dev
```
Pakaĵnomoj povas malsami. Sur iuj sistemoj uzu `libhidapi-libusb1-dev` aŭ `libhidapi-hidraw0-dev`.

### Konstruado
La fonto atendas du kompiltempajn makroojn:
- `TARGET` – uzata por GResource-vojoj kaj agorda dosiernomo.
- `WINTITLE` – titolo de la fenestro.

Ekzemplo kun Makefile:
```sh
make TARGET=hidtool WINTITLE="HID Tool"
```

Manuala konstruo:
```sh
glib-compile-resources resources/resources.gresource.xml \
  --target=resources.c --generate-source

gcc -O2 -Wall -o hidtool main.c resources.c \
  -DTARGET='"hidtool"' -DWINTITLE='"HID Tool"' \
  $(pkg-config --cflags --libs gtk+-3.0 hidapi-libusb libconfig glib-2.0)
```
Anstataŭigu `hidapi-libusb` per `hidapi-hidraw` aŭ `hidapi` laŭ via platformo.

### Uzado
1. Lanĉu la programon.
2. Elektu HID-aparaton el la falmenuo.
3. Alklaku **Connect**.
4. La Report Descriptor estas legata kaj langetoj kreiĝas por ĉiu Report ID.
5. Uzu la kontrolojn:
   - **Send** – sendi Output-raporton.
   - **Read** – mane legi Input-raporton.
   - **Set Feature** – sendi Feature-raporton.
   - **Get Feature** – legi Feature-raporton.
6. Ŝaltu **HEX** por alterni inter deksesumaj bajtoj kaj ASCII-teksto.
7. Alklaku **Show Descriptor** por protokoli la analizitan HID Report Descriptor.
8. Alklaku **Refresh** por relisti HID-aparatojn.
9. Ŝaltu **Auto** por aŭtomate konekti ĉe lanĉo.
10. Alklaku **Clear** por malplenigi la protokolojn en ĉiuj langetoj.

### Agordo
La agorda dosiero troviĝas ĉe:
```text
~/.config/<TARGET>.cfg
```
Ĝi uzas la formaton de libconfig. Subtenataj ŝlosiloj:
- `hex_mode` – `true`/`false`
- `autoconnect` – `true`/`false`
- `window_width` – entjero
- `window_height` – entjero
- `window_x` – entjero
- `window_y` – entjero
- `last_device` – signoĉeno, ekz. `1234:5678:SN123`

### Notoj
- Sur Linux, aliro al HID-aparatoj povas postuli udev-regulojn aŭ plialtigitajn permesojn.
- Aparatoj kun nur Output aŭ Feature-raportoj estas subtenataj; la fona leganto ne startas por ili.
- La leganta fadeno uzas neblokantan `hid_read` kaj sendas datumojn al la ĉefa fadeno de GTK per `g_idle_add`.
- Manuala Input-legado uzas `hid_get_input_report` kaj traktas la unuan redonitan bajton kiel Report ID.
- Report ID `0` estas uzata kiel defaŭlta raporto, kiam la priskribo ne deklaras Report ID.

---

## Azərbaycan Türkcəsi

### Ümumi baxış
`TARGET` — `hidapi` vasitəsilə USB HID cihazlarını yoxlamaq və onlarla əlaqə saxlamaq üçün GTK3 tətbiqidir. O, HID cihazlarını sadalayır, seçilmiş cihazı açır, HID Report Deskriptorunu oxuyur, təhlil edir və Input, Output və ya Feature hesabatları olan hər Report ID üçün notebook-da tab yaradır.

### Xüsusiyyətlər
- USB HID cihazlarını sadalayır və `VID:PID  İstehsalçı Məhsul` göstərir.
- Son cihazı bərpa etmək üçün sabit `VID:PID[:Serial]` identifikatorunu saxlayır.
- Cihazı `hid_open_path` ilə yol vasitəsilə açır.
- HID Report Deskriptorunu `hid_get_report_descriptor` ilə oxuyur.
- Report Size, Report Count, Report ID, Push/Pop və Input/Output/Feature elementlərini təhlil edir.
- Input, Output və Feature üçün bayt ölçüləri ilə hər Report ID üçün tab yaradır.
- Output hesabatlarını doldurma/kəsmə xəbərdarlıqları ilə göndərir.
- Input hesabatlarını `hid_get_input_report` ilə əl ilə oxuyur.
- Cihazda Input hesabatları varsa, Input üçün fon oxuyucu thread işə salınır.
- Feature hesabatları üçün Get və Set.
- Göstərmə və giriş üçün HEX və ASCII rejimləri.
- Hər tab üçün metadata vurğulaması və maksimum 500 sətir loq.
- Aktiv tabda təhlil edilmiş HID Report Deskriptorunu göstərir.
- Başlanğıcda avtomatik qoşulma seçimi.
- `libconfig` ilə konfiqurasiyanı saxlayır və bərpa edir.

### Asılılıqlar
- GTK+ 3
- hidapi
- libconfig
- GLib / GIO
- GCC və ya Clang
- make
- pkg-config
- glib-compile-resources

Debian/Ubuntu tipli sistemlərdə:
```sh
sudo apt install build-essential pkg-config libgtk-3-dev libhidapi-dev libconfig-dev libglib2.0-dev
```
Paket adları fərqli ola bilər. Bəzi sistemlərdə `libhidapi-libusb1-dev` və ya `libhidapi-hidraw0-dev` istifadə edin.

### Qurma / Kompilyasiya
Mənbə iki kompilyasiya vaxtı makrosu gözləyir:
- `TARGET` – GResource yolları və konfiqurasiya fayl adı üçün.
- `WINTITLE` – pəncərə başlığı.

Makefile ilə nümunə:
```sh
make TARGET=hidtool WINTITLE="HID Tool"
```

Əl ilə qurma nümunəsi:
```sh
glib-compile-resources resources/resources.gresource.xml \
  --target=resources.c --generate-source

gcc -O2 -Wall -o hidtool main.c resources.c \
  -DTARGET='"hidtool"' -DWINTITLE='"HID Tool"' \
  $(pkg-config --cflags --libs gtk+-3.0 hidapi-libusb libconfig glib-2.0)
```
Platformanıza uyğun olaraq `hidapi-libusb` yerinə `hidapi-hidraw` və ya `hidapi` yazın.

### İstifadə
1. Proqramı işə salın.
2. Açılan siyahıdan HID cihazı seçin.
3. **Connect** düyməsinə basın.
4. Report Deskriptoru oxunur və hər Report ID üçün tab yaradılır.
5. İdarəetmələrdən istifadə edin:
   - **Send** – Output hesabatı göndərir.
   - **Read** – Input hesabatını əl ilə oxuyur.
   - **Set Feature** – Feature hesabatı göndərir.
   - **Get Feature** – Feature hesabatını oxuyur.
6. Hexadecimal baytlar və ASCII mətn arasında keçmək üçün **HEX** işarəsini qoyun.
7. Təhlil edilmiş HID Report Deskriptorunu loqa yazmaq üçün **Show Descriptor** düyməsinə basın.
8. HID cihazlarını yenidən sadalamaq üçün **Refresh** düyməsinə basın.
9. Başlanğıcda avtomatik qoşulmaq üçün **Auto** seçin.
10. Bütün tablardakı loqları təmizləmək üçün **Clear** düyməsinə basın.

### Konfiqurasiya
Konfiqurasiya faylı burada saxlanılır:
```text
~/.config/<TARGET>.cfg
```
libconfig formatından istifadə edir. Dəstəklənən açarlar:
- `hex_mode` – `true`/`false`
- `autoconnect` – `true`/`false`
- `window_width` – tam ədəd
- `window_height` – tam ədəd
- `window_x` – tam ədəd
- `window_y` – tam ədəd
- `last_device` – sətir, məs. `1234:5678:SN123`

### Qeydlər
- Linux-da HID cihazlarına giriş üçün udev qaydaları və ya yüksək icazələr tələb oluna bilər.
- Yalnız Output və ya Feature hesabatları olan cihazlar dəstəklənir; onlar üçün fon oxuyucusu işə salınmır.
- Oxuyucu thread bloklamayan `hid_read` istifadə edir və məlumatı `g_idle_add` ilə GTK əsas thread-inə göndərir.
- Əl ilə Input oxuma `hid_get_input_report` istifadə edir və qaytarılan ilk baytı Report ID kimi qəbul edir.
- Deskriptorda Report ID elan edilmədikdə, Report ID `0` defolt hesabat kimi istifadə olunur.
