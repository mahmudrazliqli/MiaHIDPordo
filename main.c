#include <gtk/gtk.h>
#include <hidapi/hidapi.h>
#include <libconfig.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <wchar.h>
#include <locale.h>
#include <sys/stat.h>

#define MAX_DESCRIPTOR_SIZE 4096
#define MAX_DEVICES 128
#define MAX_REPORT_ID 256
#define MAX_LOG_LINES 500
#define HEX_BUF_SIZE 4096
#define MAX_REPORT_BYTES 1024
#define MAX_READ_BUF (MAX_REPORT_BYTES + 1)
#define MAX_GLOBAL_STACK 4
#define READER_POLL_SLEEP_US 1000     // 1ms
#define MANUAL_READ_TIMEOUT_MS 300    // کل زمان انتظار manual read */
#define MANUAL_READ_STEP_MS 5         // هر گام، mutex آزاد می‌شود */

#define BITS_TO_BYTES(b) ((unsigned int)(((unsigned long long)(b) + 7ULL) / 8ULL))

#define CFG_DIR    ".config"
#define CFG_NAME   "miahidpordo.cfg"

#ifndef MIAHIDPORDO_DATA_DIR
#define MIAHIDPORDO_DATA_DIR "."
#endif

/* ---------- ساختارها ---------- */

typedef struct {
    unsigned int input_bytes;
    unsigned int output_bytes;
    unsigned int feature_bytes;
    unsigned int input_bits;
    unsigned int output_bits;
    unsigned int feature_bits;
} ReportSizes;

typedef struct {
    GtkWidget *input_view;
    GtkTextBuffer *input_buf;
    GtkWidget *output_entry;
    GtkWidget *feature_entry;
    unsigned char report_id;
    int has_report_id;
    ReportSizes sizes;
    int log_lines;
} ReportBOX;

typedef struct {
    GtkWidget *notebook, *status_label, *device_combo, *window, *hex_check;
    GtkWidget *connect_button, *refresh_button, *clear_button;
    GtkWidget *auto_check;                
    GPtrArray *device_paths;
    GPtrArray *device_ids;                 //  VID:PID[:Serial] 

    hid_device *dev;
    GMutex hid_mutex;
    gint connected;
    gint has_report_id;
    ReportSizes main_sizes[MAX_REPORT_ID];

    ReportBOX *tabs[MAX_REPORT_ID];
    int tab_count;
    gint tabs_generation;

    GThread *reader_thread;
    gint reader_running;
    gint shutting_down;
    gint hex_mode;

    gint manual_reads_active;
} App;

static App app;

typedef struct {
    unsigned char data[MAX_READ_BUF];
    int len;
    int has_report_id;
    gint generation;
} ReadEvent;

/* ---------- forward declarations ---------- */

static gpointer reader_thread_func(gpointer data);
static void stop_reader_thread(void);
static void clear_tabs(void);
static void connect_device(void);

/* ---------- توابع کمکی ---------- */

static int is_hex_mode(void) {
    return g_atomic_int_get(&app.hex_mode);
}

static const char *hid_err_str_locked(hid_device *dev) {
    static char errbuf[256];
    const wchar_t *err = hid_error(dev);
    if (!err) {
        snprintf(errbuf, sizeof(errbuf), "%s", "unknown error");
        return errbuf;
    }
    size_t n = wcstombs(errbuf, err, sizeof(errbuf) - 1);
    if (n == (size_t)-1)
        snprintf(errbuf, sizeof(errbuf), "%s", "(error conversion failed)");
    else
        errbuf[n] = '\0';
    return errbuf;
}

static void wchar_to_locale(const wchar_t *w, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (!w || !*w) { snprintf(out, out_size, "-"); return; }
    size_t n = wcstombs(out, w, out_size - 1);
    if (n == (size_t)-1) snprintf(out, out_size, "(?)");
    else out[n] = '\0';
}

static void format_bytes(const unsigned char *buf, int len, char *out, size_t out_size) {
    if (len <= 0 || out_size == 0) { out[0] = '\0'; return; }

    if (is_hex_mode()) {
        static const char hx[] = "0123456789ABCDEF";
        size_t pos = 0;
        for (int i = 0; i < len; i++) {
            if (pos + 4 > out_size) break;
            out[pos++] = hx[buf[i] >> 4];
            out[pos++] = hx[buf[i] & 0xF];
            out[pos++] = ' ';
        }
        if (pos > 0) pos--;
        out[pos] = '\0';
    } else {
        size_t pos = 0;
        for (int i = 0; i < len && pos + 1 < out_size; i++) {
            unsigned char c = buf[i];
            out[pos++] = isprint((unsigned char)c) ? (char)c : '.';
        }
        out[pos] = '\0';
    }
}

static int parse_input(const char *str, unsigned char *out, int max_len) {
    if (is_hex_mode()) {
        int count = 0;
        const char *p = str;
        while (*p && count < max_len) {
            while (*p == ' ' || *p == '\t' || *p == ',' || *p == ':' ||
                   *p == '\n' || *p == '\r')
                p++;
            if (!*p) break;

            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

            unsigned int val = 0;
            int digits = 0;
            while (digits < 2 && isxdigit((unsigned char)*p)) {
                int c = (unsigned char)*p;
                int d;
                if (c >= '0' && c <= '9')      d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else                            d = c - 'A' + 10;
                val = val * 16 + d;
                p++;
                digits++;
            }
            if (digits == 0) break;
            out[count++] = (unsigned char)val;
        }
        return count;
    } else {
        int len = (int)strlen(str);
        if (len > max_len) len = max_len;
        if (len > 0) memcpy(out, str, len);
        return len;
    }
}

static void log_to_tab(ReportBOX *tab, const char *prefix, const char *text) {
    if (!tab || !tab->input_buf) return;
    GtkTextBuffer *buf = tab->input_buf;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);

    char line[HEX_BUF_SIZE + 256];
    int len = snprintf(line, sizeof(line), "[%s] %s\n", prefix, text);
    if (len < 0) len = 0;
    if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
    gtk_text_buffer_insert(buf, &end, line, len);

    tab->log_lines++;

    while (tab->log_lines > MAX_LOG_LINES) {
        GtkTextIter start, cut;
        gtk_text_buffer_get_start_iter(buf, &start);
        cut = start;
        if (!gtk_text_iter_forward_line(&cut)) break;
        gtk_text_buffer_delete(buf, &start, &cut);
        tab->log_lines--;
    }

    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_place_cursor(buf, &end);

    if (tab->input_view) {
        GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->input_view),
                                     mark, 0.0, FALSE, 0, 0);
        gtk_text_buffer_delete_mark(buf, mark);
    }
}

