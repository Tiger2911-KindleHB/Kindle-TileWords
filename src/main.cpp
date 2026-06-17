#include <gtk/gtk.h>
#include <cairo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <csignal>
#include <dirent.h>
#include <sys/stat.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

static std::string g_log_path;

static void app_log(const std::string& msg) {
    if (g_log_path.empty()) return;
    std::ofstream f(g_log_path, std::ios::app);
    if (f) f << std::time(nullptr) << " " << msg << "\n";
}

static void app_log_signal(int sig) {
    if (!g_log_path.empty()) {
        std::ofstream f(g_log_path, std::ios::app);
        if (f) f << std::time(nullptr) << " fatal signal " << sig << "\n";
    }
    std::_Exit(128 + sig);
}

constexpr int BOARD_N = 15;
constexpr int RACK_N = 7;
constexpr int MAX_PLAYERS = 4;
constexpr int BINGO_BONUS = 50;

struct Rect { int x = 0, y = 0, w = 0, h = 0; };
static bool in_rect(const Rect& r, int px, int py) {
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

static bool rect_intersects(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static Rect rect_intersection(const Rect& a, const Rect& b) {
    int x1 = std::max(a.x, b.x);
    int y1 = std::max(a.y, b.y);
    int x2 = std::min(a.x + a.w, b.x + b.w);
    int y2 = std::min(a.y + a.h, b.y + b.h);
    if (x2 <= x1 || y2 <= y1) return Rect{};
    return {x1, y1, x2 - x1, y2 - y1};
}

struct Tile { char ch = 0; bool blank = false; };
struct Cell { char ch = 0; bool locked = false; bool blank = false; int rack_index = -1; };

enum class Premium { None, DL, TL, DW, TW, Star };
enum class Mode { Normal, Handoff, Invalid, NewSetup, Exchange, BlankSelect, GameOver, Settings };

struct MoveResult {
    bool ok = false;
    int score = 0;
    std::string message;
    std::string word;
};

struct Game {
    std::array<std::array<Cell, BOARD_N>, BOARD_N> board{};
    std::array<std::array<Premium, BOARD_N>, BOARD_N> premiums{};
    std::array<std::array<Tile, RACK_N>, MAX_PLAYERS> racks{};
    std::array<int, MAX_PLAYERS> rack_count{{0,0,0,0}};
    std::vector<Tile> bag;
    std::array<int, MAX_PLAYERS> scores{{0,0,0,0}};
    std::array<bool, MAX_PLAYERS> cpu{{false,false,false,false}};
    int player_count = 2;
    int setup_player_count = 2;
    std::array<bool, MAX_PLAYERS> setup_cpu{{false,false,false,false}};
    int current = 0;
    int selected_rack = -1;
    int pass_count = 0;
    bool game_over = false;
    bool ai_enabled = false;
    int tile_scale = 100;
    int board_cell_tune = 0; // per-cell board size adjustment saved per device
    int tile_limit_option = 100;
    int last_turn_player = -1;
    int last_turn_score = 0;
    std::string last_turn_word;
    bool procedural_board = false;
    bool setup_procedural_board = false;
    int starter_letters_option = 0;
    Mode mode = Mode::Normal;
    std::array<bool, RACK_N> exchange_selected{{false,false,false,false,false,false,false}};
    int blank_row = -1;
    int blank_col = -1;
    std::string invalid_message;
};

struct Layout {
    Rect top_exit;
    Rect top_settings;
    Rect top_new;
    Rect board;
    int cell = 0;
    std::array<Rect, RACK_N> rack{};
    Rect btn_submit;
    Rect btn_pass;
    Rect btn_exchange;
    Rect btn_shuffle;
    Rect btn_value;
    Rect btn_settings_ai;
    Rect btn_settings_minus;
    Rect btn_settings_plus;
    Rect btn_settings_grid_value;
    Rect btn_settings_placeholder1;
    Rect btn_settings_placeholder2;
    Rect btn_settings_back;
    Rect confirm_button;
    Rect popup_yes;
    Rect popup_no;
    Rect setup_players;
    std::array<Rect, MAX_PLAYERS> setup_player_type{};
    Rect setup_tile_limit;
    Rect setup_board_size;
    Rect setup_custom_board;
    Rect setup_start;
    Rect setup_cancel;
};

class TileWordsApp {
public:
    bool init(int argc, char** argv);
    int run();

private:
    GtkWidget* window_ = nullptr;
    Game game_;
    Layout layout_;
    std::set<std::string> dictionary_;
    std::string home_;
    std::string save_path_;
    std::string dict_path_;
    std::vector<Rect> dirty_rects_;
    bool full_redraw_pending_ = true;
    int last_logged_w_ = -1;
    int last_logged_h_ = -1;
    int last_logged_cell_ = -1;
    int last_logged_tune_ = 9999;

    static gboolean on_draw(GtkWidget* widget, GdkEventExpose* event, gpointer data);
    static gboolean on_button(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean on_key(GtkWidget* widget, GdkEventKey* event, gpointer data);
    static gboolean on_focus_out(GtkWidget* widget, GdkEventFocus* event, gpointer data);
    static gboolean on_visibility(GtkWidget* widget, GdkEventVisibility* event, gpointer data);
    static gboolean on_delete(GtkWidget* widget, GdkEvent* event, gpointer data);
    static gboolean on_raise_timer(gpointer data);

    void compute_layout(int w, int h);
    void log_grid_metrics(const char* reason, int w, int h);
    void draw(cairo_t* cr, int w, int h);
    void draw_normal(cairo_t* cr, int w, int h);
    void draw_handoff(cairo_t* cr, int w, int h);
    void draw_invalid(cairo_t* cr, int w, int h);
    void draw_new_setup(cairo_t* cr, int w, int h);
    void draw_exchange(cairo_t* cr, int w, int h);
    void draw_blank_select(cairo_t* cr, int w, int h);
    void draw_game_over(cairo_t* cr, int w, int h);
    void draw_settings(cairo_t* cr, int w, int h);

    void touch(int x, int y);
    void touch_normal(int x, int y);
    void touch_handoff(int x, int y);
    void touch_exchange(int x, int y);
    void touch_blank_select(int x, int y);
    void touch_new_setup(int x, int y);
    void touch_settings(int x, int y);

    void queue_draw();
    void queue_draw_rect(const Rect& r);
    void queue_draw_rack();
    Rect board_cell_rect(int row, int col) const;
    Premium cell_premium(int row, int col) const;
    void setup_premiums();
    void place_starter_letters();
    bool can_place_starter_at(int row, int col) const;
    void quit();
    void request_sleep();
    void return_unsubmitted_tiles_to_rack();
    void new_game(bool reset_scores = true);
    void load_dictionary();
    void save_game();
    bool load_game();
    void refill_rack(int player);
    Tile draw_tile();
    void shuffle_rack();
    void pass_turn();
    void exchange_tiles();
    int next_player_index() const;
    void advance_to_next_player();
    void enter_handoff();
    int preview_score() const;
    void reject(const std::string& msg);
    void submit_move();
    MoveResult validate_and_score();
    void lock_placed_and_score(int score, const std::string& word);
    void return_temp_tile(int row, int col);
    int locked_count() const;
    std::vector<std::pair<int,int>> placed_cells() const;
    std::string word_at(int row, int col, int dr, int dc) const;
    int score_word(const std::vector<std::pair<int,int>>& cells) const;
    bool is_dictionary_word(const std::string& word) const;
};

static std::string upper_word(std::string s) {
    std::string out;
    bool started = false;
    for (unsigned char uc : s) {
        if (std::isalpha(uc)) {
            out.push_back(static_cast<char>(std::toupper(uc)));
            started = true;
        } else if (started) {
            break;
        }
    }
    return out;
}

static std::string lower_string(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}


static std::string pad3(int n) {
    if (n < 0) n = 0;
    if (n < 10) return "00" + std::to_string(n);
    if (n < 100) return "0" + std::to_string(n);
    return std::to_string(n);
}

static bool ends_with_ci(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return lower_string(s.substr(s.size() - suffix.size())) == lower_string(suffix);
}

static bool file_exists_readable(const std::string& path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
}


static int letter_value(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case 'A': case 'E': case 'I': case 'O': case 'U': case 'L': case 'N': case 'S': case 'T': case 'R': return 1;
        case 'D': case 'G': return 2;
        case 'B': case 'C': case 'M': case 'P': return 3;
        case 'F': case 'H': case 'V': case 'W': case 'Y': return 4;
        case 'K': return 5;
        case 'J': case 'X': return 8;
        case 'Q': case 'Z': return 10;
        default: return 0;
    }
}

static Premium standard_premium_at(int r, int c) {
    static const int tw[][2] = {{0,0},{0,7},{0,14},{7,0},{7,14},{14,0},{14,7},{14,14}};
    static const int dw[][2] = {{1,1},{2,2},{3,3},{4,4},{10,10},{11,11},{12,12},{13,13},{1,13},{2,12},{3,11},{4,10},{10,4},{11,3},{12,2},{13,1}};
    static const int tl[][2] = {{1,5},{1,9},{5,1},{5,5},{5,9},{5,13},{9,1},{9,5},{9,9},{9,13},{13,5},{13,9}};
    static const int dl[][2] = {{0,3},{0,11},{2,6},{2,8},{3,0},{3,7},{3,14},{6,2},{6,6},{6,8},{6,12},{7,3},{7,11},{8,2},{8,6},{8,8},{8,12},{11,0},{11,7},{11,14},{12,6},{12,8},{14,3},{14,11}};
    if (r == 7 && c == 7) return Premium::Star;
    for (auto& p : tw) if (p[0] == r && p[1] == c) return Premium::TW;
    for (auto& p : dw) if (p[0] == r && p[1] == c) return Premium::DW;
    for (auto& p : tl) if (p[0] == r && p[1] == c) return Premium::TL;
    for (auto& p : dl) if (p[0] == r && p[1] == c) return Premium::DL;
    return Premium::None;
}


static char premium_to_char(Premium p) {
    switch (p) {
        case Premium::DL: return 'L';
        case Premium::TL: return 'T';
        case Premium::DW: return 'W';
        case Premium::TW: return 'Q';
        case Premium::Star: return 'S';
        case Premium::None: default: return '.';
    }
}

static Premium premium_from_char(char ch) {
    switch (ch) {
        case 'L': return Premium::DL;
        case 'T': return Premium::TL;
        case 'W': return Premium::DW;
        case 'Q': return Premium::TW;
        case 'S': return Premium::Star;
        default: return Premium::None;
    }
}

static void set_gray(cairo_t* cr, double g) { cairo_set_source_rgb(cr, g, g, g); }

static void fill_rect(cairo_t* cr, const Rect& r, double gray) {
    set_gray(cr, gray);
    cairo_rectangle(cr, r.x, r.y, r.w, r.h);
    cairo_fill(cr);
}

static void stroke_rect(cairo_t* cr, const Rect& r, double gray, double width = 2.0) {
    set_gray(cr, gray);
    cairo_set_line_width(cr, width);
    cairo_rectangle(cr, r.x + width / 2.0, r.y + width / 2.0, r.w - width, r.h - width);
    cairo_stroke(cr);
}

static void text(cairo_t* cr, int x, int y, const std::string& s, double size, double gray = 0.0, bool bold = false) {
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    set_gray(cr, gray);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s.c_str());
}

static void centered_text(cairo_t* cr, const Rect& r, const std::string& s, double size, double gray = 0.0, bool bold = false) {
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext{};
    cairo_text_extents(cr, s.c_str(), &ext);
    set_gray(cr, gray);
    double x = r.x + (r.w - ext.width) / 2.0 - ext.x_bearing;
    double y = r.y + (r.h - ext.height) / 2.0 - ext.y_bearing;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, s.c_str());
}

