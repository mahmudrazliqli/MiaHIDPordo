#include <gtk/gtk.h>
#include <hidapi/hidapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <wchar.h>

#define MAX_DESCRIPTOR_SIZE 4096
#define MAX_DEVICES 128
#define MAX_REPORT_ID 256
#define MAX_LOG_LINES 500
#define HEX_BUF_SIZE 4096
#define MAX_REPORT_BYTES 1024
#define MAX_READ_BUF (MAX_REPORT_BYTES + 1)
#define MAX_GLOBAL_STACK 16

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
} ReportBOX;

typedef struct {
    GtkWidget *notebook, *status_label, *device_combo, *window, *hex_check;
    GtkWidget *connect_button, *refresh_button, *clear_button;

    char *device_paths[MAX_DEVICES];
    int device_count;

    hid_device *dev;
    int connected;
    int has_report_id;
    ReportSizes main_sizes[MAX_REPORT_ID];

    ReportBOX *tabs[MAX_REPORT_ID];
    int tab_count;

    GThread *reader_thread;
    gint reader_running;
    gint shutting_down;
} App;

static App app;

typedef struct {
    unsigned char data[MAX_READ_BUF];
    int len;
    int has_report_id;
} ReadEvent;

/* ---------- Forward declarations ---------- */
static gpointer reader_thread_func(gpointer data);

/* ---------- توابع کمکی ---------- */

static int is_hex_mode(void) {
    if (!app.hex_check) return 0;
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.hex_check));
}

/* hid_error ممکن است NULL برگرداند؛ تبدیل ایمن wchar_t -> char.
 * نکته: بافر static است و فقط باید از ترد اصلی GTK صدا زده شود. */
static const char *hid_err_str(hid_device *dev) {
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

static void clear_devices(void) {
    for (int i = 0; i < app.device_count; i++) {
        free(app.device_paths[i]);
        app.device_paths[i] = NULL;
    }
    app.device_count = 0;
}

static void format_bytes(const unsigned char *buf, int len, char *out, size_t out_size) {
    if (len <= 0 || out_size == 0) { out[0] = '\0'; return; }
    if (is_hex_mode()) {
        int pos = 0;
        for (int i = 0; i < len && pos < (int)out_size - 4; i++)
            pos += snprintf(out + pos, out_size - pos, "%02X ", buf[i]);
        if (pos > 0) out[pos - 1] = '\0';
        else out[0] = '\0';
    } else {
        int pos = 0;
        for (int i = 0; i < len && pos < (int)out_size - 2; i++) {
            unsigned char c = buf[i];
            out[pos++] = isprint((unsigned char)c) ? (char)c : '.';
        }
        out[pos] = '\0';
    }
}

/* ورودی هگز: حداکثر ۲ رقم هگز برای هر بایت، با پشتیبانی اختیاری از پیشوند 0x. */
static int parse_input(const char *str, unsigned char *out, int max_len) {
    if (is_hex_mode()) {
        int count = 0;
        const char *p = str;
        while (*p && count < max_len) {
            while (*p == ' ' || *p == '\t' || *p == ',' || *p == ':' ||
                   *p == '\n' || *p == '\r')
                p++;
            if (!*p) break;

            /* پیشوند اختیاری 0x / 0X */
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

            /* حداکثر ۲ رقم هگز برای یک بایت */
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

static void log_to_buffer(GtkTextBuffer *buf, const char *prefix, const char *text) {
    if (!buf) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);

    char line[HEX_BUF_SIZE + 64];
    snprintf(line, sizeof(line), "[%s] %s\n", prefix, text);
    gtk_text_buffer_insert(buf, &end, line, -1);

    int line_count = gtk_text_buffer_get_line_count(buf);
    if (line_count > MAX_LOG_LINES) {
        GtkTextIter start, cut;
        gtk_text_buffer_get_start_iter(buf, &start);
        cut = start;
        gtk_text_iter_forward_lines(&cut, line_count - MAX_LOG_LINES);
        gtk_text_buffer_delete(buf, &start, &cut);
    }

    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_place_cursor(buf, &end);

    GtkTextView *view = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buf), "view"));
    if (view) {
        GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
        gtk_text_view_scroll_to_mark(view, mark, 0.0, FALSE, 0, 0);
        gtk_text_buffer_delete_mark(buf, mark);
    }
}