/* ---------- settings (libconfig) ---------- */

static char *cfg_path = NULL;
static char *pending_last_device = NULL;

static const char *get_config_path(void) {
    if (cfg_path) return cfg_path;
    const char *home = g_get_home_dir();
    if (!home || !*home) home = ".";
    char *dir = g_build_filename(home, CFG_DIR, NULL);
    g_mkdir_with_parents(dir, 0755);
    cfg_path = g_build_filename(dir, CFG_NAME, NULL);
    g_free(dir);
    return cfg_path;
}

static void apply_pending_device(void) {
    int target = -1;
    if (pending_last_device && *pending_last_device && app.device_ids) {
        for (int i = 0; i < (int)app.device_ids->len; i++) {
            const char *id = g_ptr_array_index(app.device_ids, i);
            if (id && strcmp(id, pending_last_device) == 0) { target = i; break; }
        }
    }
    if (target < 0 && app.device_ids && app.device_ids->len > 0) target = 0;
    if (target >= 0 && app.device_combo)
        gtk_combo_box_set_active(GTK_COMBO_BOX(app.device_combo), target);
}

static void load_config(void) {
    config_t cfg;
    config_init(&cfg);

    if (!config_read_file(&cfg, get_config_path())) {
        config_destroy(&cfg);
        return;
    }

    int b = 0;
    int w = 0, h = 0, x = -1, y = -1;

    if (config_lookup_bool(&cfg, "hex_mode", &b) && app.hex_check)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.hex_check), b ? TRUE : FALSE);

    if (config_lookup_bool(&cfg, "autoconnect", &b) && app.auto_check)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.auto_check), b ? TRUE : FALSE);

    int has_w = config_lookup_int(&cfg, "window_width",  &w);
    int has_h = config_lookup_int(&cfg, "window_height", &h);
    int has_x = config_lookup_int(&cfg, "window_x",      &x);
    int has_y = config_lookup_int(&cfg, "window_y",      &y);

    if (app.window) {
        if (has_w && has_h && w > 0 && h > 0)
            gtk_window_resize(GTK_WINDOW(app.window), w, h);
        if (has_x && has_y && x >= 0 && y >= 0)
            gtk_window_move(GTK_WINDOW(app.window), x, y);
    }

    const char *last_dev = NULL;
    if (config_lookup_string(&cfg, "last_device", &last_dev) && last_dev && *last_dev) {
        g_free(pending_last_device);
        pending_last_device = g_strdup(last_dev);
    }

    config_destroy(&cfg);
}

static void save_config(void) {
    config_t cfg;
    config_init(&cfg);
    config_setting_t *root = config_root_setting(&cfg);
    config_setting_t *s;

    s = config_setting_add(root, "hex_mode", CONFIG_TYPE_BOOL);
    config_setting_set_bool(s, is_hex_mode() ? 1 : 0);

    int auto_on = 0;
    if (app.auto_check)
        auto_on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.auto_check)) ? 1 : 0;
    s = config_setting_add(root, "autoconnect", CONFIG_TYPE_BOOL);
    config_setting_set_bool(s, auto_on);

    int w = 0, h = 0, x = -1, y = -1;
    if (app.window) {
        gtk_window_get_size(GTK_WINDOW(app.window), &w, &h);
        gtk_window_get_position(GTK_WINDOW(app.window), &x, &y);
    }
    s = config_setting_add(root, "window_width",  CONFIG_TYPE_INT); config_setting_set_int(s, w);
    s = config_setting_add(root, "window_height", CONFIG_TYPE_INT); config_setting_set_int(s, h);
    s = config_setting_add(root, "window_x",      CONFIG_TYPE_INT); config_setting_set_int(s, x);
    s = config_setting_add(root, "window_y",      CONFIG_TYPE_INT); config_setting_set_int(s, y);

    const char *dev_id = "";
    if (app.device_combo && app.device_ids) {
        int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app.device_combo));
        if (sel >= 0 && sel < (int)app.device_ids->len) {
            const char *id = g_ptr_array_index(app.device_ids, sel);
            if (id) dev_id = id;
        }
    }
    s = config_setting_add(root, "last_device", CONFIG_TYPE_STRING);
    config_setting_set_string(s, dev_id);

    config_write_file(&cfg, get_config_path());
    config_destroy(&cfg);
}

/* ---------- parsing Report Descriptor ---------- */

typedef struct {
    unsigned int report_size;
    unsigned int report_count;
    unsigned char report_id;
} GlobalState;