static void draw_tile_face(cairo_t* cr, const Rect& r, char ch, int value, bool inverted, double scale_percent) {
    (void)scale_percent;
    fill_rect(cr, r, inverted ? 0.0 : 1.0);
    stroke_rect(cr, r, 0.0, 3.0);
    if (!ch) return;

    double fg = inverted ? 1.0 : 0.0;
    double base = std::min(r.w, r.h);
    double letter_size = std::max(24.0, base * 0.58);
    double value_size = std::max(16.0, base * 0.30);

    std::string letter(1, ch);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, letter_size);
    cairo_text_extents_t le{};
    cairo_text_extents(cr, letter.c_str(), &le);
    set_gray(cr, fg);
    double lx = r.x + (r.w - le.width) / 2.0 - le.x_bearing;
    double ly = r.y + (r.h - le.height) / 2.0 - le.y_bearing;
    cairo_move_to(cr, lx, ly);
    cairo_show_text(cr, letter.c_str());

    if (value > 0) {
        std::string val = std::to_string(value);
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, value_size);
        cairo_text_extents_t ve{};
        cairo_text_extents(cr, val.c_str(), &ve);
        double margin = std::max(3.0, base * 0.06);
        double vx = r.x + r.w - margin - ve.width - ve.x_bearing;
        double vy = r.y + r.h - margin - (ve.y_bearing + ve.height);
        cairo_move_to(cr, vx, vy);
        cairo_show_text(cr, val.c_str());
    }
}


static void draw_button(cairo_t* cr, const Rect& r, const std::string& label, bool inverted = false) {
    fill_rect(cr, r, inverted ? 0.0 : 1.0);
    stroke_rect(cr, r, 0.0, 3.0);
    if (label.empty()) return;

    double fg = inverted ? 1.0 : 0.0;
    double size = std::max(16.0, std::min(32.0, r.h * 0.42));
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_text_extents_t ext{};
    do {
        cairo_set_font_size(cr, size);
        cairo_text_extents(cr, label.c_str(), &ext);
        if (ext.width <= std::max(8, r.w - 14) && ext.height <= std::max(8, r.h - 10)) break;
        size -= 1.0;
    } while (size > 10.0);

    set_gray(cr, fg);
    double x = r.x + (r.w - ext.width) / 2.0 - ext.x_bearing;
    double y = r.y + (r.h - ext.height) / 2.0 - ext.y_bearing;
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, label.c_str());
}
bool TileWordsApp::init(int argc, char** argv) {
    const char* env_home = std::getenv("TILEWORDS_HOME");
    home_ = env_home && *env_home ? env_home : "/mnt/us/extensions/tilewords";
    save_path_ = home_ + "/data/save.json";
    dict_path_ = home_ + "/data/dictionary.txt";
    g_log_path = home_ + "/data/app.log";

    std::signal(SIGSEGV, app_log_signal);
    std::signal(SIGABRT, app_log_signal);
    std::signal(SIGBUS, app_log_signal);
    std::signal(SIGILL, app_log_signal);

    app_log("constructor: before gtk_init");
    if (!gtk_init_check(&argc, &argv)) {
        app_log("constructor: gtk_init_check failed");
        return false;
    }
    app_log("constructor: after gtk_init");

    window_ = gtk_window_new(GTK_WINDOW_POPUP);
    if (!window_) {
        app_log("constructor: window creation failed");
        return false;
    }
    app_log("constructor: popup window created");

    gtk_window_set_title(GTK_WINDOW(window_), "TileWords");
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    int screen_w = gdk_screen_width();
    int screen_h = gdk_screen_height();
    gtk_widget_set_size_request(window_, screen_w, screen_h);
    gtk_window_set_default_size(GTK_WINDOW(window_), screen_w, screen_h);
    gtk_window_resize(GTK_WINDOW(window_), screen_w, screen_h);
    gtk_window_move(GTK_WINDOW(window_), 0, 0);
    gtk_window_fullscreen(GTK_WINDOW(window_));
    gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window_), TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window_), TRUE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window_), TRUE);
    gtk_widget_set_app_paintable(window_, TRUE);
    gtk_widget_set_double_buffered(window_, FALSE);
    gtk_widget_add_events(window_, static_cast<GdkEventMask>(
        GDK_EXPOSURE_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_FOCUS_CHANGE_MASK |
        GDK_VISIBILITY_NOTIFY_MASK |
        GDK_STRUCTURE_MASK));

    g_signal_connect(G_OBJECT(window_), "expose-event", G_CALLBACK(TileWordsApp::on_draw), this);
    g_signal_connect(G_OBJECT(window_), "button-press-event", G_CALLBACK(TileWordsApp::on_button), this);
    g_signal_connect(G_OBJECT(window_), "key-press-event", G_CALLBACK(TileWordsApp::on_key), this);
    g_signal_connect(G_OBJECT(window_), "focus-out-event", G_CALLBACK(TileWordsApp::on_focus_out), this);
    g_signal_connect(G_OBJECT(window_), "visibility-notify-event", G_CALLBACK(TileWordsApp::on_visibility), this);
    g_signal_connect(G_OBJECT(window_), "delete-event", G_CALLBACK(TileWordsApp::on_delete), this);

    load_dictionary();
    if (!load_game()) new_game(true);

    return true;
}

int TileWordsApp::run() {
    app_log("run: show window");
    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    GdkWindow* gdk_window = gtk_widget_get_window(window_);
    if (gdk_window) {
        gdk_window_set_override_redirect(gdk_window, TRUE);
        gdk_window_move_resize(gdk_window, 0, 0, gdk_screen_width(), gdk_screen_height());
        gdk_window_show(gdk_window);
        gdk_window_raise(gdk_window);
        gdk_window_set_events(gdk_window, static_cast<GdkEventMask>(gdk_window_get_events(gdk_window) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_KEY_PRESS_MASK | GDK_FOCUS_CHANGE_MASK | GDK_VISIBILITY_NOTIFY_MASK | GDK_STRUCTURE_MASK));
        app_log("input: configured popup override-redirect window event mask");
    } else {
        app_log("input: missing gdk window after show");
    }
    gtk_widget_grab_focus(window_);
    app_log("run: popup shown without redraw timers");
    app_log("run: enter gtk_main");
    gtk_main();
    app_log("run: gtk_main returned");
    return 0;
}

gboolean TileWordsApp::on_draw(GtkWidget* widget, GdkEventExpose* event, gpointer data) {
    static bool first = true;
    auto* app = static_cast<TileWordsApp*>(data);
    GdkWindow* win = gtk_widget_get_window(widget);
    if (!win) { app_log("draw: missing gdk window"); return TRUE; }

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    if (alloc.width <= 0 || alloc.height <= 0) {
        alloc.width = gdk_screen_width();
        alloc.height = gdk_screen_height();
    }
    app->compute_layout(alloc.width, alloc.height);

    Rect expose{0, 0, alloc.width, alloc.height};
    if (event) expose = {event->area.x, event->area.y, event->area.width, event->area.height};

    std::vector<Rect> paint_rects;
    if (first || app->full_redraw_pending_) {
        paint_rects.push_back(expose);
        if (first) { app_log("draw: first expose"); first = false; }
        app->full_redraw_pending_ = false;
        app->dirty_rects_.clear();
    } else if (!app->dirty_rects_.empty()) {
        std::vector<Rect> remaining;
        for (const Rect& dirty : app->dirty_rects_) {
            Rect clipped = rect_intersection(dirty, expose);
            if (clipped.w > 0 && clipped.h > 0) {
                paint_rects.push_back(clipped);
            } else {
                remaining.push_back(dirty);
            }
        }
        app->dirty_rects_.swap(remaining);
    } else {
        return TRUE;
    }

    if (paint_rects.empty()) return TRUE;

    cairo_t* cr = gdk_cairo_create(win);
    for (const Rect& r : paint_rects) {
        cairo_save(cr);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_clip(cr);
        app->draw(cr, alloc.width, alloc.height);
        cairo_restore(cr);
    }
    cairo_destroy(cr);
    return TRUE;
}
gboolean TileWordsApp::on_button(GtkWidget*, GdkEventButton* event, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    if (event->type == GDK_BUTTON_PRESS) app->touch(static_cast<int>(event->x), static_cast<int>(event->y));
    return TRUE;
}

gboolean TileWordsApp::on_key(GtkWidget*, GdkEventKey* event, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    if (!event) return FALSE;

    std::ostringstream ss;
    ss << "key: keyval=" << static_cast<unsigned long>(event->keyval);
    app_log(ss.str());

    // Common XF86 power/sleep keysyms. If the Kindle routes the hardware
    // sleep button to the focused app, save safely and ask powerd to sleep.
    const unsigned long keyval = static_cast<unsigned long>(event->keyval);
    if (keyval == 0x1008FF2AUL || // XF86PowerOff
        keyval == 0x1008FF2FUL || // XF86Sleep
        keyval == 0x1008FF56UL) { // XF86Suspend
        app->request_sleep();
        return TRUE;
    }
    return FALSE;
}

gboolean TileWordsApp::on_focus_out(GtkWidget*, GdkEventFocus*, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    app_log("lifecycle: focus-out; safe save");
    app->save_game();
    return FALSE;
}

gboolean TileWordsApp::on_visibility(GtkWidget*, GdkEventVisibility* event, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    if (event) {
        std::ostringstream ss;
        ss << "lifecycle: visibility-state=" << static_cast<int>(event->state) << "; safe save";
        app_log(ss.str());
    } else {
        app_log("lifecycle: visibility event; safe save");
    }
    app->save_game();
    return FALSE;
}

gboolean TileWordsApp::on_delete(GtkWidget*, GdkEvent*, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    app->quit();
    return TRUE;
}

