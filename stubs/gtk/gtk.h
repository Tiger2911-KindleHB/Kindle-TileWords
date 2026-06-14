#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int gboolean;
typedef void* gpointer;
typedef char gchar;
typedef unsigned long gulong;
typedef void (*GCallback)(void);

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkContainer GtkContainer;
typedef struct _GdkWindow GdkWindow;
typedef struct _GdkEvent GdkEvent;
typedef struct _GdkEventExpose GdkEventExpose;

struct _GtkWidget { char private_opaque[1]; };
struct _GtkWindow { char private_opaque[1]; };
struct _GtkContainer { char private_opaque[1]; };
struct _GdkWindow { char private_opaque[1]; };
struct _GdkEvent { char private_opaque[1]; };
struct _GdkEventExpose { char private_opaque[1]; };

typedef struct {
    int x;
    int y;
    int width;
    int height;
} GtkAllocation;

typedef struct {
    int type;
    void* window;
    signed char send_event;
    unsigned char _pad[3];
    unsigned int time;
    double x;
    double y;
} GdkEventButton;

#define TRUE 1
#define FALSE 0
#define GTK_WINDOW_TOPLEVEL 0
#define GDK_BUTTON_PRESS_MASK (1 << 8)
#define GDK_BUTTON_PRESS 4

#define G_OBJECT(x) ((void*)(x))
#define GTK_WINDOW(x) ((GtkWindow*)(x))
#define GTK_CONTAINER(x) ((GtkContainer*)(x))
#define G_CALLBACK(f) ((GCallback)(f))

#ifdef __cplusplus
}
#endif