static void parse_report_descriptor(const unsigned char *desc, int len, ReportSizes *out_sizes, int *has_report_id) {
    GlobalState cur = {0, 0, 0};
    GlobalState stack[MAX_GLOBAL_STACK];
    int sp = 0;

    *has_report_id = 0;
    memset(out_sizes, 0, sizeof(ReportSizes) * MAX_REPORT_ID);

    for (int i = 0; i < len; ) {
        unsigned char prefix = desc[i++];

        if (prefix == 0xFE) {
            if (i + 1 >= len) break;
            int dlen = desc[i];
            if (i + 2 + dlen > len) break;
            i += 2 + dlen;
            continue;
        }

        int size_code = prefix & 3;
        int type = (prefix >> 2) & 3;
        int tag = (prefix >> 4) & 0xF;
        int data_size = (size_code == 3) ? 4 : size_code;

        unsigned int data = 0;
        for (int b = 0; b < data_size && i < len; b++)
            data |= ((unsigned int)desc[i++]) << (8 * b);

        if (type == 1) {
            switch (tag) {
                case 0x7: cur.report_size  = data; break;
                case 0x9: cur.report_count = data; break;
                case 0x8: cur.report_id    = (unsigned char)(data & 0xFF);
                          *has_report_id   = 1; break;
                case 0xA: if (sp < MAX_GLOBAL_STACK) stack[sp++] = cur; break;
                case 0xB: if (sp > 0) cur = stack[--sp]; break;
                default: break;
            }
        } else if (type == 0) {
            unsigned long long n =
                (unsigned long long)cur.report_size * (unsigned long long)cur.report_count;
            if (n > 0xFFFFFFFFULL) n = 0xFFFFFFFFULL;

            unsigned int *field = NULL;
            if      (tag == 0x8) field = &out_sizes[cur.report_id].input_bits;
            else if (tag == 0x9) field = &out_sizes[cur.report_id].output_bits;
            else if (tag == 0xB) field = &out_sizes[cur.report_id].feature_bits;

            if (field) {
                unsigned long long tot = (unsigned long long)*field + n;
                *field = (tot > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (unsigned int)tot;
            }
        }
    }

    for (int id = 0; id < MAX_REPORT_ID; id++) {
        out_sizes[id].input_bytes   = BITS_TO_BYTES(out_sizes[id].input_bits);
        out_sizes[id].output_bytes  = BITS_TO_BYTES(out_sizes[id].output_bits);
        out_sizes[id].feature_bytes = BITS_TO_BYTES(out_sizes[id].feature_bits);
    }
}

/* ---------- مسیردهی داده‌ی ورودی ---------- */

static void route_input_data(const unsigned char *buf, int n, int has_report_id, const char *prefix) {
    char msg[HEX_BUF_SIZE + 256];
    char disp[HEX_BUF_SIZE];
    int offset = 0;
    unsigned char rid = 0;

    if (has_report_id && n > 0) {
        rid = buf[0];
        offset = 1;
    }
    int payload_len = n - offset;
    if (payload_len <= 0) return;

    format_bytes(buf + offset, payload_len, disp, sizeof(disp));

    if (!has_report_id) {
        ReportBOX *tab = app.tabs[0];
        if (tab) {
            snprintf(msg, sizeof(msg), "RECV (%d bytes): %s", payload_len, disp);
            log_to_tab(tab, prefix, msg);
        }
    } else {
        ReportBOX *tab = app.tabs[rid];
        if (tab) {
            snprintf(msg, sizeof(msg), "RECV (%d bytes): %s", payload_len, disp);
            log_to_tab(tab, prefix, msg);
        } else {
            char status[160];
            snprintf(status, sizeof(status),
                     "Input on unknown Report ID 0x%02X (%d bytes)",
                     rid, payload_len);
            gtk_label_set_text(GTK_LABEL(app.status_label), status);
        }
    }
}

/* ---------- عملیات HID روی thread جدا (Output / Feature Set / Feature Get) ---------- */

enum {
    OP_OUTPUT,
    OP_FEATURE_SET,
    OP_FEATURE_GET
};

typedef struct {
    ReportBOX *tab;
    gint generation;
    int op;

    /* ورودی */
    unsigned char payload[MAX_REPORT_BYTES];
    int payload_len;
    unsigned char report_id;
    int has_report_id;
    int read_len;                 /* فقط برای OP_FEATURE_GET */

    /* خروجی */
    int status;                   /* 0 = OK، -1 = خطا */
    int bytes;                    /* برای write: نوشته‌شده؛ برای get: خوانده‌شده */
    unsigned char resp[MAX_READ_BUF];
    char errmsg[256];
} SyncOp;

static gboolean sync_op_done(gpointer data) {
    SyncOp *op = (SyncOp *)data;

    if (op->generation != g_atomic_int_get(&app.tabs_generation))
        return G_SOURCE_REMOVE;

    ReportBOX *tab = app.tabs[op->tab->report_id];
    if (!tab || tab != op->tab) return G_SOURCE_REMOVE;

    char disp[HEX_BUF_SIZE];
    char msg[HEX_BUF_SIZE + 256];

    if (op->op == OP_OUTPUT) {
        format_bytes(op->payload, op->payload_len, disp, sizeof(disp));
        int expect = op->payload_len + 1;
        if (op->status < 0)
            snprintf(msg, sizeof(msg), "hid_write failed: %s", op->errmsg);
        else if (op->bytes != expect)
            snprintf(msg, sizeof(msg), "hid_write short: %d of %d bytes", op->bytes, expect);
        else
            snprintf(msg, sizeof(msg), "SENT (%d bytes): %s", op->payload_len, disp);
        log_to_tab(tab, "OUT ", msg);

    } else if (op->op == OP_FEATURE_SET) {
        format_bytes(op->payload, op->payload_len, disp, sizeof(disp));
        int expect = op->payload_len + 1;
        if (op->status < 0)
            snprintf(msg, sizeof(msg), "hid_send_feature_report failed: %s", op->errmsg);
        else if (op->bytes != expect)
            snprintf(msg, sizeof(msg), "hid_send_feature_report short: %d of %d bytes",
                     op->bytes, expect);
        else
            snprintf(msg, sizeof(msg), "SET  (%d bytes): %s", op->payload_len, disp);
        log_to_tab(tab, "FEAT", msg);

    } else { /* OP_FEATURE_GET */
        if (op->status < 0) {
            snprintf(msg, sizeof(msg), "hid_get_feature_report failed: %s", op->errmsg);
        } else if (op->bytes == 0) {
            snprintf(msg, sizeof(msg), "RECV (0 bytes)");
        } else {
            int payload_len = op->bytes - 1;
            if (payload_len < 0) payload_len = 0;
            format_bytes(op->resp + 1, payload_len, disp, sizeof(disp));
            snprintf(msg, sizeof(msg), "GET  (%d bytes): %s", payload_len, disp);
        }
        log_to_tab(tab, "FEAT", msg);
    }
    return G_SOURCE_REMOVE;
}

static gpointer sync_op_thread(gpointer data) {
    SyncOp *op = (SyncOp *)data;

    if (op->op == OP_FEATURE_GET) {
        unsigned char out_buf[MAX_READ_BUF];
        memset(out_buf, 0, sizeof(out_buf));
        out_buf[0] = op->has_report_id ? op->report_id : 0x00;

        int total = op->read_len + 1;
        if (total > (int)sizeof(out_buf)) total = sizeof(out_buf);

        g_mutex_lock(&app.hid_mutex);
        hid_device *dev = app.dev;
        if (!dev || !g_atomic_int_get(&app.connected)) {
            op->status = -1;
            snprintf(op->errmsg, sizeof(op->errmsg), "device not available");
        } else {
            int got = hid_get_feature_report(dev, out_buf, total);
            if (got < 0) {
                op->status = -1;
                snprintf(op->errmsg, sizeof(op->errmsg), "%s", hid_err_str_locked(dev));
            } else {
                op->status = 0;
                op->bytes = got;
                if (got > 0) {
                    int cp = got > (int)sizeof(op->resp) ? (int)sizeof(op->resp) : got;
                    memcpy(op->resp, out_buf, cp);
                }
            }
        }
        g_mutex_unlock(&app.hid_mutex);

    } else {
        unsigned char out_buf[MAX_READ_BUF];
        int out_len = 0;
        out_buf[out_len++] = op->has_report_id ? op->report_id : 0x00;
        memcpy(out_buf + out_len, op->payload, op->payload_len);
        out_len += op->payload_len;

        g_mutex_lock(&app.hid_mutex);
        hid_device *dev = app.dev;
        if (!dev || !g_atomic_int_get(&app.connected)) {
            op->status = -1;
            snprintf(op->errmsg, sizeof(op->errmsg), "device not available");
        } else {
            int written;
            if (op->op == OP_OUTPUT)
                written = hid_write(dev, out_buf, out_len);
            else
                written = hid_send_feature_report(dev, out_buf, out_len);

            if (written < 0) {
                op->status = -1;
                snprintf(op->errmsg, sizeof(op->errmsg), "%s", hid_err_str_locked(dev));
            } else {
                op->status = 0;
                op->bytes = written;
            }
        }
        g_mutex_unlock(&app.hid_mutex);
    }

    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, sync_op_done, op, g_free);
    return NULL;
}

