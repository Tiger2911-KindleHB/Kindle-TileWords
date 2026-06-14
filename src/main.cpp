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
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int BOARD_N = 15;
constexpr int RACK_N = 7;
constexpr int BINGO_BONUS = 50;

struct Rect { int x = 0, y = 0, w = 0, h = 0; };
static bool in_rect(const Rect& r, int px, int py) {
    return px >= r.x && py >= r.y && px < r.x + r.w && py < r.y + r.h;
}

struct Tile { char ch = 0; bool blank = false; };
struct Cell { char ch = 0; bool locked = false; bool blank = false; int rack_index = -1; };

enum class Premium { None, DL, TL, DW, TW, Star };
enum class Mode { Normal, Handoff, Invalid, ConfirmNew, Exchange, BlankSelect, GameOver };

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
    Mode mode = Mode::Normal;
    std::array<bool, RACK_N> exchange_selected{{false,false,false,false,false,false,false}};
    int blank_row = -1;
    int blank_col = -1;
    std::string invalid_message;
};

struct Layout {
    Rect top_exit;
    Rect top_new;
    Rect board;
    int cell = 0;
    std::array<Rect, RACK_N> rack{};
    Rect btn_submit;
    Rect btn_pass;
    Rect btn_exchange;
    Rect btn_shuffle;
    Rect btn_new;
    Rect btn_exit;
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
    GtkWidget* area_ = nullptr;
    Game game_;
    Layout layout_;
    std::set<std::string> dictionary_;
    std::string home_;
    std::string save_path_;
    std::string dict_path_;

    static gboolean on_draw(GtkWidget* widget, GdkEventExpose* event, gpointer data);
    static gboolean on_button(GtkWidget* widget, GdkEventButton* event, gpointer data);
    static gboolean on_delete(GtkWidget* widget, GdkEvent* event, gpointer data);

    void compute_layout(int w, int h);
    void draw(cairo_t* cr, int w, int h);
    void draw_normal(cairo_t* cr, int w, int h);
    void draw_handoff(cairo_t* cr, int w, int h);
    void draw_invalid(cairo_t* cr, int w, int h);
    void draw_confirm_new(cairo_t* cr, int w, int h);
    void draw_exchange(cairo_t* cr, int w, int h);
    void draw_blank_select(cairo_t* cr, int w, int h);
    void draw_game_over(cairo_t* cr, int w, int h);

    void touch(int x, int y);
    void touch_normal(int x, int y);
    void touch_handoff(int x, int y);
    void touch_exchange(int x, int y);
    void touch_blank_select(int x, int y);
    void touch_confirm_new(int x, int y);

    void queue_draw();
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

    gtk_init(&argc, &argv);
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "TileWords");
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_fullscreen(GTK_WINDOW(window_));
    gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);

    area_ = gtk_drawing_area_new();
    gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(window_), area_);

    g_signal_connect(G_OBJECT(area_), "expose-event", G_CALLBACK(TileWordsApp::on_draw), this);
    g_signal_connect(G_OBJECT(area_), "button-press-event", G_CALLBACK(TileWordsApp::on_button), this);
    g_signal_connect(G_OBJECT(window_), "delete-event", G_CALLBACK(TileWordsApp::on_delete), this);

    load_dictionary();
    if (!load_game()) new_game(true);

    gtk_widget_show_all(window_);
    gtk_window_present(GTK_WINDOW(window_));
    return true;
}

int TileWordsApp::run() {
    gtk_main();
    return 0;
}