/* ---------- تجزیه Report Descriptor ---------- */

typedef struct {
    unsigned int report_size;
    unsigned int report_count;
    unsigned char report_id;
} GlobalState;

static void parse_report_descriptor(const unsigned char *desc, int len,
                                    ReportSizes *out_sizes, int *has_report_id) {
    GlobalState cur;
    GlobalState stack[MAX_GLOBAL_STACK];
    int sp = 0;

    memset(&cur, 0, sizeof(cur));
    *has_report_id = 0;
    memset(out_sizes, 0, sizeof(ReportSizes) * MAX_REPORT_ID);

    for (int i = 0; i < len; ) {
        unsigned char prefix = desc[i++];

        if (prefix == 0xFE) {
            /* Long Item: 0xFE | bDataSize(1) | bLongItemTag(1) | Data(bDataSize) */
            if (i >= len) break;
            int dlen = desc[i];                 /* bDataSize */
            if (i + 2 + dlen > len) break;      /* محافظت از مرز */
            i += 2 + dlen;                      /* رد کردن bDataSize + bLongItemTag + data */
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
            /* Global Item */
            if (tag == 0x7) {
                cur.report_size = data;
            } else if (tag == 0x9) {
                cur.report_count = data;
            } else if (tag == 0x8) {
                cur.report_id = (unsigned char)(data & 0xFF);
                *has_report_id = 1;
            } else if (tag == 0xA) {
                /* Push */
                if (sp < MAX_GLOBAL_STACK) stack[sp++] = cur;
            } else if (tag == 0xB) {
                /* Pop */
                if (sp > 0) cur = stack[--sp];
            }
        } else if (type == 0) {
            /* Main Item: با محافظت از سرریز */
            unsigned long long n =
                (unsigned long long)cur.report_size * (unsigned long long)cur.report_count;
            if (n > 0xFFFFFFFFULL) n = 0xFFFFFFFFULL;
            unsigned int nn = (unsigned int)n;

            if (tag == 0x8) {
                unsigned long long tot =
                    (unsigned long long)out_sizes[cur.report_id].input_bits + nn;
                out_sizes[cur.report_id].input_bits =
                    (tot > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (unsigned int)tot;
            } else if (tag == 0x9) {
                unsigned long long tot =
                    (unsigned long long)out_sizes[cur.report_id].output_bits + nn;
                out_sizes[cur.report_id].output_bits =
                    (tot > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (unsigned int)tot;
            } else if (tag == 0xB) {
                unsigned long long tot =
                    (unsigned long long)out_sizes[cur.report_id].feature_bits + nn;
                out_sizes[cur.report_id].feature_bits =
                    (tot > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (unsigned int)tot;
            }
        }
    }

    for (int id = 0; id < MAX_REPORT_ID; id++) {
        out_sizes[id].input_bytes   = (out_sizes[id].input_bits   + 7) / 8;
        out_sizes[id].output_bytes  = (out_sizes[id].output_bits  + 7) / 8;
        out_sizes[id].feature_bytes = (out_sizes[id].feature_bits + 7) / 8;
    }
}

/* ---------- مسیردهی داده‌ی ورودی ---------- */

static void route_input_data(const unsigned char *buf, int n,
                             int has_report_id, const char *prefix) {
    char msg[HEX_BUF_SIZE], disp[2048];
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
        snprintf(msg, sizeof(msg), "RECV (%d bytes): %s", payload_len, disp);
        for (int i = 0; i < app.tab_count; i++) {
            if (app.tabs[i] && app.tabs[i]->input_buf)
                log_to_buffer(app.tabs[i]->input_buf, prefix, msg);
        }
    } else {
        int found = 0;
        snprintf(msg, sizeof(msg), "RECV (%d bytes): %s", payload_len, disp);
        for (int i = 0; i < app.tab_count; i++) {
            ReportBOX *t = app.tabs[i];
            if (t && t->has_report_id && t->report_id == rid) {
                log_to_buffer(t->input_buf, prefix, msg);
                found = 1;
            }
        }
        if (!found) {
            char msg2[HEX_BUF_SIZE];
            snprintf(msg2, sizeof(msg2),
                     "RECV (ReportID=0x%02X, %d bytes): %s",
                     rid, payload_len, disp);
            for (int i = 0; i < app.tab_count; i++) {
                if (app.tabs[i] && app.tabs[i]->input_buf)
                    log_to_buffer(app.tabs[i]->input_buf, prefix, msg2);
            }
        }
    }
}

/* ---------- ارسال Output ---------- */

static void on_send_output(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!app.connected) { log_to_buffer(tab->input_buf, "OUT", "not connected"); return; }
    if (tab->sizes.output_bytes == 0) {
        log_to_buffer(tab->input_buf, "OUT", "this report has no Output capability");
        return;
    }

    const char *txt = gtk_entry_get_text(GTK_ENTRY(tab->output_entry));
    unsigned char buf[MAX_REPORT_BYTES];
    int n = parse_input(txt, buf, sizeof(buf));
    if (n == 0) { log_to_buffer(tab->input_buf, "OUT", "(empty)"); return; }

    if (n > (int)tab->sizes.output_bytes) {
        char warn[128];
        snprintf(warn, sizeof(warn), "WARNING: %d bytes > max %u bytes",
                 n, tab->sizes.output_bytes);
        log_to_buffer(tab->input_buf, "OUT", warn);
    }
    if (n > MAX_REPORT_BYTES) n = MAX_REPORT_BYTES;

    /* hidapi: همیشه بایت اول = Report ID (0x00 برای بدون Report ID) */
    unsigned char out_buf[MAX_READ_BUF];
    int out_len = 0;
    out_buf[out_len++] = tab->has_report_id ? tab->report_id : 0x00;
    memcpy(out_buf + out_len, buf, n);
    out_len += n;

    int written = hid_write(app.dev, out_buf, out_len);
    char msg[HEX_BUF_SIZE], disp[2048];
    format_bytes(buf, n, disp, sizeof(disp));
    if (written < 0)
        snprintf(msg, sizeof(msg), "hid_write failed: %s", hid_err_str(app.dev));
    else
        snprintf(msg, sizeof(msg), "SENT (%d bytes): %s", n, disp);
    log_to_buffer(tab->input_buf, "OUT", msg);
}

/* ---------- Read Input (دکمه Read) ---------- */

static void on_read_input(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!app.connected) { log_to_buffer(tab->input_buf, "READ", "not connected"); return; }

    /* توقف موقت thread خواننده تا با هم تداخل نکنند */
    g_atomic_int_set(&app.reader_running, 0);
    if (app.reader_thread) {
        g_thread_join(app.reader_thread);
        app.reader_thread = NULL;
    }

    unsigned char buf[MAX_READ_BUF];
    int n = hid_read_timeout(app.dev, buf, sizeof(buf), 500);
    char msg[HEX_BUF_SIZE];
    if (n < 0) {
        snprintf(msg, sizeof(msg), "READ failed: %s", hid_err_str(app.dev));
        log_to_buffer(tab->input_buf, "READ", msg);
    } else if (n == 0) {
        log_to_buffer(tab->input_buf, "READ", "timeout (no data)");
    } else {
        route_input_data(buf, n, app.has_report_id, "READ");
    }

    /* راه‌اندازی مجدد thread خواننده */
    if (app.connected) {
        g_atomic_int_set(&app.reader_running, 1);
        app.reader_thread = g_thread_new("hid_reader", reader_thread_func, &app);
        if (!app.reader_thread) {
            g_atomic_int_set(&app.reader_running, 0);
        }
    }
}