static void start_sync_op(SyncOp *op, const char *fail_prefix) {
    GThread *t = g_thread_new("hid_sync", sync_op_thread, op);
    if (!t) {
        log_to_tab(op->tab, fail_prefix, "failed to start sync thread");
        g_free(op);
        return;
    }
    g_thread_unref(t);
}

/* ---------- Send Output ---------- */

static void on_send_output(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!g_atomic_int_get(&app.connected)) {
        log_to_tab(tab, "OUT ", "not connected");
        return;
    }
    if (tab->sizes.output_bytes == 0) {
        log_to_tab(tab, "OUT ", "this report has no Output capability");
        return;
    }

    const char *txt = gtk_entry_get_text(GTK_ENTRY(tab->output_entry));
    unsigned char buf[MAX_REPORT_BYTES];
    int n = parse_input(txt, buf, sizeof(buf));
    if (n == 0) { log_to_tab(tab, "OUT ", "(empty)"); return; }

    if (n > (int)tab->sizes.output_bytes) {
        char warn[160];
        snprintf(warn, sizeof(warn),
                 "WARNING: %d bytes > max %u bytes (sending anyway)",
                 n, tab->sizes.output_bytes);
        log_to_tab(tab, "OUT ", warn);
    }

    SyncOp *op = g_new0(SyncOp, 1);
    op->tab = tab;
    op->generation = g_atomic_int_get(&app.tabs_generation);
    op->op = OP_OUTPUT;
    memcpy(op->payload, buf, n);
    op->payload_len = n;
    op->report_id = tab->report_id;
    op->has_report_id = tab->has_report_id;

    start_sync_op(op, "OUT ");
}

/* ---------- Read Input (manual) ---------- */

typedef struct {
    ReportBOX *tab;
    unsigned char rid;
    int has_rid;
    gint generation;
    unsigned char data[MAX_READ_BUF];
    int len;
    char errmsg[256];
} ManualReadResult;

static gboolean deliver_manual_read(gpointer data) {
    ManualReadResult *r = (ManualReadResult *)data;

    if (r->generation != g_atomic_int_get(&app.tabs_generation))
        return G_SOURCE_REMOVE;
    if (!g_atomic_int_get(&app.connected))
        return G_SOURCE_REMOVE;

    ReportBOX *tab = app.tabs[r->rid];
    if (!tab || tab != r->tab) return G_SOURCE_REMOVE;

    if (r->len > 0) {
        /* 
         * hid_get_input_report همیشه Report ID را در بایت اول برمی‌گرداند،
         * حتی وقتی Report ID واقعی صفر است. پس اینجا has_rid را 1 می‌گذاریم
         * تا route_input_data بایت اول را به عنوان Report ID در نظر بگیرد.
         */
        route_input_data(r->data, r->len, 1 /* has_report_id */, "READ");
    } else if (r->len == 0) {
        log_to_tab(tab, "READ", "timeout (no data)");
    } else {
        char msg[512];
        snprintf(msg, sizeof(msg), "READ failed: %s", r->errmsg);
        log_to_tab(tab, "READ", msg);
    }
    return G_SOURCE_REMOVE;
}

static gpointer manual_read_thread(gpointer data) {
    ManualReadResult *r = (ManualReadResult *)data;
    int waited_ms = 0;

    r->len = 0;   /* پیش‌فرض: timeout */

    while (waited_ms < MANUAL_READ_TIMEOUT_MS) {
        g_mutex_lock(&app.hid_mutex);
        hid_device *dev = app.dev;
        if (!dev || !g_atomic_int_get(&app.connected)) {
            r->len = -1;
            snprintf(r->errmsg, sizeof(r->errmsg), "not connected");
            g_mutex_unlock(&app.hid_mutex);
            break;
        }

        /* --- تغییر: استفاده از Control Read به جای hid_read --- */
        unsigned char buf[MAX_READ_BUF];
        memset(buf, 0, sizeof(buf));
        buf[0] = r->has_rid ? r->rid : 0x00;   /* Report ID در بایت اول */

        int want = (int)r->tab->sizes.input_bytes + 1;  /* +1 برای Report ID */
        if (want > MAX_READ_BUF) want = MAX_READ_BUF;

        int n = hid_get_input_report(dev, buf, want);

        if (n < 0) {
            r->len = -1;
            snprintf(r->errmsg, sizeof(r->errmsg), "%s", hid_err_str_locked(dev));
            g_mutex_unlock(&app.hid_mutex);
            break;
        }
        g_mutex_unlock(&app.hid_mutex);

        if (n > 0) {
            memcpy(r->data, buf, n);
            r->len = n;
            break;
        }

        g_usleep(MANUAL_READ_STEP_MS * 1000);
        waited_ms += MANUAL_READ_STEP_MS;
    }

    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, deliver_manual_read, r, g_free);
    g_atomic_int_dec_and_test(&app.manual_reads_active);
    return NULL;
}

