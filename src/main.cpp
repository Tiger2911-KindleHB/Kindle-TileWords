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
enum class Mode { Normal, Handoff, Invalid, ConfirmNew, Exchange, BlankSelect, GameOver, Settings };

struct MoveResult {
    bool ok = false;
    int score = 0;
    std::string message;
};

struct Game {
    std::array<std::array<Cell, BOARD_N>, BOARD_N> board{};
    std::array<std::array<Tile, RACK_N>, 2> racks{};
    std::array<int, 2> rack_count{{0,0}};
    std::vector<Tile> bag;
    std::array<int, 2> scores{{0,0}};
    int current = 0;
    int selected_rack = -1;
    int pass_count = 0;
    bool game_over = false;
    bool ai_enabled = false;
    int tile_scale = 100;
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
    Rect btn_settings_ai;
    Rect btn_settings_minus;
    Rect btn_settings_plus;
    Rect btn_settings_placeholder1;
    Rect btn_settings_placeholder2;
    Rect btn_settings_back;
    Rect confirm_button;
    Rect popup_yes;
    Rect popup_no;
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

    static gboolean on_draw(GtkWidget* widget, GdkEventExpose* event, gpointer data);
    static gboolean on_button(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean on_delete(GtkWidget* widget, GdkEvent* event, gpointer data);
    static gboolean on_raise_timer(gpointer data);

    void compute_layout(int w, int h);
    void draw(cairo_t* cr, int w, int h);
    void draw_normal(cairo_t* cr, int w, int h);
    void draw_handoff(cairo_t* cr, int w, int h);
    void draw_invalid(cairo_t* cr, int w, int h);
    void draw_confirm_new(cairo_t* cr, int w, int h);
    void draw_exchange(cairo_t* cr, int w, int h);
    void draw_blank_select(cairo_t* cr, int w, int h);
    void draw_game_over(cairo_t* cr, int w, int h);
    void draw_settings(cairo_t* cr, int w, int h);

    void touch(int x, int y);
    void touch_normal(int x, int y);
    void touch_handoff(int x, int y);
    void touch_exchange(int x, int y);
    void touch_blank_select(int x, int y);
    void touch_confirm_new(int x, int y);
    void touch_settings(int x, int y);

    void queue_draw();
    void queue_draw_rect(const Rect& r);
    void queue_draw_rack();
    Rect board_cell_rect(int row, int col) const;
    void quit();
    void new_game(bool reset_scores = true);
    void load_dictionary();
    void save_game();
    bool load_game();
    void refill_rack(int player);
    Tile draw_tile();
    void shuffle_rack();
    void pass_turn();
    void exchange_tiles();
    void enter_handoff();
    void reject(const std::string& msg);
    void submit_move();
    MoveResult validate_and_score();
    void lock_placed_and_score(int score);
    void return_temp_tile(int row, int col);
    int locked_count() const;
    std::vector<std::pair<int,int>> placed_cells() const;
    std::string word_at(int row, int col, int dr, int dc) const;
    int score_word(const std::vector<std::pair<int,int>>& cells) const;
    bool is_dictionary_word(const std::string& word) const;
};

static std::string upper_word(std::string s) {
    std::string out;
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalpha(uc)) out.push_back(static_cast<char>(std::toupper(uc)));
    }
    return out;
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

static Premium premium_at(int r, int c) {
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
    fill_rect(cr, r, inverted ? 0.0 : 1.0);
    stroke_rect(cr, r, 0.0, 3.0);
    if (!ch) return;

    double fg = inverted ? 1.0 : 0.0;
    double base = std::min(r.w, r.h);
    double letter_size = std::max(22.0, base * 0.56 * scale_percent / 100.0);
    double value_size = std::max(16.0, base * 0.23 * scale_percent / 100.0);

    std::string letter(1, ch);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, letter_size);
    cairo_text_extents_t le{};
    cairo_text_extents(cr, letter.c_str(), &le);
    set_gray(cr, fg);
    double lx = r.x + (r.w - le.width) * 0.42 - le.x_bearing;
    double ly = r.y + (r.h - le.height) * 0.43 - le.y_bearing;
    cairo_move_to(cr, lx, ly);
    cairo_show_text(cr, letter.c_str());

    std::string val = std::to_string(value);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, value_size);
    cairo_text_extents_t ve{};
    cairo_text_extents(cr, val.c_str(), &ve);
    double vx = r.x + r.w - ve.width - std::max(5.0, base * 0.08) - ve.x_bearing;
    double vy = r.y + r.h - std::max(5.0, base * 0.06) - ve.y_bearing;
    cairo_move_to(cr, vx, vy);
    cairo_show_text(cr, val.c_str());
}