/* ---------- Feature: Set/Get ---------- */

static void on_send_feature(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!app.connected) { log_to_buffer(tab->input_buf, "FEAT", "not connected"); return; }
    if (tab->sizes.feature_bytes == 0) {
        log_to_buffer(tab->input_buf, "FEAT", "this report has no Feature capability");
        return;
    }

    const char *txt = gtk_entry_get_text(GTK_ENTRY(tab->feature_entry));
    unsigned char buf[MAX_REPORT_BYTES];
    int n = parse_input(txt, buf, sizeof(buf));
    if (n == 0) { log_to_buffer(tab->input_buf, "FEAT", "(empty)"); return; }

    if (n > (int)tab->sizes.feature_bytes) {
        char warn[128];
        snprintf(warn, sizeof(warn), "WARNING: %d bytes > max %u bytes",
                 n, tab->sizes.feature_bytes);
        log_to_buffer(tab->input_buf, "FEAT", warn);
    }
    if (n > MAX_REPORT_BYTES) n = MAX_REPORT_BYTES;

    unsigned char out_buf[MAX_READ_BUF];
    int out_len = 0;
    out_buf[out_len++] = tab->has_report_id ? tab->report_id : 0x00;
    memcpy(out_buf + out_len, buf, n);
    out_len += n;

    int written = hid_send_feature_report(app.dev, out_buf, out_len);
    char msg[HEX_BUF_SIZE], disp[2048];
    format_bytes(buf, n, disp, sizeof(disp));
    if (written < 0)
        snprintf(msg, sizeof(msg), "hid_send_feature_report failed: %s", hid_err_str(app.dev));
    else
        snprintf(msg, sizeof(msg), "SET (%d bytes): %s", n, disp);
    log_to_buffer(tab->input_buf, "FEAT", msg);
}

