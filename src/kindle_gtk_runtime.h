#pragma once

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

struct TileWordsGtkRuntime {
    void* gtk = nullptr;
    void* gdk = nullptr;
    void* gobject = nullptr;
    void* cairo = nullptr;

    void (*gtk_init)(int*, char***) = nullptr;
    GtkWidget* (*gtk_window_new)(int) = nullptr;
    void (*gtk_window_set_title)(GtkWindow*, const char*) = nullptr;
    void (*gtk_window_set_decorated)(GtkWindow*, int) = nullptr;
    void (*gtk_window_fullscreen)(GtkWindow*) = nullptr;
    void (*gtk_window_set_keep_above)(GtkWindow*, int) = nullptr;
    GtkWidget* (*gtk_drawing_area_new)(void) = nullptr;
    void (*gtk_widget_add_events)(GtkWidget*, int) = nullptr;
    void (*gtk_container_add)(GtkContainer*, GtkWidget*) = nullptr;
    void (*gtk_widget_show_all)(GtkWidget*) = nullptr;
    void (*gtk_window_present)(GtkWindow*) = nullptr;
    void (*gtk_main)(void) = nullptr;
    void (*gtk_main_quit)(void) = nullptr;
    void (*gtk_widget_get_allocation)(GtkWidget*, GtkAllocation*) = nullptr;
    void (*gtk_widget_queue_draw)(GtkWidget*) = nullptr;
    GdkWindow* (*gtk_widget_get_window)(GtkWidget*) = nullptr;

    unsigned long (*g_signal_connect_data)(void*, const char*, GCallback, void*, void*, int) = nullptr;
    cairo_t* (*gdk_cairo_create)(void*) = nullptr;

    void (*cairo_destroy)(cairo_t*) = nullptr;
    void (*cairo_set_source_rgb)(cairo_t*, double, double, double) = nullptr;
    void (*cairo_rectangle)(cairo_t*, double, double, double, double) = nullptr;
    void (*cairo_fill)(cairo_t*) = nullptr;
    void (*cairo_set_line_width)(cairo_t*, double) = nullptr;
    void (*cairo_stroke)(cairo_t*) = nullptr;
    void (*cairo_select_font_face)(cairo_t*, const char*, int, int) = nullptr;
    void (*cairo_set_font_size)(cairo_t*, double) = nullptr;
    void (*cairo_move_to)(cairo_t*, double, double) = nullptr;
    void (*cairo_show_text)(cairo_t*, const char*) = nullptr;
    void (*cairo_text_extents)(cairo_t*, const char*, cairo_text_extents_t*) = nullptr;
};

static TileWordsGtkRuntime tw_gtk;

static inline void tw_set_error(char* err, size_t err_len, const char* msg) {
    if (err && err_len) {
        std::snprintf(err, err_len, "%s", msg ? msg : "unknown GTK runtime error");
    }
}

static inline void* tw_dlopen_any(const char* const* names) {
    for (int i = 0; names[i]; ++i) {
        void* h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) return h;
    }
    return nullptr;
}

static inline bool tw_load_symbol(void* handle, const char* name, void** out, char* err, size_t err_len) {
    dlerror();
    void* p = dlsym(handle, name);
    const char* dl_err = dlerror();
    if (!p || dl_err) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "missing GTK runtime symbol: %s", name);
        tw_set_error(err, err_len, buf);
        return false;
    }
    *out = p;
    return true;
}

#define TW_SYM(handle, member, symbol) \
    do { \
        void* p__ = nullptr; \
        if (!tw_load_symbol((handle), (symbol), &p__, err, err_len)) return false; \
        tw_gtk.member = reinterpret_cast<decltype(tw_gtk.member)>(p__); \
    } while (0)