static void on_read_input(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!g_atomic_int_get(&app.connected)) {
        log_to_tab(tab, "READ", "not connected");
        return;
    }
    if (tab->sizes.input_bytes == 0) {
        log_to_tab(tab, "READ", "this report has no Input capability");
        return;
    }

    ManualReadResult *r = g_new0(ManualReadResult, 1);
    r->tab = tab;
    r->rid = tab->report_id;
    r->has_rid = tab->has_report_id;
    r->generation = g_atomic_int_get(&app.tabs_generation);

    g_atomic_int_inc(&app.manual_reads_active);

    GThread *t = g_thread_new("hid_manual_read", manual_read_thread, r);
    if (!t) {
        g_atomic_int_dec_and_test(&app.manual_reads_active);
        log_to_tab(tab, "READ", "failed to start read thread");
        g_free(r);
        return;
    }
    g_thread_unref(t);
}
/* ---------- Feature: Set / Get ---------- */

static void on_send_feature(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!g_atomic_int_get(&app.connected)) {
        log_to_tab(tab, "FEAT", "not connected");
        return;
    }
    if (tab->sizes.feature_bytes == 0) {
        log_to_tab(tab, "FEAT", "this report has no Feature capability");
        return;
    }

    const char *txt = gtk_entry_get_text(GTK_ENTRY(tab->feature_entry));
    unsigned char buf[MAX_REPORT_BYTES];
    int n = parse_input(txt, buf, sizeof(buf));
    if (n == 0) { log_to_tab(tab, "FEAT", "(empty)"); return; }

    if (n > (int)tab->sizes.feature_bytes) {
        char warn[160];
        snprintf(warn, sizeof(warn),
                 "WARNING: %d bytes > max %u bytes (sending anyway)",
                 n, tab->sizes.feature_bytes);
        log_to_tab(tab, "FEAT", warn);
    }

    SyncOp *op = g_new0(SyncOp, 1);
    op->tab = tab;
    op->generation = g_atomic_int_get(&app.tabs_generation);
    op->op = OP_FEATURE_SET;
    memcpy(op->payload, buf, n);
    op->payload_len = n;
    op->report_id = tab->report_id;
    op->has_report_id = tab->has_report_id;

    start_sync_op(op, "FEAT");
}

static void on_get_feature(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!g_atomic_int_get(&app.connected)) {
        log_to_tab(tab, "FEAT", "not connected");
        return;
    }
    if (tab->sizes.feature_bytes == 0) {
        log_to_tab(tab, "FEAT", "this report has no Feature capability");
        return;
    }

    int read_len = (int)tab->sizes.feature_bytes;
    if (read_len <= 0) read_len = 64;
    if (read_len > MAX_REPORT_BYTES) read_len = MAX_REPORT_BYTES;

    SyncOp *op = g_new0(SyncOp, 1);
    op->tab = tab;
    op->generation = g_atomic_int_get(&app.tabs_generation);
    op->op = OP_FEATURE_GET;
    op->report_id = tab->report_id;
    op->has_report_id = tab->has_report_id;
    op->read_len = read_len;

    start_sync_op(op, "FEAT");
}

/* ---------- تحویل داده از thread به GTK ---------- */

static gboolean deliver_input(gpointer data) {
    ReadEvent *ev = (ReadEvent *)data;
    if (ev->generation != g_atomic_int_get(&app.tabs_generation))
        return G_SOURCE_REMOVE;
    if (!g_atomic_int_get(&app.connected))
        return G_SOURCE_REMOVE;
    if (ev->len > 0)
        route_input_data(ev->data, ev->len, ev->has_report_id, "IN  ");
    return G_SOURCE_REMOVE;
}

typedef struct { char msg[320]; } ReaderFailedMsg;

static gboolean on_reader_failed(gpointer data) {
    ReaderFailedMsg *m = (ReaderFailedMsg *)data;
    if (g_atomic_int_get(&app.shutting_down)) return G_SOURCE_REMOVE;

    g_mutex_lock(&app.hid_mutex);
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    g_mutex_unlock(&app.hid_mutex);

    g_atomic_int_set(&app.connected, 0);
    clear_tabs();

    if (app.status_label)
        gtk_label_set_text(GTK_LABEL(app.status_label), m->msg);

    if (app.connect_button) {
        gtk_button_set_label(GTK_BUTTON(app.connect_button), "Connect");
        gtk_widget_set_sensitive(app.connect_button,
                                 app.device_paths && app.device_paths->len > 0);
    }
    if (app.device_combo)   gtk_widget_set_sensitive(app.device_combo, TRUE);
    if (app.refresh_button) gtk_widget_set_sensitive(app.refresh_button, TRUE);
    return G_SOURCE_REMOVE;
}

/* ---------- Thread خواندن (non-blocking polling) ---------- */

static gpointer reader_thread_func(gpointer data) {
    App *a = (App *)data;
    unsigned char buf[MAX_READ_BUF];
    gint gen = g_atomic_int_get(&a->tabs_generation);

    while (g_atomic_int_get(&a->reader_running)) {
        int n = 0;
        char errbuf[256] = {0};

        g_mutex_lock(&a->hid_mutex);
        hid_device *dev = a->dev;
        if (dev && g_atomic_int_get(&a->connected) &&
            g_atomic_int_get(&a->reader_running)) {
            /* non-blocking read — بلافاصله برمی‌گردد */
            n = hid_read(dev, buf, sizeof(buf));
            if (n < 0)
                snprintf(errbuf, sizeof(errbuf), "%s", hid_err_str_locked(dev));
        }
        g_mutex_unlock(&a->hid_mutex);

        if (n > 0) {
            ReadEvent *ev = g_malloc(sizeof(ReadEvent));
            memcpy(ev->data, buf, n);
            ev->len = n;
            ev->has_report_id = g_atomic_int_get(&a->has_report_id);
            ev->generation = gen;
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, deliver_input, ev, g_free);
        } else if (n < 0) {
            g_atomic_int_set(&a->reader_running, 0);
            if (!g_atomic_int_get(&a->shutting_down)) {
                ReaderFailedMsg *m = g_malloc(sizeof(ReaderFailedMsg));
                snprintf(m->msg, sizeof(m->msg), "Reader stopped: %s", errbuf);
                g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, on_reader_failed, m, g_free);
            }
            break;
        } else {
            /* n == 0: هنوز داده‌ای نیست، کوتاه بخواب تا بقیه فرصت کنند */
            g_usleep(READER_POLL_SLEEP_US);
        }
    }
    return NULL;
}