static void on_get_feature(GtkButton *b, gpointer data) {
    (void)b;
    ReportBOX *tab = (ReportBOX *)data;
    if (!app.connected) { log_to_buffer(tab->input_buf, "FEAT", "not connected"); return; }
    if (tab->sizes.feature_bytes == 0) {
        log_to_buffer(tab->input_buf, "FEAT", "this report has no Feature capability");
        return;
    }

    int read_len = (int)tab->sizes.feature_bytes;
    if (read_len <= 0) read_len = 64;
    if (read_len > MAX_REPORT_BYTES) read_len = MAX_REPORT_BYTES;

    unsigned char out_buf[MAX_READ_BUF];
    memset(out_buf, 0, sizeof(out_buf));
    /* hidapi: همیشه بایت اول = Report ID در درخواست */
    out_buf[0] = tab->has_report_id ? tab->report_id : 0x00;

    int total = read_len + 1;
    if (total > (int)sizeof(out_buf)) total = sizeof(out_buf);

    int got = hid_get_feature_report(app.dev, out_buf, total);
    char msg[HEX_BUF_SIZE], disp[2048];
    if (got < 0) {
        snprintf(msg, sizeof(msg), "hid_get_feature_report failed: %s", hid_err_str(app.dev));
    } else if (got == 0) {
        snprintf(msg, sizeof(msg), "RECV (0 bytes)");
    } else {
        /* hidapi همیشه Report ID را در بایت ۰ برمی‌گرداند */
        int payload_len = got - 1;
        if (payload_len < 0) payload_len = 0;
        format_bytes(out_buf + 1, payload_len, disp, sizeof(disp));
        snprintf(msg, sizeof(msg), "GET (%d bytes): %s", payload_len, disp);
    }
    log_to_buffer(tab->input_buf, "FEAT", msg);
}

/* ---------- تحویل داده از thread به GTK ---------- */

static gboolean deliver_input(gpointer data) {
    ReadEvent *ev = (ReadEvent *)data;
    if (ev->len > 0)
        route_input_data(ev->data, ev->len, ev->has_report_id, "IN");
    free(ev);
    return G_SOURCE_REMOVE;
}

/* ---------- Thread خواندن ---------- */