gboolean TileWordsApp::on_raise_timer(gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    if (!app || !app->window_) return FALSE;
    app_log("focus: raise/present timer");
    gtk_window_fullscreen(GTK_WINDOW(app->window_));
    gtk_window_set_keep_above(GTK_WINDOW(app->window_), TRUE);
    gtk_window_present(GTK_WINDOW(app->window_));
    gtk_widget_grab_focus(app->window_);
    GdkWindow* win = gtk_widget_get_window(app->window_);
    if (win) {
        gdk_window_set_override_redirect(win, TRUE);
        gdk_window_move_resize(win, 0, 0, gdk_screen_width(), gdk_screen_height());
        gdk_window_show(win);
        gdk_window_raise(win);
        gdk_window_focus(win, GDK_CURRENT_TIME);
    }
    return FALSE;
}

void TileWordsApp::compute_layout(int w, int h) {
    // Gameplay layout tuned for Kindle: larger board first, then rack/buttons.
    int top_h = std::max(128, h / 10 + 6);
    int bottom_h = std::max(318, h / 5 + 42);

    int top_gap = 8;
    int top_btn_h = std::max(48, std::min(58, h / 24));
    int new_w = std::max(92, std::min(112, w / 10));
    int settings_w = std::max(150, std::min(174, w / 6));
    int exit_w = std::max(92, std::min(112, w / 10));
    layout_.top_exit = {w - top_gap - exit_w, 8, exit_w, top_btn_h};
    layout_.top_settings = {layout_.top_exit.x - top_gap - settings_w, 8, settings_w, top_btn_h};
    layout_.top_new = {layout_.top_settings.x - top_gap - new_w, 8, new_w, top_btn_h};

    int board_size = std::min(w - 118, h - top_h - bottom_h - 8);
    int base_cell = std::max(28, board_size / BOARD_N);
    int tuned_cell = base_cell + game_.board_cell_tune;
    layout_.cell = std::max(28, std::min(90, tuned_cell));
    board_size = layout_.cell * BOARD_N;
    layout_.board = {(w - board_size) / 2, top_h + 2, board_size, board_size};

    int rack_tile = std::min((w - 118) / RACK_N, std::max(94, h / 12));
    int btn_h = std::max(72, std::min(90, h / 16));
    int rack_y = std::min(h - rack_tile - btn_h - 24, layout_.board.y + layout_.board.h + 12);
    rack_y = std::max(layout_.board.y + layout_.board.h + 8, rack_y);
    int rack_x = (w - rack_tile * RACK_N) / 2;
    for (int i = 0; i < RACK_N; ++i) layout_.rack[i] = {rack_x + i * rack_tile, rack_y, rack_tile - 5, rack_tile - 5};

    int btn_y = rack_y + rack_tile + 10;
    int gap = std::max(7, w / 120);
    int value_w = std::max(160, std::min(198, w / 5));
    int bw = std::min(158, (w - 44 - value_w - 4 * gap) / 4);
    bw = std::max(132, bw);
    int total_w = 4 * bw + value_w + 4 * gap;
    int x = std::max(12, (w - total_w) / 2);
    layout_.btn_submit = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_pass = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_exchange = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_shuffle = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_value = {x, btn_y, value_w, btn_h};

    int confirm_w = std::max(220, std::min(340, w / 3));
    int confirm_h = std::max(64, std::min(78, h / 18));
    layout_.confirm_button = {(w - confirm_w) / 2, h * 78 / 100, confirm_w, confirm_h};
    layout_.popup_yes = {w / 2 - 190, h * 62 / 100, 170, 70};
    layout_.popup_no = {w / 2 + 20, h * 62 / 100, 170, 70};

    int settings_w2 = std::min(w * 4 / 5, 700);
    int settings_h = std::min(h * 56 / 100, 520);
    int settings_x = (w - settings_w2) / 2;
    int settings_y = (h - settings_h) / 2;
    int inner_x = settings_x + 50;
    int inner_w = settings_w2 - 100;
    int settings_btn_h = std::max(54, h / 20);
    int settings_gap = 16;
    int first_y = settings_y + 88;

    layout_.btn_settings_ai = {inner_x, first_y, inner_w, settings_btn_h};
    int grid_y = first_y + settings_btn_h + settings_gap;
    int grid_gap = 12;
    int side_w = std::max(126, inner_w / 4);
    int mid_w = inner_w - side_w * 2 - grid_gap * 2;
    layout_.btn_settings_minus = {inner_x, grid_y, side_w, settings_btn_h};
    layout_.btn_settings_grid_value = {inner_x + side_w + grid_gap, grid_y, mid_w, settings_btn_h};
    layout_.btn_settings_plus = {layout_.btn_settings_grid_value.x + mid_w + grid_gap, grid_y, side_w, settings_btn_h};

    int half_w = (inner_w - settings_gap) / 2;
    int future_y = grid_y + settings_btn_h + settings_gap;
    layout_.btn_settings_placeholder1 = {inner_x, future_y, half_w, settings_btn_h};
    layout_.btn_settings_placeholder2 = {inner_x + half_w + settings_gap, future_y, half_w, settings_btn_h};
    layout_.btn_settings_back = {settings_x + settings_w2 / 2 - 100, future_y + settings_btn_h + settings_gap + 8, 200, settings_btn_h};

    int setup_w = std::min(w * 78 / 100, 780);
    int setup_h = std::min(h * 74 / 100, 780);
    int setup_x = (w - setup_w) / 2;
    int setup_y = std::max(78, (h - setup_h) / 2);
    int row_h = std::max(50, std::min(62, h / 19));
    int row_gap = 10;
    int inner_setup_x = setup_x + 48;
    int inner_setup_w = setup_w - 96;

    layout_.setup_players = {inner_setup_x, setup_y + 88, inner_setup_w, row_h};
    int py = setup_y + 154;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        layout_.setup_player_type[i] = {inner_setup_x, py + i * (row_h + row_gap), inner_setup_w, row_h};
    }
    int opt_y = py + MAX_PLAYERS * (row_h + row_gap) + 8;
    int opt_w = (inner_setup_w - row_gap) / 2;
    layout_.setup_tile_limit = {inner_setup_x, opt_y, opt_w, row_h};
    layout_.setup_board_size = {inner_setup_x + opt_w + row_gap, opt_y, opt_w, row_h};
    layout_.setup_custom_board = {inner_setup_x, opt_y + row_h + row_gap, inner_setup_w, row_h};
    int action_y = setup_y + setup_h - row_h - 34;
    layout_.setup_start = {setup_x + setup_w / 2 - 230, action_y, 200, row_h};
    layout_.setup_cancel = {setup_x + setup_w / 2 + 30, action_y, 200, row_h};

    if (w != last_logged_w_ || h != last_logged_h_ || layout_.cell != last_logged_cell_ || game_.board_cell_tune != last_logged_tune_) {
        log_grid_metrics("layout", w, h);
        last_logged_w_ = w;
        last_logged_h_ = h;
        last_logged_cell_ = layout_.cell;
        last_logged_tune_ = game_.board_cell_tune;
    }
}

void TileWordsApp::log_grid_metrics(const char* reason, int w, int h) {
    std::ostringstream ss;
    ss << "grid: " << reason
       << " screen=" << w << "x" << h
       << " tune=" << game_.board_cell_tune
       << " cell_px=" << layout_.cell
       << " board_px=" << layout_.board.w << "x" << layout_.board.h
       << " board_xy=" << layout_.board.x << "," << layout_.board.y
       << " rack_y=" << layout_.rack[0].y
       << " buttons_y=" << layout_.btn_submit.y;
    app_log(ss.str());
}
void TileWordsApp::draw(cairo_t* cr, int w, int h) {
    fill_rect(cr, {0,0,w,h}, 1.0);
    switch (game_.mode) {
        case Mode::Handoff: draw_handoff(cr, w, h); break;
        case Mode::Invalid: draw_invalid(cr, w, h); break;
        case Mode::NewSetup: draw_new_setup(cr, w, h); break;
        case Mode::Exchange: draw_exchange(cr, w, h); break;
        case Mode::BlankSelect: draw_blank_select(cr, w, h); break;
        case Mode::GameOver: draw_game_over(cr, w, h); break;
        case Mode::Settings: draw_settings(cr, w, h); break;
        case Mode::Normal: default: draw_normal(cr, w, h); break;
    }
}