static void stop_reader_thread(void) {
    if (app.reader_thread) {
        g_atomic_int_set(&app.reader_running, 0);
        g_thread_join(app.reader_thread);
        app.reader_thread = NULL;
    }
}

/* ---------- ساخت ویو ---------- */

static GtkWidget *make_terminal_view(GtkWidget **out_view) {
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled, TRUE);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    if (out_view) *out_view = view;
    return scrolled;
}

/* ---------- مدیریت تب‌ها ---------- */

static void clear_tabs(void) {
    stop_reader_thread();

    g_atomic_int_inc(&app.tabs_generation);

    if (!app.notebook) return;

    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app.notebook));
    for (int i = n - 1; i >= 0; i--) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(app.notebook), i);
    }

    app.tab_count = 0;
    memset(app.tabs, 0, sizeof(app.tabs));
}

static void clear_all_logs(void) {
    for (int i = 0; i < MAX_REPORT_ID; i++) {
        ReportBOX *t = app.tabs[i];
        if (t && t->input_buf) {
            gtk_text_buffer_set_text(t->input_buf, "", -1);
            t->log_lines = 0;
        }
    }
}

static void build_tabs(const unsigned char *desc, int len)
{
    clear_tabs();

    int has_id = 0;
    parse_report_descriptor(desc, len, app.main_sizes, &has_id);
    g_atomic_int_set(&app.has_report_id, has_id);

    for (int id = 0; id < MAX_REPORT_ID; id++) {
        ReportSizes *hidsizes = &app.main_sizes[id];
        if (!hidsizes->input_bytes && !hidsizes->output_bytes && !hidsizes->feature_bytes)
            continue;

        char tab_title[32];
        if (!has_id)
            snprintf(tab_title, sizeof(tab_title), "Default");
        else
            snprintf(tab_title, sizeof(tab_title), "Report ID:0x%02X", (unsigned)id);

        ReportBOX *tab = g_new0(ReportBOX, 1);
        tab->report_id = id;
        tab->has_report_id = has_id ? 1 : 0;
        tab->sizes = *hidsizes;
        tab->log_lines = 0;

        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

        GtkWidget *scrolled = make_terminal_view(&tab->input_view);
        tab->input_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab->input_view));
        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

        const char *ph = is_hex_mode() ? "hex bytes e.g. 01 02 FF" : "text e.g. Hello";

        GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);

        if (tab->sizes.output_bytes) {
            tab->output_entry = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(tab->output_entry), ph);
            g_signal_connect(tab->output_entry, "activate",
                             G_CALLBACK(on_send_output), tab);
            gtk_box_pack_start(GTK_BOX(hbox), tab->output_entry, TRUE, TRUE, 0);

            GtkWidget *send = gtk_button_new_with_label("Send");
            gtk_widget_set_size_request(send, 120, -1);
            g_signal_connect(send, "clicked", G_CALLBACK(on_send_output), tab);
            gtk_box_pack_start(GTK_BOX(hbox), send, FALSE, FALSE, 0);
        }

        if (tab->sizes.input_bytes) {
            GtkWidget *read_in_btn = gtk_button_new_with_label("Read");
            gtk_widget_set_size_request(read_in_btn, 120, -1);
            g_signal_connect(read_in_btn, "clicked", G_CALLBACK(on_read_input), tab);
            gtk_box_pack_start(GTK_BOX(hbox), read_in_btn, FALSE, FALSE, 0);
        }

        GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
        if (tab->sizes.feature_bytes) {
            tab->feature_entry = gtk_entry_new();
            gtk_entry_set_placeholder_text(GTK_ENTRY(tab->feature_entry), ph);
            gtk_box_pack_start(GTK_BOX(hbox2), tab->feature_entry, TRUE, TRUE, 0);

            GtkWidget *set_btn = gtk_button_new_with_label("Set Feature");
            gtk_widget_set_size_request(set_btn, 120, -1);
            g_signal_connect(set_btn, "clicked", G_CALLBACK(on_send_feature), tab);
            gtk_box_pack_start(GTK_BOX(hbox2), set_btn, FALSE, FALSE, 0);

            GtkWidget *get_btn = gtk_button_new_with_label("Get Feature");
            gtk_widget_set_size_request(get_btn, 120, -1);
            g_signal_connect(get_btn, "clicked", G_CALLBACK(on_get_feature), tab);
            gtk_box_pack_start(GTK_BOX(hbox2), get_btn, FALSE, FALSE, 0);
        }

        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);

        g_object_set_data_full(G_OBJECT(vbox), "report_box", tab, g_free);
        gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook),
                                 vbox, gtk_label_new(tab_title));
        gtk_widget_show_all(vbox);
        app.tabs[id] = tab;
        app.tab_count++;
    }
}

/* ---------- دستگاه ---------- */