static gpointer reader_thread_func(gpointer data) {
    App *a = (App *)data;
    unsigned char buf[MAX_READ_BUF];

    while (g_atomic_int_get(&a->reader_running)) {
        int n = hid_read_timeout(a->dev, buf, sizeof(buf), 100);
        if (n > 0) {
            ReadEvent *ev = malloc(sizeof(ReadEvent));
            if (!ev) continue;
            memcpy(ev->data, buf, n);
            ev->len = n;
            ev->has_report_id = a->has_report_id;
            g_idle_add(deliver_input, ev);
        } else if (n < 0) {
            /* خطای دستگاه — از حلقه خارج شو */
            break;
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

/* ---------- ساخت ویو و تب ---------- */

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

static GtkWidget *build_report_box(ReportBOX *rep_box, const char *label_text) {

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

    GtkWidget *info = gtk_label_new(label_text);
    gtk_widget_set_halign(info, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 0);

    const char *ph = is_hex_mode() ? "hex bytes e.g. 01 02 FF" : "text e.g. Hello";

    /* Input and Output and Feature */

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), 
            "Incoming(max %u bytes)  /  Outgoing(max %u bytes)  /  Feature(max %u bytes)", 
             rep_box->sizes.input_bytes, rep_box->sizes.output_bytes,rep_box->sizes.feature_bytes);
    GtkWidget *lbl = gtk_label_new(tmp);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

    GtkWidget *scrolled = make_terminal_view(&rep_box->input_view);
    rep_box->input_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(rep_box->input_view));
    g_object_set_data(G_OBJECT(rep_box->input_buf), "view", rep_box->input_view);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);

    /* Output Entry and Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);

    if (rep_box->sizes.output_bytes){
        rep_box->output_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(rep_box->output_entry), ph);
        g_signal_connect(rep_box->output_entry, "activate", G_CALLBACK(on_send_output), rep_box);
        gtk_box_pack_start(GTK_BOX(hbox), rep_box->output_entry, TRUE, TRUE, 0);

        GtkWidget *send = gtk_button_new_with_label("Send");
        gtk_widget_set_size_request(send, 120, -1);
        g_signal_connect(send, "clicked", G_CALLBACK(on_send_output), rep_box);
        gtk_box_pack_start(GTK_BOX(hbox), send, FALSE, FALSE, 0);
    }

    if (rep_box->sizes.input_bytes){
        GtkWidget *read_in_btn = gtk_button_new_with_label("Read");
        gtk_widget_set_size_request(read_in_btn,120, -1);
        g_signal_connect(read_in_btn, "clicked", G_CALLBACK(on_read_input), rep_box);
        gtk_box_pack_start(GTK_BOX(hbox), read_in_btn, FALSE, FALSE, 0);
    }

        /* Feature Entry and Buttons */
        GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_container_set_border_width(GTK_CONTAINER(hbox2), 4);
    if (rep_box->sizes.feature_bytes){


        rep_box->feature_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(rep_box->feature_entry), ph);
        gtk_box_pack_start(GTK_BOX(hbox2), rep_box->feature_entry, TRUE, TRUE, 0);

        GtkWidget *set_btn = gtk_button_new_with_label("Set Feature");
        gtk_widget_set_size_request(set_btn, 120, -1);
        g_signal_connect(set_btn, "clicked", G_CALLBACK(on_send_feature), rep_box);
        gtk_box_pack_start(GTK_BOX(hbox2), set_btn, FALSE, FALSE, 0);

        GtkWidget *get_btn = gtk_button_new_with_label("Get Feature");
        gtk_widget_set_size_request(get_btn, 120, -1);
        g_signal_connect(get_btn, "clicked", G_CALLBACK(on_get_feature), rep_box);
        gtk_box_pack_start(GTK_BOX(hbox2), get_btn, FALSE, FALSE, 0);
    }
    /* Pack all into main box */
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), hbox2, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(vbox), box, TRUE, TRUE, 0);

    return vbox;
}


/* clear_tabs: پس از gtk_notebook_remove_page ویجت به‌طور خودکار نابود می‌شود؛
 * بنابراین gtk_widget_destroy صدا زده نمی‌شود (double-destroy = CRITICAL). */
static void clear_tabs(void) {
    stop_reader_thread();

    if (!app.notebook) return;

    int n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app.notebook));
    for (int i = n - 1; i >= 0; i--) {
        GtkWidget *child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app.notebook), i);
        if (child) {
            ReportBOX *box = g_object_get_data(G_OBJECT(child), "report_box");
            g_object_set_data(G_OBJECT(child), "report_box", NULL);
            if (box) free(box);
            gtk_notebook_remove_page(GTK_NOTEBOOK(app.notebook), i);
            /* NO gtk_widget_destroy here! */
        }
    }
    app.tab_count = 0;
    memset(app.tabs, 0, sizeof(app.tabs));
}

static void clear_all_logs(void) {
    for (int i = 0; i < app.tab_count; i++) {
        ReportBOX *t = app.tabs[i];
        if (!t) continue;
        if (t->input_buf)   gtk_text_buffer_set_text(t->input_buf,   "", -1);
    }
}