void TileWordsApp::draw_normal(cairo_t* cr, int w, int h) {
    draw_button(cr, layout_.top_new, "NEW");
    draw_button(cr, layout_.top_settings, "SETTINGS");
    draw_button(cr, layout_.top_exit, "EXIT");

    Rect tiles_box{14, 20, 232, 54};
    fill_rect(cr, tiles_box, 1.0);
    stroke_rect(cr, tiles_box, 0.0, 2.0);
    std::ostringstream ts;
    ts << "Tiles Left: " << pad3(static_cast<int>(game_.bag.size()));
    centered_text(cr, tiles_box, ts.str(), 30, 0.0, false);

    int pc = std::max(2, std::min(MAX_PLAYERS, game_.player_count));
    int score_row_y = 92;
    int score_row_h = 58;
    int slot_w = pc >= 4 ? std::min(210, (w - 150) / pc) : std::min(280, (w - 240) / pc);
    slot_w = std::max(160, slot_w);
    int total_score_w = slot_w * pc;
    int score_x = (w - total_score_w) / 2;
    for (int i = 0; i < pc; ++i) {
        Rect pr{score_x + i * slot_w + 5, score_row_y, slot_w - 10, score_row_h};
        if (i == game_.current) {
            fill_rect(cr, pr, 1.0);
            stroke_rect(cr, pr, 0.0, 3.0);
        }
        std::ostringstream ps;
        ps << (game_.cpu[i] ? "CPU " : "Player ") << (i + 1) << ": " << pad3(game_.scores[i]);
        centered_text(cr, pr, ps.str(), pc >= 4 ? 27 : 31, 0.0, false);
    }

    for (int r = 0; r < BOARD_N; ++r) {
        for (int c = 0; c < BOARD_N; ++c) {
            Rect cell{layout_.board.x + c * layout_.cell, layout_.board.y + r * layout_.cell, layout_.cell, layout_.cell};
            Premium p = cell_premium(r, c);
            double bg = 1.0;
            if (p == Premium::DL || p == Premium::DW || p == Premium::Star) bg = 0.88;
            if (p == Premium::TL || p == Premium::TW) bg = 0.78;
            if (game_.board[r][c].ch && !game_.board[r][c].locked) bg = 0.70;
            fill_rect(cr, cell, bg);
            stroke_rect(cr, cell, 0.0, 1.0);

            if (game_.board[r][c].ch) {
                int val = game_.board[r][c].blank ? 0 : letter_value(game_.board[r][c].ch);
                draw_tile_face(cr, cell, game_.board[r][c].ch, val, false, game_.tile_scale);
            } else {
                std::string lab;
                if (p == Premium::DL) lab = "2L";
                else if (p == Premium::TL) lab = "3L";
                else if (p == Premium::DW) lab = "2W";
                else if (p == Premium::TW) lab = "3W";
                else if (p == Premium::Star) lab = "*";
                if (!lab.empty()) centered_text(cr, cell, lab, std::max(18, static_cast<int>(layout_.cell * 0.38)), 0.0, true);
            }
        }
    }

    for (int i = 0; i < RACK_N; ++i) {
        bool selected = game_.selected_rack == i;
        Tile t = game_.racks[game_.current][i];
        if (t.ch) draw_tile_face(cr, layout_.rack[i], t.ch == '?' ? '?' : t.ch, t.blank ? 0 : letter_value(t.ch), selected, game_.tile_scale);
        else draw_button(cr, layout_.rack[i], "", selected);
    }

    draw_button(cr, layout_.btn_submit, "SUBMIT");
    draw_button(cr, layout_.btn_pass, "PASS");
    draw_button(cr, layout_.btn_exchange, "EXCHANGE");
    draw_button(cr, layout_.btn_shuffle, "SHUFFLE");
    std::ostringstream vs;
    vs << "VALUE: " << pad3(preview_score());
    draw_button(cr, layout_.btn_value, vs.str());
}
void TileWordsApp::draw_handoff(cairo_t* cr, int w, int h) {
    fill_rect(cr, {0,0,w,h}, 1.0);
    draw_button(cr, layout_.top_exit, "EXIT");

    const double title_size = std::max(74.0, std::min(128.0, h / 11.5));
    const double turn_size = std::max(36.0, title_size * 0.50);
    const double info_size = std::max(24.0, std::min(38.0, h / 38.0));
    const double score_size = std::max(24.0, std::min(36.0, h / 42.0));

    Rect title_rect{0, h * 12 / 100, w, static_cast<int>(title_size + 24)};
    centered_text(cr, title_rect, "Pass the Kindle", title_size, 0.0, false);

    int underline_w = std::min(w * 78 / 100, static_cast<int>(title_size * 8.6));
    int underline_x = (w - underline_w) / 2;
    int underline_y = title_rect.y + title_rect.h - 8;
    set_gray(cr, 0.0);
    cairo_set_line_width(cr, std::max(2.0, title_size / 34.0));
    cairo_move_to(cr, underline_x, underline_y);
    cairo_line_to(cr, underline_x + underline_w, underline_y);
    cairo_stroke(cr);

    std::ostringstream turn;
    turn << (game_.cpu[game_.current] ? "CPU " : "Player ") << (game_.current + 1) << "'s Turn";
    centered_text(cr, {0, h * 28 / 100, w, static_cast<int>(turn_size + 18)}, turn.str(), turn_size, 0.0, false);

    std::ostringstream scored;
    scored << "Your opponent just scored " << pad3(game_.last_turn_score) << " points";
    centered_text(cr, {0, h * 42 / 100, w, static_cast<int>(info_size + 18)}, scored.str(), info_size, 0.0, false);

    std::string word = game_.last_turn_word.empty() ? "--" : game_.last_turn_word;
    centered_text(cr, {0, h * 47 / 100, w, static_cast<int>(info_size + 18)}, "with the word: " + word, info_size, 0.0, false);

    int pc = std::max(2, std::min(MAX_PLAYERS, game_.player_count));
    int score_row_y = h * 58 / 100;
    int score_row_h = std::max(50, static_cast<int>(score_size + 26));
    int slot_w = pc >= 4 ? std::min(210, (w - 100) / pc) : std::min(300, (w - 180) / pc);
    slot_w = std::max(pc >= 4 ? 150 : 190, slot_w);
    int total_score_w = slot_w * pc;
    int score_x = (w - total_score_w) / 2;
    for (int i = 0; i < pc; ++i) {
        Rect pr{score_x + i * slot_w + 5, score_row_y, slot_w - 10, score_row_h};
        if (i == game_.current) stroke_rect(cr, pr, 0.0, 3.0);
        std::ostringstream ps;
        ps << (game_.cpu[i] ? "CPU " : "Player ") << (i + 1) << ": " << pad3(game_.scores[i]);
        centered_text(cr, pr, ps.str(), pc >= 4 ? std::max(22.0, score_size * 0.84) : score_size, 0.0, false);
    }

    draw_button(cr, layout_.confirm_button, game_.cpu[game_.current] ? "CONTINUE" : "CONFIRM");
}

void TileWordsApp::draw_invalid(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 10, h / 3, w * 8 / 10, h / 4};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 20, box.w, 60}, "INVALID MOVE", 34, 0.0, true);
    centered_text(cr, {box.x + 20, box.y + 95, box.w - 40, 60}, game_.invalid_message, 24, 0.0, false);
    draw_button(cr, {w / 2 - 100, box.y + box.h - 85, 200, 62}, "OK");
}

void TileWordsApp::draw_new_setup(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    int box_w = std::min(w * 78 / 100, 780);
    int box_h = std::min(h * 74 / 100, 780);
    Rect box{(w - box_w) / 2, std::max(78, (h - box_h) / 2), box_w, box_h};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 18, box.w, 56}, "NEW GAME SETUP", 34, 0.0, true);

    std::ostringstream pc;
    pc << "PLAYERS: " << game_.setup_player_count << "  -  TAP TO CHANGE";
    draw_button(cr, layout_.setup_players, pc.str());

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        bool active = i < game_.setup_player_count;
        Rect r = layout_.setup_player_type[i];
        if (!active) {
            fill_rect(cr, r, 0.94);
            stroke_rect(cr, r, 0.0, 2.0);
            std::ostringstream off; off << "PLAYER " << (i + 1) << ": OFF";
            centered_text(cr, r, off.str(), 21, 0.55, true);
        } else {
            std::ostringstream ss;
            ss << "PLAYER " << (i + 1) << ": " << (game_.setup_cpu[i] ? "CPU" : "HUMAN");
            draw_button(cr, r, ss.str());
        }
    }

    std::ostringstream tl;
    tl << "TILES: " << game_.tile_limit_option;
    draw_button(cr, layout_.setup_tile_limit, tl.str());

    std::ostringstream bs;
    bs << "BOARD: " << (game_.setup_procedural_board ? "PROCEDURAL" : "STANDARD");
    draw_button(cr, layout_.setup_board_size, bs.str());

    std::ostringstream st;
    st << "STARTER LETTERS: " << game_.starter_letters_option;
    draw_button(cr, layout_.setup_custom_board, st.str());

    draw_button(cr, layout_.setup_start, "START GAME");
    draw_button(cr, layout_.setup_cancel, "CANCEL");
}
void TileWordsApp::draw_exchange(cairo_t* cr, int w, int h) {
    (void)w;
    (void)h;
    draw_normal(cr, w, h);
    // Keep exchange mode visually clean: selected rack tiles get an outline, with no
    // instruction text overlaying the rack or bottom controls.
    for (int i = 0; i < RACK_N; ++i) {
        if (game_.exchange_selected[i]) stroke_rect(cr, layout_.rack[i], 0.0, 7.0);
    }
}

void TileWordsApp::draw_blank_select(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);

    const int cols = 7;
    const int rows = 4;
    int tile_size = layout_.rack[0].w > 0 ? layout_.rack[0].w : std::max(82, w / 11);
    tile_size = std::max(72, std::min(tile_size, (w - 140) / cols));
    int gap = std::max(8, tile_size / 12);
    int grid_w = cols * tile_size + (cols - 1) * gap;
    int grid_h = rows * tile_size + (rows - 1) * gap;

    int pad_x = std::max(34, w / 28);
    int title_h = std::max(58, h / 22);
    int top_pad = 26;
    int bottom_pad = 34;
    int box_w = std::min(w - 80, grid_w + pad_x * 2);
    int box_h = top_pad + title_h + 22 + grid_h + bottom_pad;
    int box_x = (w - box_w) / 2;
    int preferred_y = std::max(layout_.board.y + layout_.cell, h / 7);
    int max_y = std::max(16, layout_.btn_submit.y - box_h - 18);
    int box_y = std::min(preferred_y, max_y);
    Rect box{box_x, box_y, box_w, box_h};

    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + top_pad, box.w, title_h}, "CHOOSE BLANK LETTER", 36, 0.0, true);

    int start_x = box.x + (box.w - grid_w) / 2;
    int start_y = box.y + top_pad + title_h + 22;

    for (int i = 0; i < 26; ++i) {
        int rr = i / cols;
        int cc = i % cols;
        Rect r{start_x + cc * (tile_size + gap), start_y + rr * (tile_size + gap), tile_size, tile_size};
        draw_tile_face(cr, r, static_cast<char>('A' + i), 0, false, game_.tile_scale);
    }
}

void TileWordsApp::draw_game_over(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 10, h / 3, w * 8 / 10, h / 4};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    int winner = 0;
    bool tie = true;
    for (int i = 1; i < game_.player_count; ++i) {
        if (game_.scores[i] != game_.scores[winner]) tie = false;
        if (game_.scores[i] > game_.scores[winner]) winner = i;
    }
    std::string msg = tie ? "GAME OVER - TIE" : "GAME OVER - PLAYER " + std::to_string(winner + 1) + " WINS";
    centered_text(cr, {box.x, box.y + 30, box.w, 70}, msg, 32, 0.0, true);
    draw_button(cr, layout_.popup_yes, "NEW");
    draw_button(cr, layout_.popup_no, "EXIT");
}

void TileWordsApp::draw_settings(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);

    int box_w = std::min(w * 4 / 5, 680);
    int box_h = std::min(h * 58 / 100, 500);
    Rect box{(w - box_w) / 2, (h - box_h) / 2, box_w, box_h};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);

    centered_text(cr, {box.x, box.y + 22, box.w, 52}, "SETTINGS", 34, 0.0, true);
    draw_button(cr, layout_.btn_settings_ai, game_.ai_enabled ? "AI OPPONENT: ON" : "AI OPPONENT: OFF");

    draw_button(cr, layout_.btn_settings_minus, "GRID -");
    std::ostringstream gs;
    gs << "GRID: " << layout_.cell << " px";
    draw_button(cr, layout_.btn_settings_grid_value, gs.str());
    draw_button(cr, layout_.btn_settings_plus, "GRID +");
    fill_rect(cr, layout_.btn_settings_placeholder1, 0.93);
    stroke_rect(cr, layout_.btn_settings_placeholder1, 0.0, 3.0);
    centered_text(cr, layout_.btn_settings_placeholder1, "FUTURE 1", std::max(18.0, layout_.btn_settings_placeholder1.h * 0.38), 0.45, true);

    fill_rect(cr, layout_.btn_settings_placeholder2, 0.93);
    stroke_rect(cr, layout_.btn_settings_placeholder2, 0.0, 3.0);
    centered_text(cr, layout_.btn_settings_placeholder2, "FUTURE 2", std::max(18.0, layout_.btn_settings_placeholder2.h * 0.38), 0.45, true);

    draw_button(cr, layout_.btn_settings_back, "BACK");
}