static void populate_devices(void) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter iter;

    g_ptr_array_set_size(app.device_paths, 0);
    if (app.device_ids) g_ptr_array_set_size(app.device_ids, 0);

    struct hid_device_info *devs = hid_enumerate(0, 0);
    int count = 0;
    for (struct hid_device_info *cur = devs; cur && count < MAX_DEVICES; cur = cur->next) {
        char mfg[128], prod[256];
        wchar_to_locale(cur->manufacturer_string, mfg, sizeof(mfg));
        wchar_to_locale(cur->product_string, prod, sizeof(prod));

        char label[512];
        snprintf(label, sizeof(label), "%04x:%04x  %s %s",
                 cur->vendor_id, cur->product_id, mfg, prod);

        /* شناسه پایدار برای بازیابی بین اجراها */
        char dev_id[256];
        if (cur->serial_number && *cur->serial_number) {
            char ser[160];
            wchar_to_locale(cur->serial_number, ser, sizeof(ser));
            snprintf(dev_id, sizeof(dev_id), "%04x:%04x:%s",
                     cur->vendor_id, cur->product_id, ser);
        } else {
            snprintf(dev_id, sizeof(dev_id), "%04x:%04x",
                     cur->vendor_id, cur->product_id);
        }

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, label, -1);
        g_ptr_array_add(app.device_paths, g_strdup(cur->path));
        if (app.device_ids)
            g_ptr_array_add(app.device_ids, g_strdup(dev_id));
        count++;
    }
    hid_free_enumeration(devs);

    gtk_combo_box_set_model(GTK_COMBO_BOX(app.device_combo), GTK_TREE_MODEL(store));
    g_object_unref(store);

    apply_pending_device();   /* به‌جای set_active(0): انتخاب قبلی را بازیابی می‌کند */

    char msg[64];
    if (count)
        snprintf(msg, sizeof(msg), "Found %d device(s)", count);
    else
        snprintf(msg, sizeof(msg), "No HID devices found");
    gtk_label_set_text(GTK_LABEL(app.status_label), msg);

    if (app.connect_button)
        gtk_widget_set_sensitive(app.connect_button, count > 0);
}

static void on_refresh_clicked(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    if (g_atomic_int_get(&app.connected)) return;
    populate_devices();
}

static void disconnect_device(void) {
    stop_reader_thread();

    g_mutex_lock(&app.hid_mutex);
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    g_mutex_unlock(&app.hid_mutex);

    g_atomic_int_set(&app.connected, 0);
    clear_tabs();

    gtk_button_set_label(GTK_BUTTON(app.connect_button), "Connect");
    gtk_widget_set_sensitive(app.device_combo, TRUE);
    gtk_widget_set_sensitive(app.refresh_button, TRUE);
    gtk_widget_set_sensitive(app.connect_button,
                             app.device_paths && app.device_paths->len > 0);

    gtk_label_set_text(GTK_LABEL(app.status_label), "Disconnected");
}


static void connect_device(void) {
    int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app.device_combo));
    if (sel < 0 || sel >= (int)app.device_paths->len) {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Select a device first");
        return;
    }
    const char *path = g_ptr_array_index(app.device_paths, sel);
    if (!path) {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Selected device has no path");
        return;
    }

    stop_reader_thread();
    g_mutex_lock(&app.hid_mutex);
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    g_mutex_unlock(&app.hid_mutex);

    hid_device *dev = hid_open_path(path);
    if (!dev) {
        gtk_label_set_text(GTK_LABEL(app.status_label),
                           "Failed to open device (permissions? already in use?)");
        return;
    }

    /* non-blocking تا hid_read فوراً برگردد */
    hid_set_nonblocking(dev, 1);

    unsigned char desc[MAX_DESCRIPTOR_SIZE];
    int n = hid_get_report_descriptor(dev, desc, sizeof(desc));
    if (n <= 0) {
        hid_close(dev);
        gtk_label_set_text(GTK_LABEL(app.status_label),
                           n == 0 ? "Empty Report Descriptor"
                                  : "Failed to read Report Descriptor");
        return;
    }

    g_mutex_lock(&app.hid_mutex);
    app.dev = dev;
    g_mutex_unlock(&app.hid_mutex);
    g_atomic_int_set(&app.connected, 1);

    build_tabs(desc, n);

    /*
     * بررسی می‌کنیم که آیا اصلاً Input Report وجود دارد یا نه.
     * دستگاه‌هایی که فقط Feature (یا Output) دارند معمولاً
     * Interrupt IN endpoint ندارند و hid_read روی آن‌ها خطا می‌دهد.
     * در آن صورت reader thread را اصلاً اجرا نمی‌کنیم.
     */
    int has_input = 0;
    for (int i = 0; i < MAX_REPORT_ID; i++) {
        if (app.main_sizes[i].input_bytes > 0) { has_input = 1; break; }
    }

    if (has_input) {
        g_atomic_int_set(&app.reader_running, 1);
        app.reader_thread = g_thread_new("hid_reader", reader_thread_func, &app);
        if (!app.reader_thread) {
            g_atomic_int_set(&app.reader_running, 0);
            g_atomic_int_set(&app.connected, 0);
            g_mutex_lock(&app.hid_mutex);
            if (app.dev) { hid_close(app.dev); app.dev = NULL; }
            g_mutex_unlock(&app.hid_mutex);
            clear_tabs();
            gtk_label_set_text(GTK_LABEL(app.status_label),
                               "Failed to start reader thread");
            gtk_widget_set_sensitive(app.device_combo, TRUE);
            gtk_widget_set_sensitive(app.refresh_button, TRUE);
            gtk_button_set_label(GTK_BUTTON(app.connect_button), "Connect");
            return;
        }
    } else {
        app.reader_thread = NULL;
        g_atomic_int_set(&app.reader_running, 0);
    }

    gtk_button_set_label(GTK_BUTTON(app.connect_button), "Disconnect");
    gtk_widget_set_sensitive(app.device_combo, FALSE);
    gtk_widget_set_sensitive(app.refresh_button, FALSE);

    char status[200];
    snprintf(status, sizeof(status),
             "Connected. Descriptor: %d bytes, %d tab(s)%s",
             n, app.tab_count,
             has_input ? "" : " (Feature/Output only, no reader)");
    gtk_label_set_text(GTK_LABEL(app.status_label), status);
}

static void on_connect_clicked(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    if (g_atomic_int_get(&app.connected)) disconnect_device();
    else connect_device();
}

static void on_clear_clicked(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    clear_all_logs();
    gtk_label_set_text(GTK_LABEL(app.status_label), "Logs cleared");
}

/* ---------- HEX/ASCII ---------- */