static void build_tabs(const unsigned char *desc, int len) {
    clear_tabs();
    parse_report_descriptor(desc, len, app.main_sizes, &app.has_report_id);

    if (!app.has_report_id) {
        ReportBOX *tab = g_new0(ReportBOX, 1);
        tab->has_report_id = 0;
        tab->report_id = 0;
        tab->sizes = app.main_sizes[0];

        char label[256];
        snprintf(label, sizeof(label),
                "No Report ID   Input: %u B, Output: %u B, Feature: %u B",
                 tab->sizes.input_bytes, tab->sizes.output_bytes, tab->sizes.feature_bytes);

        GtkWidget *page = build_report_box(tab, label);
        g_object_set_data(G_OBJECT(page), "report_tab", tab);
        gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), page, gtk_label_new("Default"));
        gtk_widget_show_all(page);

        app.tabs[0] = tab;
        app.tab_count = 1;
        return;
    }

    int idx = 0;
    for (int id = 0; id < MAX_REPORT_ID && idx < MAX_REPORT_ID; id++) {
        ReportSizes *s = &app.main_sizes[id];
        if (!s->input_bytes && !s->output_bytes && !s->feature_bytes) continue;

        ReportBOX *tab = g_new0(ReportBOX, 1);
        tab->has_report_id = 1;
        tab->report_id = (unsigned char)id;
        tab->sizes = *s;

        char label[256];
        snprintf(label, sizeof(label),"Report ID 0x%02X   Input: %u B, Output: %u B, Feature: %u B",id, s->input_bytes, s->output_bytes, s->feature_bytes);

        GtkWidget *page = build_report_box(tab, label);
        g_object_set_data(G_OBJECT(page), "report_tab", tab);

        char tab_text[32];
        snprintf(tab_text, sizeof(tab_text), "Report ID:0x%02X", id);
        gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), page, gtk_label_new(tab_text));
        gtk_widget_show_all(page);

        app.tabs[idx++] = tab;
    }
    app.tab_count = idx;
}

/* ---------- دستگاه ---------- */

static void populate_devices(void) {
    GtkListStore *store = gtk_list_store_new(1, G_TYPE_STRING);
    GtkTreeIter iter;

    clear_devices();

    struct hid_device_info *devs = hid_enumerate(0, 0);
    for (struct hid_device_info *cur = devs; cur && app.device_count < MAX_DEVICES; cur = cur->next) {
        char label[512];
        snprintf(label, sizeof(label), "%04x:%04x  %ls %ls",
                 cur->vendor_id, cur->product_id,
                 cur->manufacturer_string ? cur->manufacturer_string : L"-",
                 cur->product_string ? cur->product_string : L"-");

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, label, -1);
        app.device_paths[app.device_count++] = cur->path ? strdup(cur->path) : NULL;
    }
    hid_free_enumeration(devs);

    gtk_combo_box_set_model(GTK_COMBO_BOX(app.device_combo), GTK_TREE_MODEL(store));
    if (app.device_count > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(app.device_combo), 0);
    g_object_unref(store);

    char msg[64];
    if (app.device_count)
        snprintf(msg, sizeof(msg), "Found %d device(s)", app.device_count);
    else
        snprintf(msg, sizeof(msg), "No HID devices found");
    gtk_label_set_text(GTK_LABEL(app.status_label), msg);

    if (app.connect_button)
        gtk_widget_set_sensitive(app.connect_button, app.device_count > 0);
}

static void on_refresh_clicked(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    if (app.connected) return;
    populate_devices();
}

static void disconnect_device(void) {
    stop_reader_thread();
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    app.connected = 0;
    clear_tabs();

    gtk_button_set_label(GTK_BUTTON(app.connect_button), "Connect");
    gtk_widget_set_sensitive(app.device_combo, TRUE);
    gtk_widget_set_sensitive(app.refresh_button, TRUE);
    gtk_widget_set_sensitive(app.connect_button, app.device_count > 0);

    gtk_label_set_text(GTK_LABEL(app.status_label), "Disconnected");
}