static inline bool tw_load_gtk_runtime(char* err, size_t err_len) {
    if (tw_gtk.gtk) return true;

    const char* gtk_names[] = {"libgtk-x11-2.0.so.0", "libgtk-x11-2.0.so", nullptr};
    const char* gdk_names[] = {"libgdk-x11-2.0.so.0", "libgdk-x11-2.0.so", nullptr};
    const char* gobject_names[] = {"libgobject-2.0.so.0", "libgobject-2.0.so", nullptr};
    const char* cairo_names[] = {"libcairo.so.2", "libcairo.so", nullptr};

    tw_gtk.gtk = tw_dlopen_any(gtk_names);
    if (!tw_gtk.gtk) { tw_set_error(err, err_len, "could not load libgtk-x11-2.0"); return false; }
    tw_gtk.gdk = tw_dlopen_any(gdk_names);
    if (!tw_gtk.gdk) { tw_set_error(err, err_len, "could not load libgdk-x11-2.0"); return false; }
    tw_gtk.gobject = tw_dlopen_any(gobject_names);
    if (!tw_gtk.gobject) { tw_set_error(err, err_len, "could not load libgobject-2.0"); return false; }
    tw_gtk.cairo = tw_dlopen_any(cairo_names);
    if (!tw_gtk.cairo) { tw_set_error(err, err_len, "could not load libcairo"); return false; }

    TW_SYM(tw_gtk.gtk, gtk_init, "gtk_init");
    TW_SYM(tw_gtk.gtk, gtk_window_new, "gtk_window_new");
    TW_SYM(tw_gtk.gtk, gtk_window_set_title, "gtk_window_set_title");
    TW_SYM(tw_gtk.gtk, gtk_window_set_decorated, "gtk_window_set_decorated");
    TW_SYM(tw_gtk.gtk, gtk_window_fullscreen, "gtk_window_fullscreen");
    TW_SYM(tw_gtk.gtk, gtk_window_set_keep_above, "gtk_window_set_keep_above");
    TW_SYM(tw_gtk.gtk, gtk_drawing_area_new, "gtk_drawing_area_new");
    TW_SYM(tw_gtk.gtk, gtk_widget_add_events, "gtk_widget_add_events");
    TW_SYM(tw_gtk.gtk, gtk_container_add, "gtk_container_add");
    TW_SYM(tw_gtk.gtk, gtk_widget_show_all, "gtk_widget_show_all");
    TW_SYM(tw_gtk.gtk, gtk_window_present, "gtk_window_present");
    TW_SYM(tw_gtk.gtk, gtk_main, "gtk_main");
    TW_SYM(tw_gtk.gtk, gtk_main_quit, "gtk_main_quit");
    TW_SYM(tw_gtk.gtk, gtk_widget_get_allocation, "gtk_widget_get_allocation");
    TW_SYM(tw_gtk.gtk, gtk_widget_queue_draw, "gtk_widget_queue_draw");
    TW_SYM(tw_gtk.gtk, gtk_widget_get_window, "gtk_widget_get_window");

    TW_SYM(tw_gtk.gobject, g_signal_connect_data, "g_signal_connect_data");
    TW_SYM(tw_gtk.gdk, gdk_cairo_create, "gdk_cairo_create");

    TW_SYM(tw_gtk.cairo, cairo_destroy, "cairo_destroy");
    TW_SYM(tw_gtk.cairo, cairo_set_source_rgb, "cairo_set_source_rgb");
    TW_SYM(tw_gtk.cairo, cairo_rectangle, "cairo_rectangle");
    TW_SYM(tw_gtk.cairo, cairo_fill, "cairo_fill");
    TW_SYM(tw_gtk.cairo, cairo_set_line_width, "cairo_set_line_width");
    TW_SYM(tw_gtk.cairo, cairo_stroke, "cairo_stroke");
    TW_SYM(tw_gtk.cairo, cairo_select_font_face, "cairo_select_font_face");
    TW_SYM(tw_gtk.cairo, cairo_set_font_size, "cairo_set_font_size");
    TW_SYM(tw_gtk.cairo, cairo_move_to, "cairo_move_to");
    TW_SYM(tw_gtk.cairo, cairo_show_text, "cairo_show_text");
    TW_SYM(tw_gtk.cairo, cairo_text_extents, "cairo_text_extents");

    return true;
}

#undef TW_SYM

static inline unsigned long tw_g_signal_connect(void* instance, const char* detailed_signal, GCallback handler, void* data) {
    return tw_gtk.g_signal_connect_data(instance, detailed_signal, handler, data, nullptr, 0);
}

#define gtk_init tw_gtk.gtk_init
#define gtk_window_new tw_gtk.gtk_window_new
#define gtk_window_set_title tw_gtk.gtk_window_set_title
#define gtk_window_set_decorated tw_gtk.gtk_window_set_decorated
#define gtk_window_fullscreen tw_gtk.gtk_window_fullscreen
#define gtk_window_set_keep_above tw_gtk.gtk_window_set_keep_above
#define gtk_drawing_area_new tw_gtk.gtk_drawing_area_new
#define gtk_widget_add_events tw_gtk.gtk_widget_add_events
#define gtk_container_add tw_gtk.gtk_container_add
#define gtk_widget_show_all tw_gtk.gtk_widget_show_all
#define gtk_window_present tw_gtk.gtk_window_present
#define gtk_main tw_gtk.gtk_main
#define gtk_main_quit tw_gtk.gtk_main_quit
#define gtk_widget_get_allocation tw_gtk.gtk_widget_get_allocation
#define gtk_widget_queue_draw tw_gtk.gtk_widget_queue_draw
#define gtk_widget_get_window tw_gtk.gtk_widget_get_window
#define g_signal_connect tw_g_signal_connect
#define gdk_cairo_create tw_gtk.gdk_cairo_create
#define cairo_destroy tw_gtk.cairo_destroy
#define cairo_set_source_rgb tw_gtk.cairo_set_source_rgb
#define cairo_rectangle tw_gtk.cairo_rectangle
#define cairo_fill tw_gtk.cairo_fill
#define cairo_set_line_width tw_gtk.cairo_set_line_width
#define cairo_stroke tw_gtk.cairo_stroke
#define cairo_select_font_face tw_gtk.cairo_select_font_face
#define cairo_set_font_size tw_gtk.cairo_set_font_size
#define cairo_move_to tw_gtk.cairo_move_to
#define cairo_show_text tw_gtk.cairo_show_text
#define cairo_text_extents tw_gtk.cairo_text_extents