static void on_hex_toggled(GtkToggleButton *b, gpointer data) {
    (void)data;
    int hex = gtk_toggle_button_get_active(b);
    g_atomic_int_set(&app.hex_mode, hex);

    const char *ph = hex ? "hex bytes e.g. 01 02 FF" : "text e.g. Hello";
    for (int i = 0; i < MAX_REPORT_ID; i++) {
        ReportBOX *t = app.tabs[i];
        if (!t) continue;
        if (t->output_entry)
            gtk_entry_set_placeholder_text(GTK_ENTRY(t->output_entry), ph);
        if (t->feature_entry)
            gtk_entry_set_placeholder_text(GTK_ENTRY(t->feature_entry), ph);
    }

    gtk_label_set_text(GTK_LABEL(app.status_label), hex ? "Mode: HEX" : "Mode: ASCII");
}

/* ---------- auto-connect پس از نمایش پنجره ---------- */

static gboolean auto_connect_idle(gpointer data) {
    (void)data;
    if (!app.auto_check) return G_SOURCE_REMOVE;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.auto_check)))
        return G_SOURCE_REMOVE;
    if (!g_atomic_int_get(&app.connected) &&
        app.device_paths && app.device_paths->len > 0)
        connect_device();
    return G_SOURCE_REMOVE;
}

/* ---------- window destroy handler ---------- */

static void on_window_destroy(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (g_atomic_int_get(&app.shutting_down)) return;
    g_atomic_int_set(&app.shutting_down, 1);

    /* ذخیره تنظیمات پیش از بستن منابع */
    save_config();

    stop_reader_thread();
    g_atomic_int_set(&app.connected, 0);

    for (int i = 0; i < 30; i++) {
        if (g_atomic_int_get(&app.manual_reads_active) == 0) break;
        g_usleep(20 * 1000);
    }

    g_mutex_lock(&app.hid_mutex);
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    g_mutex_unlock(&app.hid_mutex);

    clear_tabs();
    g_ptr_array_set_size(app.device_paths, 0);
    if (app.device_ids) g_ptr_array_set_size(app.device_ids, 0);

    gtk_main_quit();
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    if (hid_init() != 0) {
        g_printerr("Failed to initialize hidapi\n");
        return 1;
    }

    g_mutex_init(&app.hid_mutex);
    app.device_paths = g_ptr_array_new_with_free_func(g_free);
    app.device_ids   = g_ptr_array_new_with_free_func(g_free);
    app.tabs_generation = 0;
    app.manual_reads_active = 0;

    gtk_init(&argc, &argv);




        static const char *ui_paths[] = {
            MIAHIDPORDO_DATA_DIR "/window1.glade",
            "window1.glade",
            NULL
        };


        GError *err = NULL;
        GtkBuilder *builder = gtk_builder_new();
        gboolean loaded = FALSE;

        for (guint i = 0; ui_paths[i] && !loaded; i++)
            if (g_file_test(ui_paths[i], G_FILE_TEST_EXISTS))
                loaded = gtk_builder_add_from_file(builder, ui_paths[i], &err);

    if (!loaded) {
        g_printerr("MiaHIDPordo: Failed to load window1.glade: %s\n", err->message);
        g_error_free(err);
        g_object_unref(builder);
        g_ptr_array_free(app.device_paths, TRUE);
        g_ptr_array_free(app.device_ids,   TRUE);
        g_mutex_clear(&app.hid_mutex);
        hid_exit();
        return 1;
    }

    app.window         = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    app.device_combo   = GTK_WIDGET(gtk_builder_get_object(builder, "device_combo"));
    app.notebook       = GTK_WIDGET(gtk_builder_get_object(builder, "report_notebook"));
    app.status_label   = GTK_WIDGET(gtk_builder_get_object(builder, "status_label"));
    app.hex_check      = GTK_WIDGET(gtk_builder_get_object(builder, "hex_check"));
    app.refresh_button = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_button"));
    app.connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "connect_button"));
    app.clear_button   = GTK_WIDGET(gtk_builder_get_object(builder, "clear_button"));
    app.auto_check     = GTK_WIDGET(gtk_builder_get_object(builder, "check_autoconnect"));

    if (!app.window || !app.device_combo || !app.notebook || !app.status_label ||
        !app.hex_check || !app.refresh_button || !app.connect_button || !app.clear_button) {
        g_printerr("Error: Failed to find required widgets in glade file\n");
        g_object_unref(builder);
        g_ptr_array_free(app.device_paths, TRUE);
        g_ptr_array_free(app.device_ids,   TRUE);
        g_mutex_clear(&app.hid_mutex);
        hid_exit();
        return 1;
    }

    /* چک‌باکس اتوکانکت در glade بدون برچسب است؛ برچسب را در کد اضافه می‌کنیم */
    if (app.auto_check && !gtk_button_get_label(GTK_BUTTON(app.auto_check)))
        gtk_button_set_label(GTK_BUTTON(app.auto_check), "Auto");

    GtkCellRenderer *rend = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(app.device_combo), rend, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(app.device_combo), rend, "text", 0, NULL);

    g_signal_connect(app.refresh_button , "clicked", G_CALLBACK(on_refresh_clicked ), NULL);
    g_signal_connect(app.connect_button , "clicked", G_CALLBACK(on_connect_clicked ), NULL);
    g_signal_connect(app.clear_button   , "clicked", G_CALLBACK(on_clear_clicked   ), NULL);
    g_signal_connect(app.hex_check      , "toggled", G_CALLBACK(on_hex_toggled     ), NULL);
    g_signal_connect(app.window         , "destroy", G_CALLBACK(on_window_destroy  ), NULL);

    /* پیش‌فرض اولیه */
    g_atomic_int_set(&app.hex_mode, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.hex_check), FALSE);

    g_object_unref(builder);

    /* بازیابی تنظیمات ذخیره‌شده (قبل از populate تا last_device اعمال شود) */
    load_config();

    populate_devices();
    gtk_widget_show_all(app.window);

    /* اتصال خودکار پس از نمایش پنجره */
    g_idle_add(auto_connect_idle, NULL);

    gtk_main();

    g_ptr_array_free(app.device_paths, TRUE);
    g_ptr_array_free(app.device_ids,   TRUE);
    g_mutex_clear(&app.hid_mutex);
    g_free(cfg_path);
    g_free(pending_last_device);
    hid_exit();
    return 0;
}