static void draw_button(cairo_t* cr, const Rect& r, const std::string& label, bool inverted = false) {
    fill_rect(cr, r, inverted ? 0.0 : 1.0);
    stroke_rect(cr, r, 0.0, 3.0);
    centered_text(cr, r, label, std::max(18.0, std::min(32.0, r.h * 0.42)), inverted ? 1.0 : 0.0, true);
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
        GDK_POINTER_MOTION_MASK));

    g_signal_connect(G_OBJECT(window_), "expose-event", G_CALLBACK(TileWordsApp::on_draw), this);
    g_signal_connect(G_OBJECT(window_), "button-press-event", G_CALLBACK(TileWordsApp::on_button), this);
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
        gdk_window_set_events(gdk_window, static_cast<GdkEventMask>(gdk_window_get_events(gdk_window) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK));
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
    int top_h = std::max(78, h / 12);
    int bottom_h = std::max(210, h / 4);

    int top_gap = 8;
    int top_btn_h = top_h - 16;
    int top_btn_w = std::max(112, std::min(150, w / 8));
    layout_.top_exit = {w - top_gap - top_btn_w, 8, top_btn_w, top_btn_h};
    layout_.top_settings = {layout_.top_exit.x - top_gap - top_btn_w, 8, top_btn_w, top_btn_h};
    layout_.top_new = {layout_.top_settings.x - top_gap - top_btn_w, 8, top_btn_w, top_btn_h};

    int board_size = std::min(w - 24, h - top_h - bottom_h - 16);
    layout_.cell = board_size / BOARD_N;
    board_size = layout_.cell * BOARD_N;
    layout_.board = {(w - board_size) / 2, top_h + 8, board_size, board_size};

    int rack_y = layout_.board.y + layout_.board.h + 12;
    int tile_w = std::min((w - 28) / RACK_N, std::max(70, h / 13));
    int rack_x = (w - tile_w * RACK_N) / 2;
    for (int i = 0; i < RACK_N; ++i) layout_.rack[i] = {rack_x + i * tile_w, rack_y, tile_w - 4, tile_w - 4};

    int btn_y = rack_y + tile_w + 10;
    int btn_h = std::max(62, h / 16);
    int gap = std::max(8, w / 90);
    int bw = std::min(210, (w - 5 * gap) / 4);
    int total_w = 4 * bw + 3 * gap;
    int x = (w - total_w) / 2;
    layout_.btn_submit = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_pass = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_exchange = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_shuffle = {x, btn_y, bw, btn_h};

    layout_.confirm_button = {w / 4, h * 65 / 100, w / 2, std::max(76, h / 11)};
    layout_.popup_yes = {w / 2 - 190, h * 62 / 100, 170, 70};
    layout_.popup_no = {w / 2 + 20, h * 62 / 100, 170, 70};

    int settings_w = std::min(w * 4 / 5, 720);
    int settings_x = (w - settings_w) / 2;
    int settings_y = h / 5;
    int settings_btn_h = std::max(60, h / 17);
    int settings_gap = 14;
    layout_.btn_settings_ai = {settings_x + 40, settings_y + 95, settings_w - 80, settings_btn_h};
    layout_.btn_settings_minus = {settings_x + 40, settings_y + 175, (settings_w - 100) / 2, settings_btn_h};
    layout_.btn_settings_plus = {settings_x + 60 + (settings_w - 100) / 2, settings_y + 175, (settings_w - 100) / 2, settings_btn_h};
    layout_.btn_settings_placeholder1 = {settings_x + 40, settings_y + 255, settings_w - 80, settings_btn_h};
    layout_.btn_settings_placeholder2 = {settings_x + 40, settings_y + 255 + settings_btn_h + settings_gap, settings_w - 80, settings_btn_h};
    layout_.btn_settings_back = {settings_x + settings_w / 2 - 120, settings_y + 255 + (settings_btn_h + settings_gap) * 2, 240, settings_btn_h};
}