void TileWordsApp::touch(int x, int y) {
    if (in_rect(layout_.top_exit, x, y)) { quit(); return; }
    if (game_.mode == Mode::Normal && in_rect(layout_.top_settings, x, y)) { game_.mode = Mode::Settings; queue_draw(); return; }
    if (game_.mode == Mode::Normal && in_rect(layout_.top_new, x, y)) { game_.setup_player_count = game_.player_count; game_.setup_cpu = game_.cpu; game_.setup_procedural_board = game_.procedural_board; game_.mode = Mode::NewSetup; queue_draw(); return; }
    switch (game_.mode) {
        case Mode::Handoff: touch_handoff(x, y); break;
        case Mode::Invalid: game_.mode = Mode::Normal; queue_draw(); break;
        case Mode::NewSetup: touch_new_setup(x, y); break;
        case Mode::Exchange: touch_exchange(x, y); break;
        case Mode::BlankSelect: touch_blank_select(x, y); break;
        case Mode::GameOver:
            if (in_rect(layout_.popup_yes, x, y)) { new_game(true); queue_draw(); }
            else if (in_rect(layout_.popup_no, x, y)) quit();
            break;
        case Mode::Settings: touch_settings(x, y); break;
        case Mode::Normal: default: touch_normal(x, y); break;
    }
}

void TileWordsApp::touch_normal(int x, int y) {
    if (in_rect(layout_.btn_submit, x, y)) { submit_move(); queue_draw(); return; }
    if (in_rect(layout_.btn_pass, x, y)) { pass_turn(); queue_draw(); return; }
    if (in_rect(layout_.btn_exchange, x, y)) { game_.mode = Mode::Exchange; game_.exchange_selected.fill(false); queue_draw(); return; }
    if (in_rect(layout_.btn_shuffle, x, y)) { shuffle_rack(); save_game(); queue_draw_rack(); return; }

    for (int i = 0; i < RACK_N; ++i) {
        if (in_rect(layout_.rack[i], x, y)) {
            if (game_.racks[game_.current][i].ch) {
                int old_selected = game_.selected_rack;
                game_.selected_rack = (game_.selected_rack == i) ? -1 : i;
                if (old_selected >= 0) queue_draw_rect(layout_.rack[old_selected]);
                queue_draw_rect(layout_.rack[i]);
            }
            return;
        }
    }

    if (in_rect(layout_.board, x, y)) {
        int col = (x - layout_.board.x) / layout_.cell;
        int row = (y - layout_.board.y) / layout_.cell;
        if (row < 0 || row >= BOARD_N || col < 0 || col >= BOARD_N) return;
        Cell& cell = game_.board[row][col];
        if (cell.ch && !cell.locked) {
            int rack_idx = cell.rack_index;
            return_temp_tile(row, col);
            queue_draw_rect(board_cell_rect(row, col));
            if (rack_idx >= 0 && rack_idx < RACK_N) queue_draw_rect(layout_.rack[rack_idx]);
            else queue_draw_rack();
            return;
        }
        if (!cell.ch && game_.selected_rack >= 0) {
            Tile& t = game_.racks[game_.current][game_.selected_rack];
            if (t.ch) {
                cell.ch = t.ch;
                cell.blank = t.blank;
                cell.locked = false;
                cell.rack_index = game_.selected_rack;
                t.ch = 0;
                t.blank = false;
                if (cell.blank) {
                    game_.blank_row = row;
                    game_.blank_col = col;
                    game_.mode = Mode::BlankSelect;
                }
                game_.selected_rack = -1;
                if (game_.mode == Mode::BlankSelect) {
                    queue_draw();
                } else {
                    queue_draw_rect(board_cell_rect(row, col));
                    queue_draw_rect(layout_.rack[cell.rack_index]);
                    queue_draw_rect(layout_.btn_value);
                }
            }
        }
    }
}

void TileWordsApp::touch_handoff(int x, int y) {
    if (in_rect(layout_.confirm_button, x, y)) {
        game_.mode = Mode::Normal;
        save_game();
        queue_draw();
    }
}

void TileWordsApp::touch_exchange(int x, int y) {
    if (in_rect(layout_.btn_pass, x, y)) { game_.mode = Mode::Normal; queue_draw(); return; }
    if (in_rect(layout_.btn_submit, x, y)) { exchange_tiles(); queue_draw(); return; }
    for (int i = 0; i < RACK_N; ++i) {
        if (in_rect(layout_.rack[i], x, y) && game_.racks[game_.current][i].ch) {
            game_.exchange_selected[i] = !game_.exchange_selected[i];
            queue_draw_rect(layout_.rack[i]);
            return;
        }
    }
}

void TileWordsApp::touch_blank_select(int x, int y) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(window_, &alloc);
    int w = alloc.width, h = alloc.height;

    const int cols = 7;
    const int rows = 4;
    int tile_size = layout_.rack[0].w > 0 ? layout_.rack[0].w : std::max(82, w / 11);
    tile_size = std::max(72, std::min(tile_size, (w - 140) / cols));
    int gap = std::max(8, tile_size / 12);
    int grid_w = cols * tile_size + (cols - 1) * gap;
    int grid_h = rows * tile_size + (rows - 1) * gap;

    int pad_x = std::max(34, w / 28);
    int title_h = std::max(58, h / 22);
    int top_pad = 26;
    int bottom_pad = 34;
    int box_w = std::min(w - 80, grid_w + pad_x * 2);
    int box_h = top_pad + title_h + 22 + grid_h + bottom_pad;
    int box_x = (w - box_w) / 2;
    int preferred_y = std::max(layout_.board.y + layout_.cell, h / 7);
    int max_y = std::max(16, layout_.btn_submit.y - box_h - 18);
    int box_y = std::min(preferred_y, max_y);

    int start_x = box_x + (box_w - grid_w) / 2;
    int start_y = box_y + top_pad + title_h + 22;

    for (int i = 0; i < 26; ++i) {
        int rr = i / cols;
        int cc = i % cols;
        Rect r{start_x + cc * (tile_size + gap), start_y + rr * (tile_size + gap), tile_size, tile_size};
        if (in_rect(r, x, y)) {
            if (game_.blank_row >= 0 && game_.blank_col >= 0) {
                game_.board[game_.blank_row][game_.blank_col].ch = static_cast<char>('A' + i);
            }
            game_.blank_row = game_.blank_col = -1;
            game_.mode = Mode::Normal;
            queue_draw();
            return;
        }
    }
}

void TileWordsApp::touch_new_setup(int x, int y) {
    if (in_rect(layout_.setup_cancel, x, y)) {
        game_.mode = Mode::Normal;
        queue_draw();
        return;
    }
    if (in_rect(layout_.setup_start, x, y)) {
        game_.player_count = std::max(2, std::min(MAX_PLAYERS, game_.setup_player_count));
        game_.cpu = game_.setup_cpu;
        game_.procedural_board = game_.setup_procedural_board;
        for (int i = game_.player_count; i < MAX_PLAYERS; ++i) game_.cpu[i] = false;
        new_game(true);
        queue_draw();
        return;
    }
    if (in_rect(layout_.setup_players, x, y)) {
        game_.setup_player_count++;
        if (game_.setup_player_count > MAX_PLAYERS) game_.setup_player_count = 2;
        for (int i = game_.setup_player_count; i < MAX_PLAYERS; ++i) game_.setup_cpu[i] = false;
        queue_draw();
        return;
    }
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (i < game_.setup_player_count && in_rect(layout_.setup_player_type[i], x, y)) {
            game_.setup_cpu[i] = !game_.setup_cpu[i];
            queue_draw();
            return;
        }
    }
    if (in_rect(layout_.setup_tile_limit, x, y)) {
        if (game_.tile_limit_option == 100) game_.tile_limit_option = 75;
        else if (game_.tile_limit_option == 75) game_.tile_limit_option = 50;
        else game_.tile_limit_option = 100;
        queue_draw();
        return;
    }
    if (in_rect(layout_.setup_board_size, x, y)) {
        game_.setup_procedural_board = !game_.setup_procedural_board;
        queue_draw();
        return;
    }
    if (in_rect(layout_.setup_custom_board, x, y)) {
        if (game_.starter_letters_option == 0) game_.starter_letters_option = 10;
        else game_.starter_letters_option = 0;
        queue_draw();
        return;
    }
}

void TileWordsApp::touch_settings(int x, int y) {
    if (in_rect(layout_.btn_settings_back, x, y)) { game_.mode = Mode::Normal; save_game(); queue_draw(); return; }
    if (in_rect(layout_.btn_settings_ai, x, y)) { game_.ai_enabled = !game_.ai_enabled; save_game(); queue_draw(); return; }
    if (in_rect(layout_.btn_settings_minus, x, y)) {
        game_.board_cell_tune = std::max(-12, game_.board_cell_tune - 1);
        save_game();
        log_grid_metrics("grid-minus", gdk_screen_width(), gdk_screen_height());
        queue_draw();
        return;
    }
    if (in_rect(layout_.btn_settings_plus, x, y)) {
        game_.board_cell_tune = std::min(12, game_.board_cell_tune + 1);
        save_game();
        log_grid_metrics("grid-plus", gdk_screen_width(), gdk_screen_height());
        queue_draw();
        return;
    }
}

void TileWordsApp::queue_draw() {
    if (!window_) return;
    full_redraw_pending_ = true;
    dirty_rects_.clear();
    gtk_widget_queue_draw(window_);
}

void TileWordsApp::queue_draw_rect(const Rect& r) {
    if (!window_) return;
    GtkAllocation alloc;
    gtk_widget_get_allocation(window_, &alloc);
    if (alloc.width <= 0 || alloc.height <= 0) {
        alloc.width = gdk_screen_width();
        alloc.height = gdk_screen_height();
    }
    int x = std::max(0, r.x - 6);
    int y = std::max(0, r.y - 6);
    int w = std::min(alloc.width - x, r.w + 12);
    int h = std::min(alloc.height - y, r.h + 12);
    if (w <= 0 || h <= 0) return;

    Rect dirty{x, y, w, h};
    dirty_rects_.push_back(dirty);

    GdkWindow* win = gtk_widget_get_window(window_);
    if (win) {
        GdkRectangle gr{dirty.x, dirty.y, dirty.w, dirty.h};
        gdk_window_invalidate_rect(win, &gr, FALSE);
    } else {
        gtk_widget_queue_draw_area(window_, dirty.x, dirty.y, dirty.w, dirty.h);
    }
}

void TileWordsApp::queue_draw_rack() {
    if (!window_) return;
    if (RACK_N <= 0) return;
    int x1 = layout_.rack[0].x;
    int y1 = layout_.rack[0].y;
    int x2 = layout_.rack[RACK_N - 1].x + layout_.rack[RACK_N - 1].w;
    int y2 = layout_.rack[0].y + layout_.rack[0].h;
    queue_draw_rect({x1, y1, x2 - x1, y2 - y1});
}