gboolean TileWordsApp::on_draw(GtkWidget* widget, GdkEventExpose*, gpointer data) {
    auto* app = static_cast<TileWordsApp*>(data);
    cairo_t* cr = gdk_cairo_create(widget->window);
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    app->compute_layout(alloc.width, alloc.height);
    app->draw(cr, alloc.width, alloc.height);
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

void TileWordsApp::compute_layout(int w, int h) {
    int top_h = std::max(72, h / 12);
    int bottom_h = std::max(190, h / 5);
    layout_.top_exit = {w - 132, 8, 122, top_h - 16};
    layout_.top_new = {w - 264, 8, 122, top_h - 16};

    int board_size = std::min(w - 24, h - top_h - bottom_h - 16);
    layout_.cell = board_size / BOARD_N;
    board_size = layout_.cell * BOARD_N;
    layout_.board = {(w - board_size) / 2, top_h + 8, board_size, board_size};

    int rack_y = layout_.board.y + layout_.board.h + 12;
    int tile_w = std::min((w - 32) / RACK_N, std::max(62, h / 15));
    int rack_x = (w - tile_w * RACK_N) / 2;
    for (int i = 0; i < RACK_N; ++i) layout_.rack[i] = {rack_x + i * tile_w, rack_y, tile_w - 4, tile_w - 4};

    int btn_y = rack_y + tile_w + 8;
    int btn_h = std::max(52, h / 18);
    int gap = 6;
    int bw = (w - 7 * gap) / 6;
    int x = gap;
    layout_.btn_submit = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_pass = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_exchange = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_shuffle = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_new = {x, btn_y, bw, btn_h}; x += bw + gap;
    layout_.btn_exit = {x, btn_y, bw, btn_h};

    layout_.confirm_button = {w / 4, h * 65 / 100, w / 2, std::max(76, h / 11)};
    layout_.popup_yes = {w / 2 - 190, h * 62 / 100, 170, 70};
    layout_.popup_no = {w / 2 + 20, h * 62 / 100, 170, 70};
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
                std::string s(1, game_.board[r][c].ch);
                centered_text(cr, cell, s, layout_.cell * 0.52, 0.0, true);
                int val = game_.board[r][c].blank ? 0 : letter_value(game_.board[r][c].ch);
                text(cr, cell.x + cell.w - 15, cell.y + cell.h - 7, std::to_string(val), std::max(8, layout_.cell / 5), 0.0, false);
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
        draw_button(cr, layout_.rack[i], game_.racks[game_.current][i].ch ? std::string(1, game_.racks[game_.current][i].ch) : "", selected);
        if (game_.racks[game_.current][i].ch) {
            int val = game_.racks[game_.current][i].blank ? 0 : letter_value(game_.racks[game_.current][i].ch);
            text(cr, layout_.rack[i].x + layout_.rack[i].w - 20, layout_.rack[i].y + layout_.rack[i].h - 10, std::to_string(val), 14, selected ? 1.0 : 0.0, false);
        }
    }

    draw_button(cr, layout_.btn_submit, "SUBMIT");
    draw_button(cr, layout_.btn_pass, "PASS");
    draw_button(cr, layout_.btn_exchange, "EXCH");
    draw_button(cr, layout_.btn_shuffle, "SHUFFLE");
    draw_button(cr, layout_.btn_new, "NEW");
    draw_button(cr, layout_.btn_exit, "EXIT");
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

void TileWordsApp::touch(int x, int y) {
    if (in_rect(layout_.top_exit, x, y) || in_rect(layout_.btn_exit, x, y)) { quit(); return; }
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
        case Mode::Normal: default: touch_normal(x, y); break;
    }
}

void TileWordsApp::touch_normal(int x, int y) {
    if (in_rect(layout_.top_new, x, y) || in_rect(layout_.btn_new, x, y)) { game_.mode = Mode::ConfirmNew; queue_draw(); return; }
    if (in_rect(layout_.btn_submit, x, y)) { submit_move(); queue_draw(); return; }
    if (in_rect(layout_.btn_pass, x, y)) { pass_turn(); queue_draw(); return; }
    if (in_rect(layout_.btn_exchange, x, y)) { game_.mode = Mode::Exchange; game_.exchange_selected.fill(false); queue_draw(); return; }
    if (in_rect(layout_.btn_shuffle, x, y)) { shuffle_rack(); save_game(); queue_draw(); return; }

    for (int i = 0; i < RACK_N; ++i) {
        if (in_rect(layout_.rack[i], x, y)) {
            if (game_.racks[game_.current][i].ch) game_.selected_rack = (game_.selected_rack == i) ? -1 : i;
            queue_draw();
            return;
        }
    }

    if (in_rect(layout_.board, x, y)) {
        int col = (x - layout_.board.x) / layout_.cell;
        int row = (y - layout_.board.y) / layout_.cell;
        if (row < 0 || row >= BOARD_N || col < 0 || col >= BOARD_N) return;
        Cell& cell = game_.board[row][col];
        if (cell.ch && !cell.locked) {
            return_temp_tile(row, col);
            queue_draw();
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
                queue_draw();
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
            queue_draw();
            return;
        }
    }
}

void TileWordsApp::touch_blank_select(int x, int y) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(area_, &alloc);
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

void TileWordsApp::queue_draw() {
    if (area_) gtk_widget_queue_draw(area_);
}

void TileWordsApp::quit() {
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
    game_ = Game{};
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