void TileWordsApp::draw(cairo_t* cr, int w, int h) {
    fill_rect(cr, {0,0,w,h}, 1.0);
    switch (game_.mode) {
        case Mode::Handoff: draw_handoff(cr, w, h); break;
        case Mode::Invalid: draw_invalid(cr, w, h); break;
        case Mode::ConfirmNew: draw_confirm_new(cr, w, h); break;
        case Mode::Exchange: draw_exchange(cr, w, h); break;
        case Mode::BlankSelect: draw_blank_select(cr, w, h); break;
        case Mode::GameOver: draw_game_over(cr, w, h); break;
        case Mode::Settings: draw_settings(cr, w, h); break;
        case Mode::Normal: default: draw_normal(cr, w, h); break;
    }
}

void TileWordsApp::draw_normal(cairo_t* cr, int w, int h) {
    text(cr, 20, 44, "TileWords", 30, 0.0, true);
    std::ostringstream ss;
    ss << "P" << (game_.current + 1) << " turn   P1 " << game_.scores[0]
       << "   P2 " << game_.scores[1] << "   Tiles " << game_.bag.size();
    text(cr, 20, 72, ss.str(), 24, 0.0, false);
    draw_button(cr, layout_.top_new, "NEW");
    draw_button(cr, layout_.top_settings, "SETTINGS");
    draw_button(cr, layout_.top_exit, "EXIT");

    for (int r = 0; r < BOARD_N; ++r) {
        for (int c = 0; c < BOARD_N; ++c) {
            Rect cell{layout_.board.x + c * layout_.cell, layout_.board.y + r * layout_.cell, layout_.cell, layout_.cell};
            Premium p = premium_at(r, c);
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
                if (!lab.empty()) centered_text(cr, cell, lab, std::max(10, layout_.cell / 4), 0.0, true);
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
}

void TileWordsApp::draw_handoff(cairo_t* cr, int w, int h) {
    fill_rect(cr, {0,0,w,h}, 1.0);
    draw_button(cr, layout_.top_exit, "EXIT");
    Rect box{w / 12, h / 4, w * 5 / 6, h / 3};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 30, box.w, 70}, "PASS KINDLE", 44, 0.0, true);
    std::ostringstream ss; ss << "PLAYER " << (game_.current + 1) << "'S TURN";
    centered_text(cr, {box.x, box.y + 130, box.w, 60}, ss.str(), 34, 0.0, true);
    centered_text(cr, {box.x, box.y + 190, box.w, 45}, "RACK HIDDEN UNTIL CONFIRM", 22, 0.0, false);
    draw_button(cr, layout_.confirm_button, "CONFIRM");
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

void TileWordsApp::draw_confirm_new(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 10, h / 3, w * 8 / 10, h / 4};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 30, box.w, 70}, "START NEW GAME?", 34, 0.0, true);
    draw_button(cr, layout_.popup_yes, "YES");
    draw_button(cr, layout_.popup_no, "NO");
}

void TileWordsApp::draw_exchange(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    centered_text(cr, {0, layout_.btn_submit.y - 36, w, 30}, "EXCHANGE: select rack tiles, then Submit", 24, 0.0, true);
    for (int i = 0; i < RACK_N; ++i) {
        if (game_.exchange_selected[i]) stroke_rect(cr, layout_.rack[i], 0.0, 7.0);
    }
}

void TileWordsApp::draw_blank_select(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 14, h / 5, w * 6 / 7, h * 3 / 5};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 15, box.w, 50}, "CHOOSE BLANK LETTER", 30, 0.0, true);
    int cols = 7;
    int size = std::min((box.w - 60) / cols, 68);
    int start_x = box.x + (box.w - cols * size) / 2;
    int start_y = box.y + 90;
    for (int i = 0; i < 26; ++i) {
        int rr = i / cols;
        int cc = i % cols;
        Rect r{start_x + cc * size, start_y + rr * size, size - 5, size - 5};
        draw_button(cr, r, std::string(1, static_cast<char>('A' + i)));
    }
}

void TileWordsApp::draw_game_over(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 10, h / 3, w * 8 / 10, h / 4};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    int winner = game_.scores[0] == game_.scores[1] ? 0 : (game_.scores[0] > game_.scores[1] ? 1 : 2);
    std::string msg = winner == 0 ? "GAME OVER - TIE" : "GAME OVER - PLAYER " + std::to_string(winner) + " WINS";
    centered_text(cr, {box.x, box.y + 30, box.w, 70}, msg, 32, 0.0, true);
    draw_button(cr, layout_.popup_yes, "NEW");
    draw_button(cr, layout_.popup_no, "EXIT");
}