Rect TileWordsApp::board_cell_rect(int row, int col) const {
    return {layout_.board.x + col * layout_.cell, layout_.board.y + row * layout_.cell, layout_.cell, layout_.cell};
}

Premium TileWordsApp::cell_premium(int row, int col) const {
    if (row < 0 || row >= BOARD_N || col < 0 || col >= BOARD_N) return Premium::None;
    return game_.premiums[row][col];
}

void TileWordsApp::setup_premiums() {
    for (int r = 0; r < BOARD_N; ++r) {
        for (int c = 0; c < BOARD_N; ++c) game_.premiums[r][c] = Premium::None;
    }

    if (!game_.procedural_board) {
        for (int r = 0; r < BOARD_N; ++r) {
            for (int c = 0; c < BOARD_N; ++c) game_.premiums[r][c] = standard_premium_at(r, c);
        }
        return;
    }

    game_.premiums[7][7] = Premium::Star;
    std::vector<std::pair<int,int>> cells;
    cells.reserve(BOARD_N * BOARD_N - 1);
    for (int r = 0; r < BOARD_N; ++r) {
        for (int c = 0; c < BOARD_N; ++c) {
            if (r == 7 && c == 7) continue;
            cells.push_back({r, c});
        }
    }
    for (int i = static_cast<int>(cells.size()) - 1; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(cells[i], cells[j]);
    }

    size_t pos = 0;
    auto assign = [&](Premium p, int count) {
        for (int i = 0; i < count && pos < cells.size(); ++i, ++pos) {
            game_.premiums[cells[pos].first][cells[pos].second] = p;
        }
    };
    assign(Premium::TW, 8);
    assign(Premium::DW, 16);
    assign(Premium::TL, 12);
    assign(Premium::DL, 24);
}

bool TileWordsApp::can_place_starter_at(int row, int col) const {
    if (row <= 0 || row >= BOARD_N - 1 || col <= 0 || col >= BOARD_N - 1) return false;
    if (row == 7 && col == 7) return false;
    if (game_.board[row][col].ch) return false;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int rr = row + dr;
            int cc = col + dc;
            if (rr >= 0 && rr < BOARD_N && cc >= 0 && cc < BOARD_N && game_.board[rr][cc].ch) return false;
        }
    }
    return true;
}

void TileWordsApp::place_starter_letters() {
    int target = std::max(0, std::min(30, game_.starter_letters_option));
    if (target <= 0) return;

    std::vector<std::pair<int,int>> cells;
    cells.reserve(BOARD_N * BOARD_N);
    for (int r = 1; r < BOARD_N - 1; ++r) {
        for (int c = 1; c < BOARD_N - 1; ++c) {
            if (r == 7 && c == 7) continue;
            cells.push_back({r, c});
        }
    }
    for (int i = static_cast<int>(cells.size()) - 1; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(cells[i], cells[j]);
    }

    int placed = 0;
    for (auto [r, c] : cells) {
        if (placed >= target || game_.bag.empty()) break;
        if (!can_place_starter_at(r, c)) continue;
        Tile t = draw_tile();
        if (!t.ch || t.blank) continue;
        game_.board[r][c] = Cell{t.ch, true, false, -1};
        ++placed;
    }
    app_log("newgame: placed " + std::to_string(placed) + " starter letters");
}

void TileWordsApp::return_unsubmitted_tiles_to_rack() {
    auto placed = placed_cells();
    if (placed.empty()) return;
    app_log("autosave: returning " + std::to_string(placed.size()) + " unsubmitted tile(s) to rack before save");
    // Return by repeatedly scanning because return_temp_tile mutates board state.
    for (auto [r, c] : placed) return_temp_tile(r, c);
    game_.selected_rack = -1;
    game_.blank_row = -1;
    game_.blank_col = -1;
    if (game_.mode == Mode::BlankSelect) game_.mode = Mode::Normal;
}

void TileWordsApp::request_sleep() {
    app_log("sleep: request received; safe save before sleep");
    save_game();
    // Let Kindle powerd handle the actual sleep request. This is intentionally
    // best-effort; if a firmware build does not expose lipc here, the save
    // has still been completed.
    int rc = std::system("lipc-set-prop com.lab126.powerd powerButton 1 >/dev/null 2>&1 &");
    app_log("sleep: powerd command rc=" + std::to_string(rc));
}

void TileWordsApp::quit() {
    app_log("quit: save and gtk_main_quit");
    save_game();
    gtk_main_quit();
}

Tile TileWordsApp::draw_tile() {
    if (game_.bag.empty()) return Tile{};
    int idx = std::rand() % game_.bag.size();
    Tile t = game_.bag[idx];
    game_.bag.erase(game_.bag.begin() + idx);
    return t;
}

void TileWordsApp::new_game(bool reset_scores) {
    bool keep_ai = game_.ai_enabled;
    int keep_tile_scale = game_.tile_scale;
    int keep_board_cell_tune = game_.board_cell_tune;
    int keep_players = std::max(2, std::min(MAX_PLAYERS, game_.player_count));
    auto keep_cpu = game_.cpu;
    int keep_tile_limit = game_.tile_limit_option;
    bool keep_procedural_board = game_.procedural_board;
    int keep_starter_letters = game_.starter_letters_option;

    game_ = Game{};
    game_.ai_enabled = keep_ai;
    game_.tile_scale = std::max(80, std::min(140, keep_tile_scale));
    game_.board_cell_tune = std::max(-12, std::min(12, keep_board_cell_tune));
    game_.player_count = keep_players;
    game_.setup_player_count = keep_players;
    game_.cpu = keep_cpu;
    for (int i = game_.player_count; i < MAX_PLAYERS; ++i) game_.cpu[i] = false;
    game_.setup_cpu = game_.cpu;
    game_.tile_limit_option = keep_tile_limit;
    game_.last_turn_player = -1;
    game_.last_turn_score = 0;
    game_.last_turn_word.clear();
    game_.procedural_board = keep_procedural_board;
    game_.setup_procedural_board = keep_procedural_board;
    game_.starter_letters_option = keep_starter_letters;
    if (!reset_scores) game_.scores = {{0,0,0,0}};

    const char* letters =
        "AAAAAAAAA" "BB" "CC" "DDDD" "EEEEEEEEEEEE" "FF" "GGG" "HH" "IIIIIIIII"
        "J" "K" "LLLL" "MM" "NNNNNN" "OOOOOOOO" "PP" "Q" "RRRRRR" "SSSS"
        "TTTTTT" "UUUU" "VV" "WW" "X" "YY" "Z" "??";
    for (const char* p = letters; *p; ++p) game_.bag.push_back(Tile{*p, *p == '?'});

    std::srand(static_cast<unsigned>(std::time(nullptr)));
    for (int i = static_cast<int>(game_.bag.size()) - 1; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(game_.bag[i], game_.bag[j]);
    }
    if (game_.tile_limit_option > 0 && game_.tile_limit_option < static_cast<int>(game_.bag.size())) {
        game_.bag.resize(game_.tile_limit_option);
    }

    setup_premiums();
    place_starter_letters();

    for (int p = 0; p < game_.player_count; ++p) refill_rack(p);
    game_.mode = Mode::Normal;
    save_game();
}

void TileWordsApp::refill_rack(int player) {
    for (int i = 0; i < RACK_N; ++i) {
        if (!game_.racks[player][i].ch && !game_.bag.empty()) game_.racks[player][i] = draw_tile();
    }
}

void TileWordsApp::load_dictionary() {
    dictionary_.clear();

    std::vector<std::string> candidates;
    auto add_candidate = [&](const std::string& path) {
        if (path.empty()) return;
        if (std::find(candidates.begin(), candidates.end(), path) == candidates.end()) {
            candidates.push_back(path);
        }
    };

    auto scan_data_dir = [&](const std::string& dir) {
        DIR* d = opendir(dir.c_str());
        if (!d) {
            app_log("dictionary: cannot scan " + dir);
            return;
        }

        int added = 0;
        while (dirent* ent = readdir(d)) {
            std::string name = ent->d_name ? ent->d_name : "";
            if (name.empty() || name == "." || name == "..") continue;

            // Load every .txt file in the data folder, not just dictionary.txt.
            // This avoids Kindle/MTP case and rename problems: Dictionary.txt,
            // dictionary.txt, wordlist.txt, NWL.txt, etc. all work.
            if (!ends_with_ci(name, ".txt")) continue;

            std::string path = dir + "/" + name;
            add_candidate(path);
            ++added;
        }
        closedir(d);
        app_log("dictionary: scanned " + dir + ", queued " + std::to_string(added) + " txt file(s)");
    };

    std::vector<std::string> data_dirs;
    auto add_dir = [&](const std::string& dir) {
        if (dir.empty()) return;
        if (std::find(data_dirs.begin(), data_dirs.end(), dir) == data_dirs.end()) data_dirs.push_back(dir);
    };

    add_dir(home_ + "/data");
    add_dir("/mnt/us/extensions/tilewords/data");
    add_dir("/mnt/us/extensions/TileWords/data");
    add_dir("./data");

    for (const std::string& dir : data_dirs) scan_data_dir(dir);

    // Explicit fallbacks. These also make the log obvious if the data folder
    // cannot be scanned on a specific Kindle build.
    add_candidate(home_ + "/data/Dictionary.txt");
    add_candidate(home_ + "/data/dictionary.txt");
    add_candidate(home_ + "/data/DICTIONARY.TXT");
    add_candidate(home_ + "/Dictionary.txt");
    add_candidate(home_ + "/dictionary.txt");
    add_candidate(dict_path_);
    add_candidate("/mnt/us/extensions/tilewords/data/Dictionary.txt");
    add_candidate("/mnt/us/extensions/tilewords/data/dictionary.txt");
    add_candidate("/mnt/us/extensions/tilewords/data/DICTIONARY.TXT");
    add_candidate("/mnt/us/extensions/TileWords/data/Dictionary.txt");
    add_candidate("/mnt/us/extensions/TileWords/data/dictionary.txt");
    add_candidate("./data/Dictionary.txt");
    add_candidate("./data/dictionary.txt");

    int files_loaded = 0;
    int total_lines = 0;
    for (const std::string& path : candidates) {
        std::ifstream in(path);
        if (!in) {
            app_log("dictionary: not found " + path);
            continue;
        }

        const size_t before = dictionary_.size();
        int file_lines = 0;
        int accepted_lines = 0;
        std::string line;
        while (std::getline(in, line)) {
            ++file_lines;
            std::string w = upper_word(line);
            if (w.size() >= 2 && w.size() <= 15) {
                dictionary_.insert(w);
                ++accepted_lines;
            }
        }

        ++files_loaded;
        total_lines += file_lines;
        const size_t added = dictionary_.size() - before;
        app_log("dictionary: read " + std::to_string(file_lines) +
                " lines, accepted " + std::to_string(accepted_lines) +
                ", added " + std::to_string(added) +
                " unique words from " + path);
    }

    if (files_loaded == 0) {
        app_log("dictionary: no dictionary txt file found in data folders; expected " + dict_path_);
    } else {
        app_log("dictionary: total " + std::to_string(dictionary_.size()) +
                " unique words from " + std::to_string(files_loaded) +
                " file(s), " + std::to_string(total_lines) +
                " lines; TOOK=" + (dictionary_.find("TOOK") != dictionary_.end() ? "yes" : "no") +
                "; ALL=" + (dictionary_.find("ALL") != dictionary_.end() ? "yes" : "no"));
    }
}