static void connect_device(void) {
    int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app.device_combo));
    if (sel < 0 || sel >= app.device_count) {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Select a device first");
        return;
    }
    if (!app.device_paths[sel]) {
        gtk_label_set_text(GTK_LABEL(app.status_label), "Selected device has no path");
        return;
    }

    if (app.dev) { hid_close(app.dev); app.dev = NULL; }

    hid_device *dev = hid_open_path(app.device_paths[sel]);
    if (!dev) {
        gtk_label_set_text(GTK_LABEL(app.status_label),
                           "Failed to open device (permissions? already in use?)");
        return;
    }

    hid_set_nonblocking(dev, 0);

    unsigned char desc[MAX_DESCRIPTOR_SIZE];
    int n = hid_get_report_descriptor(dev, desc, sizeof(desc));
    if (n < 0) {
        hid_close(dev);
        gtk_label_set_text(GTK_LABEL(app.status_label), "Failed to read Report Descriptor");
        return;
    }

    app.dev = dev;
    app.connected = 1;
    app.has_report_id = 0;

    build_tabs(desc, n);

    g_atomic_int_set(&app.reader_running, 1);
    app.reader_thread = g_thread_new("hid_reader", reader_thread_func, &app);
    if (!app.reader_thread) {
        g_atomic_int_set(&app.reader_running, 0);
        hid_close(app.dev);
        app.dev = NULL;
        app.connected = 0;
        clear_tabs();
        gtk_label_set_text(GTK_LABEL(app.status_label), "Failed to start reader thread");
        gtk_widget_set_sensitive(app.device_combo, TRUE);
        gtk_widget_set_sensitive(app.refresh_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(app.connect_button), "Connect");
        return;
    }

    gtk_button_set_label(GTK_BUTTON(app.connect_button), "Disconnect");
    gtk_widget_set_sensitive(app.device_combo, FALSE);
    gtk_widget_set_sensitive(app.refresh_button, FALSE);

    char status[160];
    snprintf(status, sizeof(status), "Connected. Descriptor: %d bytes, %d tab(s)", n, app.tab_count);
    gtk_label_set_text(GTK_LABEL(app.status_label), status);
}

static void on_connect_clicked(GtkButton *b, gpointer d) {
    (void)b; (void)d;
    if (app.connected) disconnect_device();
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
    const char *ph = hex ? "hex bytes e.g. 01 02 FF" : "text e.g. Hello";

    for (int i = 0; i < app.tab_count; i++) {
        ReportBOX *t = app.tabs[i];
        if (!t) continue;
        if (t->output_entry)
            gtk_entry_set_placeholder_text(GTK_ENTRY(t->output_entry), ph);
        if (t->feature_entry)
            gtk_entry_set_placeholder_text(GTK_ENTRY(t->feature_entry), ph);
    }

    gtk_label_set_text(GTK_LABEL(app.status_label), hex ? "Mode: HEX" : "Mode: ASCII");
}

/* ---------- window destroy handler ---------- */

static void on_window_destroy(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    if (g_atomic_int_get(&app.shutting_down)) return;
    g_atomic_int_set(&app.shutting_down, 1);

    stop_reader_thread();
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    app.connected = 0;
    clear_tabs();       /* ویجت‌ها هنوز معتبرند */
    clear_devices();

    gtk_main_quit();
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    if (hid_init() != 0) {
        g_printerr("Failed to initialize hidapi\n");
        return 1;
    }

    gtk_init(&argc, &argv);

    GError *err = NULL;
    GtkBuilder *builder = gtk_builder_new();
    if (!gtk_builder_add_from_file(builder, "window1.glade", &err)) {
        g_printerr("Failed to load window1.glade: %s\n", err->message);
        g_error_free(err);
        g_object_unref(builder);
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

    if (!app.window || !app.device_combo || !app.notebook || !app.status_label ||
        !app.hex_check || !app.refresh_button || !app.connect_button || !app.clear_button) {
        g_printerr("Error: Failed to find required widgets in glade file\n");
        g_object_unref(builder);
        hid_exit();
        return 1;
    }

    GtkCellRenderer *rend = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(app.device_combo), rend, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(app.device_combo), rend, "text", 0, NULL);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.hex_check), FALSE);

    g_signal_connect(app.refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), NULL);
    g_signal_connect(app.connect_button, "clicked", G_CALLBACK(on_connect_clicked), NULL);
    g_signal_connect(app.clear_button,   "clicked", G_CALLBACK(on_clear_clicked), NULL);
    g_signal_connect(app.hex_check,      "toggled", G_CALLBACK(on_hex_toggled), NULL);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    g_object_unref(builder);

    populate_devices();
    gtk_widget_show_all(app.window);
    gtk_main();

    /* پس از gtk_main ویجت‌ها نابود شده‌اند؛ فقط منابع غیر-GTK را آزاد می‌کنیم. */
    if (app.dev) { hid_close(app.dev); app.dev = NULL; }
    clear_devices();
    hid_exit();
    return 0;
}