void TileWordsApp::draw_settings(cairo_t* cr, int w, int h) {
    draw_normal(cr, w, h);
    Rect box{w / 10, h / 6, w * 8 / 10, h * 2 / 3};
    fill_rect(cr, box, 1.0);
    stroke_rect(cr, box, 0.0, 4.0);
    centered_text(cr, {box.x, box.y + 18, box.w, 54}, "SETTINGS", 34, 0.0, true);

    draw_button(cr, layout_.btn_settings_ai, game_.ai_enabled ? "AI OPPONENT: ON" : "AI OPPONENT: OFF");
    draw_button(cr, layout_.btn_settings_minus, "TILE FONT -");
    draw_button(cr, layout_.btn_settings_plus, "TILE FONT +");
    std::ostringstream ss;
    ss << "Tile font: " << game_.tile_scale << "%";
    centered_text(cr, {box.x, layout_.btn_settings_minus.y - 34, box.w, 30}, ss.str(), 22, 0.0, false);
    fill_rect(cr, layout_.btn_settings_placeholder1, 0.92);
    stroke_rect(cr, layout_.btn_settings_placeholder1, 0.0, 3.0);
    centered_text(cr, layout_.btn_settings_placeholder1, "FUTURE OPTION 1", std::max(18.0, layout_.btn_settings_placeholder1.h * 0.36), 0.45, true);
    fill_rect(cr, layout_.btn_settings_placeholder2, 0.92);
    stroke_rect(cr, layout_.btn_settings_placeholder2, 0.0, 3.0);
    centered_text(cr, layout_.btn_settings_placeholder2, "FUTURE OPTION 2", std::max(18.0, layout_.btn_settings_placeholder2.h * 0.36), 0.45, true);
    draw_button(cr, layout_.btn_settings_back, "BACK");
}

