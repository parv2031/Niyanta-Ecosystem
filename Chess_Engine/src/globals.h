#ifndef GLOBALS_H
#define GLOBALS_H

#ifndef __EMSCRIPTEN__
#include <gtk/gtk.h>
#endif

#include <string.h>
#include <time.h>

extern char BLACK_ROOK[];
extern char BLACK_KNIGHT[];
extern char BLACK_BISHOP[];
extern char BLACK_QUEEN[];
extern char BLACK_KING[];
extern char BLACK_PAWN[];

extern char WHITE_PAWN[];
extern char WHITE_ROOK[];
extern char WHITE_KNIGHT[];
extern char WHITE_BISHOP[];
extern char WHITE_QUEEN[];
extern char WHITE_KING[];

extern char* chess_board[8][8];

#ifndef __EMSCRIPTEN__
typedef struct {
    int row;
    int col;
    gboolean is_pressed;
    GtkWidget *piece_widget;
    char piece_name[50];
} BoardSquareData;

typedef struct {
    GtkWidget *piece_widget;
    int row;
    int col;
} CapturedPiece;

extern GList *captured_pieces_list;
extern BoardSquareData *selected_square;
extern BoardSquareData *target_square;
#endif

extern int num_possible_moves;
extern int possible_moves[100][2];
extern int bot_src_row;
extern int bot_src_col;
extern int bot_used_capture;
extern int bot_dest[2];

#ifndef __EMSCRIPTEN__
extern gboolean about_label_visible;
extern GtkWidget *mode_window;
extern GtkWidget *board_grid_widget;
#endif

typedef enum { WHITE_TURN, BLACK_TURN } Turn;
typedef enum { COLOR_WHITE = 0, COLOR_BLACK = 1 } Color;
extern Turn current_turn;

/* ── Undo / Redo stack ──────────────────────────────────────────────── */
#define HISTORY_MAX 5001

#ifndef __EMSCRIPTEN__
typedef struct {
    int from_r;           /* source row */
    int from_c;           /* source col */
    int to_r;             /* dest row   */
    int to_c;             /* dest col   */
    char *moved_piece;    /* pointer to the piece string that moved */
    char *captured_piece; /* pointer to the string that was captured ("" if none) */
    /* GUI widget references so we can restore them visually */
    GtkWidget *moved_widget;    /* the widget we moved */
    GtkWidget *captured_widget; /* the widget we hid (NULL if no capture) */
    /* Pointers to the BoardSquareData structs to restore their piece_widget ptrs */
    BoardSquareData *from_square;
    BoardSquareData *to_square;
} MoveRecord;
#else
typedef struct {
    int from_r;
    int from_c;
    int to_r;
    int to_c;
    char *moved_piece;
    char *captured_piece;
} MoveRecord;
#endif

extern MoveRecord history[HISTORY_MAX];
extern int history_top;   /* index of next free slot */
extern int history_max;   /* max redo index */

void push_move(MoveRecord m);
MoveRecord pop_move(void);
int  history_empty(void);
void undo_last_move(void);
void redo_last_move(void);

void reset_board();
#endif