static std::string json_string_value(const std::string& src, const std::string& key) {
    std::string marker = "\"" + key + "\"";
    size_t p = src.find(marker);
    if (p == std::string::npos) return "";
    p = src.find(':', p);
    if (p == std::string::npos) return "";
    p = src.find('"', p);
    if (p == std::string::npos) return "";
    size_t q = src.find('"', p + 1);
    if (q == std::string::npos) return "";
    return src.substr(p + 1, q - p - 1);
}

static int json_int_value(const std::string& src, const std::string& key, int def = 0) {
    std::string marker = "\"" + key + "\"";
    size_t p = src.find(marker);
    if (p == std::string::npos) return def;
    p = src.find(':', p);
    if (p == std::string::npos) return def;
    return std::atoi(src.c_str() + p + 1);
}

void TileWordsApp::save_game() {
    return_unsubmitted_tiles_to_rack();
    std::ofstream out(save_path_);
    if (!out) return;
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"current_player\": " << game_.current << ",\n";
    out << "  \"player_count\": " << game_.player_count << ",\n";
    for (int p = 0; p < MAX_PLAYERS; ++p) out << "  \"score" << p << "\": " << game_.scores[p] << ",\n";
    for (int p = 0; p < MAX_PLAYERS; ++p) out << "  \"cpu" << p << "\": " << (game_.cpu[p] ? 1 : 0) << ",\n";
    out << "  \"handoff\": " << (game_.mode == Mode::Handoff ? 1 : 0) << ",\n";
    out << "  \"game_over\": " << (game_.game_over ? 1 : 0) << ",\n";
    out << "  \"ai_enabled\": " << (game_.ai_enabled ? 1 : 0) << ",\n";
    out << "  \"tile_scale\": " << game_.tile_scale << ",\n";
    out << "  \"board_cell_tune\": " << game_.board_cell_tune << ",\n";
    out << "  \"tile_limit_option\": " << game_.tile_limit_option << ",\n";
    out << "  \"last_turn_player\": " << game_.last_turn_player << ",\n";
    out << "  \"last_turn_score\": " << game_.last_turn_score << ",\n";
    out << "  \"last_turn_word\": \"" << upper_word(game_.last_turn_word) << "\",\n";
    out << "  \"procedural_board\": " << (game_.procedural_board ? 1 : 0) << ",\n";
    out << "  \"starter_letters_option\": " << game_.starter_letters_option << ",\n";
    out << "  \"premiumboard\": [\n";
    for (int r = 0; r < BOARD_N; ++r) {
        out << "    \"";
        for (int c = 0; c < BOARD_N; ++c) out << premium_to_char(game_.premiums[r][c]);
        out << "\"" << (r == BOARD_N - 1 ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"bag\": \"";
    for (const auto& t : game_.bag) out << t.ch;
    out << "\",\n";
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        out << "  \"rack" << p << "\": \"";
        for (int i = 0; i < RACK_N; ++i) out << (game_.racks[p][i].ch ? game_.racks[p][i].ch : '.');
        out << "\",\n";
        out << "  \"rackblank" << p << "\": \"";
        for (int i = 0; i < RACK_N; ++i) out << (game_.racks[p][i].blank ? '1' : '0');
        out << "\",\n";
    }
    out << "  \"board\": [\n";
    for (int r = 0; r < BOARD_N; ++r) {
        out << "    \"";
        for (int c = 0; c < BOARD_N; ++c) out << (game_.board[r][c].ch ? game_.board[r][c].ch : '.');
        out << "\"" << (r == BOARD_N - 1 ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"blankboard\": [\n";
    for (int r = 0; r < BOARD_N; ++r) {
        out << "    \"";
        for (int c = 0; c < BOARD_N; ++c) out << (game_.board[r][c].blank ? '1' : '0');
        out << "\"" << (r == BOARD_N - 1 ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
}

bool TileWordsApp::load_game() {
    std::ifstream in(save_path_);
    if (!in) return false;
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (src.find("version") == std::string::npos) return false;

    Game loaded;
    loaded.player_count = std::max(2, std::min(MAX_PLAYERS, json_int_value(src, "player_count", 2)));
    loaded.current = std::max(0, std::min(loaded.player_count - 1, json_int_value(src, "current_player", 0)));
    for (int p = 0; p < MAX_PLAYERS; ++p) loaded.scores[p] = json_int_value(src, "score" + std::to_string(p), 0);
    for (int p = 0; p < MAX_PLAYERS; ++p) loaded.cpu[p] = json_int_value(src, "cpu" + std::to_string(p), 0) != 0;
    for (int p = loaded.player_count; p < MAX_PLAYERS; ++p) loaded.cpu[p] = false;
    loaded.setup_player_count = loaded.player_count;
    loaded.setup_cpu = loaded.cpu;
    loaded.game_over = json_int_value(src, "game_over", 0) != 0;
    loaded.ai_enabled = json_int_value(src, "ai_enabled", 0) != 0;
    loaded.tile_scale = std::max(80, std::min(140, json_int_value(src, "tile_scale", 100)));
    loaded.board_cell_tune = std::max(-12, std::min(12, json_int_value(src, "board_cell_tune", 0)));
    loaded.tile_limit_option = json_int_value(src, "tile_limit_option", 100);
    loaded.last_turn_player = json_int_value(src, "last_turn_player", -1);
    loaded.last_turn_score = std::max(0, json_int_value(src, "last_turn_score", 0));
    loaded.last_turn_word = upper_word(json_string_value(src, "last_turn_word"));
    if (loaded.last_turn_player >= loaded.player_count) loaded.last_turn_player = -1;
    loaded.procedural_board = json_int_value(src, "procedural_board", 0) != 0;
    loaded.setup_procedural_board = loaded.procedural_board;
    loaded.starter_letters_option = json_int_value(src, "starter_letters_option", 0);
    loaded.mode = loaded.game_over ? Mode::GameOver : (json_int_value(src, "handoff", 0) ? Mode::Handoff : Mode::Normal);

    std::string bag = json_string_value(src, "bag");
    for (char c : bag) if (c && c != '.') loaded.bag.push_back(Tile{c, c == '?'});
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        std::string rack = json_string_value(src, "rack" + std::to_string(p));
        std::string blanks = json_string_value(src, "rackblank" + std::to_string(p));
        for (int i = 0; i < RACK_N && i < static_cast<int>(rack.size()); ++i) {
            if (rack[i] != '.') loaded.racks[p][i] = Tile{rack[i], i < static_cast<int>(blanks.size()) && blanks[i] == '1'};
        }
    }

    size_t bp = src.find("\"board\"");
    if (bp == std::string::npos) return false;
    size_t pos = src.find('[', bp);
    for (int r = 0; r < BOARD_N && pos != std::string::npos; ++r) {
        pos = src.find('"', pos);
        if (pos == std::string::npos) return false;
        size_t q = src.find('"', pos + 1);
        if (q == std::string::npos) return false;
        std::string row = src.substr(pos + 1, q - pos - 1);
        for (int c = 0; c < BOARD_N && c < static_cast<int>(row.size()); ++c) {
            if (row[c] != '.') loaded.board[r][c] = Cell{row[c], true, false, -1};
        }
        pos = q + 1;
    }
    size_t blp = src.find("\"blankboard\"");
    if (blp != std::string::npos) {
        pos = src.find('[', blp);
        for (int r = 0; r < BOARD_N && pos != std::string::npos; ++r) {
            pos = src.find('"', pos);
            if (pos == std::string::npos) break;
            size_t q = src.find('"', pos + 1);
            if (q == std::string::npos) break;
            std::string row = src.substr(pos + 1, q - pos - 1);
            for (int c = 0; c < BOARD_N && c < static_cast<int>(row.size()); ++c) {
                if (row[c] == '1') loaded.board[r][c].blank = true;
            }
            pos = q + 1;
        }
    }
    bool premium_loaded = false;
    size_t pp = src.find("\"premiumboard\"");
    if (pp != std::string::npos) {
        size_t pos = src.find('[', pp);
        premium_loaded = true;
        for (int r = 0; r < BOARD_N && pos != std::string::npos; ++r) {
            pos = src.find('"', pos + 1);
            if (pos == std::string::npos) { premium_loaded = false; break; }
            size_t q = src.find('"', pos + 1);
            if (q == std::string::npos) { premium_loaded = false; break; }
            std::string row = src.substr(pos + 1, q - pos - 1);
            for (int c = 0; c < BOARD_N && c < static_cast<int>(row.size()); ++c) loaded.premiums[r][c] = premium_from_char(row[c]);
            pos = q + 1;
        }
    }
    if (!premium_loaded) {
        for (int r = 0; r < BOARD_N; ++r) {
            for (int c = 0; c < BOARD_N; ++c) loaded.premiums[r][c] = standard_premium_at(r, c);
        }
    }

    game_ = loaded;
    return true;
}

void TileWordsApp::shuffle_rack() {
    auto& rack = game_.racks[game_.current];
    for (int i = RACK_N - 1; i > 0; --i) {
        int j = std::rand() % (i + 1);
        std::swap(rack[i], rack[j]);
    }
    game_.selected_rack = -1;
}

void TileWordsApp::pass_turn() {
    for (auto [r, c] : placed_cells()) return_temp_tile(r, c);
    game_.last_turn_player = game_.current;
    game_.last_turn_score = 0;
    game_.last_turn_word.clear();
    game_.selected_rack = -1;
    game_.pass_count++;
    if (game_.pass_count >= game_.player_count * 3) {
        game_.game_over = true;
        game_.mode = Mode::GameOver;
    } else {
        advance_to_next_player();
        enter_handoff();
    }
    save_game();
}

void TileWordsApp::exchange_tiles() {
    bool any = false;
    for (bool b : game_.exchange_selected) any = any || b;
    if (!any) { reject("Select at least one tile."); return; }
    if (game_.bag.empty()) { reject("Tile bag is empty."); return; }
    auto& rack = game_.racks[game_.current];
    for (int i = 0; i < RACK_N; ++i) {
        if (game_.exchange_selected[i] && rack[i].ch) {
            game_.bag.push_back(rack[i]);
            rack[i] = draw_tile();
        }
    }
    game_.exchange_selected.fill(false);
    game_.last_turn_player = game_.current;
    game_.last_turn_score = 0;
    game_.last_turn_word.clear();
    game_.selected_rack = -1;
    game_.pass_count = 0;
    advance_to_next_player();
    enter_handoff();
    save_game();
}

int TileWordsApp::next_player_index() const {
    int count = std::max(2, std::min(MAX_PLAYERS, game_.player_count));
    return (game_.current + 1) % count;
}

void TileWordsApp::advance_to_next_player() {
    game_.current = next_player_index();
    game_.selected_rack = -1;
}

void TileWordsApp::enter_handoff() {
    if (!game_.game_over) game_.mode = Mode::Handoff;
}

void TileWordsApp::reject(const std::string& msg) {
    game_.invalid_message = msg;
    game_.mode = Mode::Invalid;
}

void TileWordsApp::submit_move() {
    MoveResult res = validate_and_score();
    if (!res.ok) { reject(res.message); return; }
    lock_placed_and_score(res.score, res.word);
    save_game();
}

int TileWordsApp::locked_count() const {
    int n = 0;
    for (int r = 0; r < BOARD_N; ++r) for (int c = 0; c < BOARD_N; ++c) if (game_.board[r][c].locked) ++n;
    return n;
}

std::vector<std::pair<int,int>> TileWordsApp::placed_cells() const {
    std::vector<std::pair<int,int>> out;
    for (int r = 0; r < BOARD_N; ++r) for (int c = 0; c < BOARD_N; ++c) if (game_.board[r][c].ch && !game_.board[r][c].locked) out.push_back({r,c});
    return out;
}

std::string TileWordsApp::word_at(int row, int col, int dr, int dc) const {
    while (row - dr >= 0 && row - dr < BOARD_N && col - dc >= 0 && col - dc < BOARD_N && game_.board[row-dr][col-dc].ch) {
        row -= dr; col -= dc;
    }
    std::string w;
    while (row >= 0 && row < BOARD_N && col >= 0 && col < BOARD_N && game_.board[row][col].ch) {
        w.push_back(game_.board[row][col].ch);
        row += dr; col += dc;
    }
    return w;
}

bool TileWordsApp::is_dictionary_word(const std::string& word) const {
    if (word.size() <= 1) return true;
    if (dictionary_.empty()) return false;
    return dictionary_.find(upper_word(word)) != dictionary_.end();
}

int TileWordsApp::score_word(const std::vector<std::pair<int,int>>& cells) const {
    int sum = 0;
    int wm = 1;
    for (auto [r,c] : cells) {
        const Cell& cell = game_.board[r][c];
        int val = cell.blank ? 0 : letter_value(cell.ch);
        if (!cell.locked) {
            Premium p = cell_premium(r,c);
            if (p == Premium::DL) val *= 2;
            else if (p == Premium::TL) val *= 3;
            else if (p == Premium::DW || p == Premium::Star) wm *= 2;
            else if (p == Premium::TW) wm *= 3;
        }
        sum += val;
    }
    return sum * wm;
}

int TileWordsApp::preview_score() const {
    auto placed = placed_cells();
    if (placed.empty()) return 0;

    int minr = BOARD_N, maxr = -1, minc = BOARD_N, maxc = -1;
    for (auto [r,c] : placed) { minr = std::min(minr,r); maxr = std::max(maxr,r); minc = std::min(minc,c); maxc = std::max(maxc,c); }
    bool same_row = minr == maxr;
    bool same_col = minc == maxc;
    if (!same_row && !same_col) return 0;

    int dr = 0, dc = 0;
    if (same_row && !same_col) { dr = 0; dc = 1; }
    else if (same_col && !same_row) { dr = 1; dc = 0; }
    else {
        int r = placed[0].first, c = placed[0].second;
        bool horiz = (c > 0 && game_.board[r][c-1].ch) || (c < BOARD_N-1 && game_.board[r][c+1].ch);
        bool vert = (r > 0 && game_.board[r-1][c].ch) || (r < BOARD_N-1 && game_.board[r+1][c].ch);
        if (horiz) { dr = 0; dc = 1; }
        else if (vert) { dr = 1; dc = 0; }
        else { dr = 0; dc = 1; }
    }

    if (placed.size() > 1) {
        if (dr == 0) {
            for (int c = minc; c <= maxc; ++c) if (!game_.board[minr][c].ch) return 0;
        } else {
            for (int r = minr; r <= maxr; ++r) if (!game_.board[r][minc].ch) return 0;
        }
    }

    auto collect_cells = [&](int r, int c, int adr, int adc) {
        while (r - adr >= 0 && r - adr < BOARD_N && c - adc >= 0 && c - adc < BOARD_N && game_.board[r-adr][c-adc].ch) { r -= adr; c -= adc; }
        std::vector<std::pair<int,int>> cells;
        while (r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N && game_.board[r][c].ch) { cells.push_back({r,c}); r += adr; c += adc; }
        return cells;
    };

    int total = 0;
    auto main_cells = collect_cells(placed[0].first, placed[0].second, dr, dc);
    if (main_cells.size() > 1) total += score_word(main_cells);

    int cross_dr = dc, cross_dc = dr;
    for (auto [r,c] : placed) {
        auto cross = collect_cells(r, c, cross_dr, cross_dc);
        if (cross.size() > 1) total += score_word(cross);
    }
    if (placed.size() == RACK_N && total > 0) total += BINGO_BONUS;
    return total;
}

MoveResult TileWordsApp::validate_and_score() {
    auto placed = placed_cells();
    if (placed.empty()) return {false, 0, "Place at least one tile."};

    int locked = locked_count();
    bool first = locked == 0;
    if (first) {
        bool covers_center = false;
        for (auto [r,c] : placed) if (r == 7 && c == 7) covers_center = true;
        if (!covers_center) return {false, 0, "First move must cover center."};
    }

    int minr = BOARD_N, maxr = -1, minc = BOARD_N, maxc = -1;
    for (auto [r,c] : placed) { minr = std::min(minr,r); maxr = std::max(maxr,r); minc = std::min(minc,c); maxc = std::max(maxc,c); }
    bool same_row = minr == maxr;
    bool same_col = minc == maxc;
    if (!same_row && !same_col) return {false, 0, "Tiles must be in one row or column."};

    int dr = 0, dc = 0;
    if (same_row && !same_col) { dr = 0; dc = 1; }
    else if (same_col && !same_row) { dr = 1; dc = 0; }
    else {
        int r = placed[0].first, c = placed[0].second;
        bool horiz = (c > 0 && game_.board[r][c-1].ch) || (c < BOARD_N-1 && game_.board[r][c+1].ch);
        bool vert = (r > 0 && game_.board[r-1][c].ch) || (r < BOARD_N-1 && game_.board[r+1][c].ch);
        if (horiz) { dr = 0; dc = 1; }
        else if (vert) { dr = 1; dc = 0; }
        else { dr = 0; dc = 1; }
    }

    if (placed.size() > 1) {
        if (dr == 0) {
            for (int c = minc; c <= maxc; ++c) if (!game_.board[minr][c].ch) return {false, 0, "No gaps allowed in word."};
        } else {
            for (int r = minr; r <= maxr; ++r) if (!game_.board[r][minc].ch) return {false, 0, "No gaps allowed in word."};
        }
    }

    if (!first) {
        bool connected = false;
        for (auto [r,c] : placed) {
            const int d4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& d : d4) {
                int rr = r + d[0], cc = c + d[1];
                if (rr >= 0 && rr < BOARD_N && cc >= 0 && cc < BOARD_N && game_.board[rr][cc].locked) connected = true;
            }
        }
        if (!connected) return {false, 0, "Move must connect."};
    }

    std::vector<std::vector<std::pair<int,int>>> words;
    auto collect_cells = [&](int r, int c, int adr, int adc) {
        while (r - adr >= 0 && r - adr < BOARD_N && c - adc >= 0 && c - adc < BOARD_N && game_.board[r-adr][c-adc].ch) { r -= adr; c -= adc; }
        std::vector<std::pair<int,int>> cells;
        while (r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N && game_.board[r][c].ch) { cells.push_back({r,c}); r += adr; c += adc; }
        return cells;
    };

    auto main_cells = collect_cells(placed[0].first, placed[0].second, dr, dc);
    if (main_cells.size() > 1) words.push_back(main_cells);

    int cross_dr = dc, cross_dc = dr;
    for (auto [r,c] : placed) {
        auto cross = collect_cells(r, c, cross_dr, cross_dc);
        if (cross.size() > 1) words.push_back(cross);
    }
    if (words.empty()) return {false, 0, "Move must form a word."};

    if (dictionary_.empty()) return {false, 0, "Dictionary missing or empty."};

    int total = 0;
    std::string best_word;
    int best_score = -1;
    for (const auto& cells : words) {
        std::string w;
        for (auto [r,c] : cells) w.push_back(game_.board[r][c].ch);
        if (!is_dictionary_word(w)) return {false, 0, "Word not in dictionary: " + w};
        int word_score = score_word(cells);
        total += word_score;
        if (word_score > best_score || (word_score == best_score && w.size() > best_word.size())) {
            best_score = word_score;
            best_word = w;
        }
    }
    if (placed.size() == RACK_N) total += BINGO_BONUS;
    return {true, total, "", best_word};
}

void TileWordsApp::lock_placed_and_score(int score, const std::string& word) {
    auto placed = placed_cells();
    for (auto [r,c] : placed) {
        game_.board[r][c].locked = true;
        game_.board[r][c].rack_index = -1;
    }
    game_.scores[game_.current] += score;
    game_.last_turn_player = game_.current;
    game_.last_turn_score = score;
    game_.last_turn_word = upper_word(word);
    refill_rack(game_.current);
    game_.selected_rack = -1;
    game_.pass_count = 0;
    bool no_tiles = game_.bag.empty();
    if (no_tiles) {
        bool empty_rack = true;
        for (const auto& t : game_.racks[game_.current]) if (t.ch) empty_rack = false;
        if (empty_rack) { game_.game_over = true; game_.mode = Mode::GameOver; return; }
    }
    advance_to_next_player();
    enter_handoff();
}

void TileWordsApp::return_temp_tile(int row, int col) {
    Cell& cell = game_.board[row][col];
    if (!cell.ch || cell.locked) return;
    int idx = cell.rack_index;
    if (idx >= 0 && idx < RACK_N && !game_.racks[game_.current][idx].ch) {
        game_.racks[game_.current][idx] = Tile{cell.blank ? '?' : cell.ch, cell.blank};
    } else {
        for (int i = 0; i < RACK_N; ++i) {
            if (!game_.racks[game_.current][i].ch) { game_.racks[game_.current][i] = Tile{cell.blank ? '?' : cell.ch, cell.blank}; break; }
        }
    }
    cell = Cell{};
}

} // namespace

int main(int argc, char** argv) {
    TileWordsApp app;
    if (!app.init(argc, argv)) return 1;
    return app.run();
}