void TileWordsApp::touch(int x, int y) {
    if (in_rect(layout_.top_exit, x, y)) { quit(); return; }
    if (game_.mode == Mode::Normal && in_rect(layout_.top_settings, x, y)) { game_.mode = Mode::Settings; queue_draw(); return; }
    if (game_.mode == Mode::Normal && in_rect(layout_.top_new, x, y)) { game_.mode = Mode::ConfirmNew; queue_draw(); return; }
    switch (game_.mode) {
        case Mode::Handoff: touch_handoff(x, y); break;
        case Mode::Invalid: game_.mode = Mode::Normal; queue_draw(); break;
        case Mode::ConfirmNew: touch_confirm_new(x, y); break;
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
    Rect box{w / 14, h / 5, w * 6 / 7, h * 3 / 5};
    int cols = 7;
    int size = std::min((box.w - 60) / cols, 68);
    int start_x = box.x + (box.w - cols * size) / 2;
    int start_y = box.y + 90;
    for (int i = 0; i < 26; ++i) {
        int rr = i / cols;
        int cc = i % cols;
        Rect r{start_x + cc * size, start_y + rr * size, size - 5, size - 5};
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

void TileWordsApp::touch_confirm_new(int x, int y) {
    if (in_rect(layout_.popup_yes, x, y)) { new_game(true); queue_draw(); }
    else if (in_rect(layout_.popup_no, x, y)) { game_.mode = Mode::Normal; queue_draw(); }
}

void TileWordsApp::touch_settings(int x, int y) {
    if (in_rect(layout_.btn_settings_back, x, y)) { game_.mode = Mode::Normal; save_game(); queue_draw(); return; }
    if (in_rect(layout_.btn_settings_ai, x, y)) { game_.ai_enabled = !game_.ai_enabled; save_game(); queue_draw(); return; }
    if (in_rect(layout_.btn_settings_minus, x, y)) {
        game_.tile_scale = std::max(80, game_.tile_scale - 10);
        save_game();
        queue_draw();
        return;
    }
    if (in_rect(layout_.btn_settings_plus, x, y)) {
        game_.tile_scale = std::min(140, game_.tile_scale + 10);
        save_game();
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
    game_ = Game{};
    game_.ai_enabled = keep_ai;
    game_.tile_scale = std::max(80, std::min(140, keep_tile_scale));
    if (!reset_scores) game_.scores = {{0,0}};
    const char* letters =
        "AAAAAAAAA" "BB" "CC" "DDDD" "EEEEEEEEEEEE" "FF" "GGG" "HH" "IIIIIIIII"
        "J" "K" "LLLL" "MM" "NNNNNN" "OOOOOOOO" "PP" "Q" "RRRRRR" "SSSS"
        "TTTTTT" "UUUU" "VV" "WW" "X" "YY" "Z" "??";
    for (const char* p = letters; *p; ++p) game_.bag.push_back(Tile{*p, *p == '?'});
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    refill_rack(0);
    refill_rack(1);
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
    std::ifstream in(dict_path_);
    std::string line;
    while (std::getline(in, line)) {
        std::string w = upper_word(line);
        if (!w.empty()) dictionary_.insert(w);
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
    std::ofstream out(save_path_);
    if (!out) return;
    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"current_player\": " << game_.current << ",\n";
    out << "  \"score0\": " << game_.scores[0] << ",\n";
    out << "  \"score1\": " << game_.scores[1] << ",\n";
    out << "  \"handoff\": " << (game_.mode == Mode::Handoff ? 1 : 0) << ",\n";
    out << "  \"game_over\": " << (game_.game_over ? 1 : 0) << ",\n";
    out << "  \"ai_enabled\": " << (game_.ai_enabled ? 1 : 0) << ",\n";
    out << "  \"tile_scale\": " << game_.tile_scale << ",\n";
    out << "  \"bag\": \"";
    for (const auto& t : game_.bag) out << t.ch;
    out << "\",\n";
    for (int p = 0; p < 2; ++p) {
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
    loaded.current = std::max(0, std::min(1, json_int_value(src, "current_player", 0)));
    loaded.scores[0] = json_int_value(src, "score0", 0);
    loaded.scores[1] = json_int_value(src, "score1", 0);
    loaded.game_over = json_int_value(src, "game_over", 0) != 0;
    loaded.ai_enabled = json_int_value(src, "ai_enabled", 0) != 0;
    loaded.tile_scale = std::max(80, std::min(140, json_int_value(src, "tile_scale", 100)));
    loaded.mode = loaded.game_over ? Mode::GameOver : (json_int_value(src, "handoff", 0) ? Mode::Handoff : Mode::Normal);

    std::string bag = json_string_value(src, "bag");
    for (char c : bag) if (c && c != '.') loaded.bag.push_back(Tile{c, c == '?'});
    for (int p = 0; p < 2; ++p) {
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
    game_.selected_rack = -1;
    game_.pass_count++;
    if (game_.pass_count >= 6) {
        game_.game_over = true;
        game_.mode = Mode::GameOver;
    } else {
        game_.current = 1 - game_.current;
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
    game_.selected_rack = -1;
    game_.pass_count = 0;
    game_.current = 1 - game_.current;
    enter_handoff();
    save_game();
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
    lock_placed_and_score(res.score);
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
    if (dictionary_.empty()) return true;
    return dictionary_.find(upper_word(word)) != dictionary_.end();
}

int TileWordsApp::score_word(const std::vector<std::pair<int,int>>& cells) const {
    int sum = 0;
    int wm = 1;
    for (auto [r,c] : cells) {
        const Cell& cell = game_.board[r][c];
        int val = cell.blank ? 0 : letter_value(cell.ch);
        if (!cell.locked) {
            Premium p = premium_at(r,c);
            if (p == Premium::DL) val *= 2;
            else if (p == Premium::TL) val *= 3;
            else if (p == Premium::DW || p == Premium::Star) wm *= 2;
            else if (p == Premium::TW) wm *= 3;
        }
        sum += val;
    }
    return sum * wm;
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

    int total = 0;
    for (const auto& cells : words) {
        std::string w;
        for (auto [r,c] : cells) w.push_back(game_.board[r][c].ch);
        if (!is_dictionary_word(w)) return {false, 0, "Word not in dictionary: " + w};
        total += score_word(cells);
    }
    if (placed.size() == RACK_N) total += BINGO_BONUS;
    return {true, total, ""};
}

void TileWordsApp::lock_placed_and_score(int score) {
    auto placed = placed_cells();
    for (auto [r,c] : placed) {
        game_.board[r][c].locked = true;
        game_.board[r][c].rack_index = -1;
    }
    game_.scores[game_.current] += score;
    refill_rack(game_.current);
    game_.selected_rack = -1;
    game_.pass_count = 0;
    bool no_tiles = game_.bag.empty();
    if (no_tiles) {
        bool empty_rack = true;
        for (const auto& t : game_.racks[game_.current]) if (t.ch) empty_rack = false;
        if (empty_rack) { game_.game_over = true; game_.mode = Mode::GameOver; return; }
    }
    game_.current = 1 - game_.current;
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
