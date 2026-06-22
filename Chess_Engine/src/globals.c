#include "globals.h"

char BLACK_ROOK[]="br.svg";
char BLACK_KNIGHT[]="bn.svg";
char BLACK_BISHOP[]="bb.svg";
char BLACK_QUEEN[]="bq.svg";
char BLACK_KING[]="bk.svg";
char BLACK_PAWN[]="bp.svg";
char WHITE_PAWN[]="wp.svg";
char WHITE_ROOK[]="wr.svg";
char WHITE_KNIGHT[]="wn.svg";
char WHITE_BISHOP[]="wb.svg";
char WHITE_QUEEN[]="wq.svg";
char WHITE_KING[]="wk.svg";

char* chess_board[8][8] = {
        {WHITE_ROOK, WHITE_KNIGHT, WHITE_BISHOP, WHITE_KING, WHITE_QUEEN, WHITE_BISHOP, WHITE_KNIGHT, WHITE_ROOK},
        {WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN},
        {BLACK_ROOK, BLACK_KNIGHT, BLACK_BISHOP, BLACK_KING, BLACK_QUEEN, BLACK_BISHOP, BLACK_KNIGHT, BLACK_ROOK}
    };

GList *captured_pieces_list = NULL;
BoardSquareData *selected_square = NULL;
BoardSquareData *target_square = NULL;

int num_possible_moves=0;
int possible_moves[100][2]={{-1,-1}};
int bot_src_row=-1;
int bot_src_col=-1;
int bot_dest[2]={-1,-1};
int bot_used_capture=1;
gboolean about_label_visible = FALSE;
GtkWidget * mode_window = NULL;
GtkWidget *board_grid_widget=NULL;
Turn current_turn = WHITE_TURN;

/* ── Undo / Redo stack storage ────────────────────────────────── */
MoveRecord history[HISTORY_MAX];
int history_top = 0;
int history_max = 0;

void push_move(MoveRecord m) {
    if (history_top < HISTORY_MAX) {
        history[history_top++] = m;
        history_max = history_top; /* new move invalidates redo future */
    }
}

MoveRecord pop_move(void) {
    /* caller must check history_empty() first */
    return history[--history_top];
}

int history_empty(void) {
    return history_top == 0;
}

/*
 * undo_last_move: pops one MoveRecord and reverts the board.
 *
 * Board state:  move the piece back from (to_r,to_c) → (from_r,from_c),
 *               restore captured piece at (to_r,to_c) if any.
 * Widget state: move moved_widget back visually;
 *               show captured_widget again if it was hidden.
 *
 * The undo system stores BOARD_OFFSET_X/Y and SQUARE_SIZE via
 * the extern declared in gui.c — we forward-declare them here.
 */
extern int BOARD_OFFSET_X;
extern int BOARD_OFFSET_Y;
extern int SQUARE_SIZE;
extern GtkWidget *board_grid_widget;
extern void get_asset_path(const char* relative_path, char* resolved_path, size_t max_len);

void undo_last_move(void) {
    if (history_empty()) return;

    MoveRecord m = pop_move();

    /* 1. Restore board logical state */
    chess_board[m.from_r][m.from_c] = m.moved_piece;
    chess_board[m.to_r][m.to_c]     = m.captured_piece;

    /* 1.5 Restore BoardSquareData state */
    if (m.from_square) m.from_square->piece_widget = m.moved_widget;
    if (m.to_square)   m.to_square->piece_widget = m.captured_widget;

    /* 2. Move the widget back */
    if (m.moved_widget) {
        GtkWidget *parent = gtk_widget_get_parent(m.moved_widget);
        if (parent)
            gtk_fixed_move(GTK_FIXED(parent), m.moved_widget,
                           BOARD_OFFSET_X + m.from_c * SQUARE_SIZE,
                           BOARD_OFFSET_Y + m.from_r * SQUARE_SIZE);
                           
        if (strcmp(m.moved_piece, WHITE_PAWN) == 0 && m.to_r == 0) {
            char path[256];
            get_asset_path("chess_pieces/wp.png", path, sizeof(path));
            gtk_image_set_from_file(GTK_IMAGE(m.moved_widget), path);
        } else if (strcmp(m.moved_piece, BLACK_PAWN) == 0 && m.to_r == 7) {
            char path[256];
            get_asset_path("chess_pieces/bp.png", path, sizeof(path));
            gtk_image_set_from_file(GTK_IMAGE(m.moved_widget), path);
        }
    }

    /* 3. Restore captured widget if there was one */
    if (m.captured_widget) {
        gtk_widget_show(m.captured_widget);
    }

    /* 4. Flip turn back */
    current_turn = (current_turn == WHITE_TURN) ? BLACK_TURN : WHITE_TURN;

    /* 5. Redraw the board */
    if (board_grid_widget)
        gtk_widget_queue_draw(board_grid_widget);
}

void redo_last_move(void) {
    if (history_top >= history_max) return; /* nothing to redo */

    /* Get the move we are re-applying */
    MoveRecord m = history[history_top++];

    /* 1. Restore board logical state (forward) */
    chess_board[m.to_r][m.to_c]     = m.moved_piece;
    chess_board[m.from_r][m.from_c] = "";

    /* 1.5 Restore BoardSquareData state */
    if (m.to_square)   m.to_square->piece_widget = m.moved_widget;
    if (m.from_square) m.from_square->piece_widget = NULL;

    /* 2. Move the widget forward */
    if (m.moved_widget) {
        GtkWidget *parent = gtk_widget_get_parent(m.moved_widget);
        if (parent)
            gtk_fixed_move(GTK_FIXED(parent), m.moved_widget,
                           BOARD_OFFSET_X + m.to_c * SQUARE_SIZE,
                           BOARD_OFFSET_Y + m.to_r * SQUARE_SIZE);
                           
        if (strcmp(m.moved_piece, WHITE_PAWN) == 0 && m.to_r == 0) {
            chess_board[m.to_r][m.to_c] = WHITE_QUEEN;
            char path[256];
            get_asset_path("chess_pieces/wq.png", path, sizeof(path));
            gtk_image_set_from_file(GTK_IMAGE(m.moved_widget), path);
        } else if (strcmp(m.moved_piece, BLACK_PAWN) == 0 && m.to_r == 7) {
            chess_board[m.to_r][m.to_c] = BLACK_QUEEN;
            char path[256];
            get_asset_path("chess_pieces/bq.png", path, sizeof(path));
            gtk_image_set_from_file(GTK_IMAGE(m.moved_widget), path);
        }
    }

    /* 3. Hide captured widget if there was one */
    if (m.captured_widget) {
        gtk_widget_hide(m.captured_widget);
    }

    /* 4. Flip turn forward */
    current_turn = (current_turn == WHITE_TURN) ? BLACK_TURN : WHITE_TURN;

    /* 5. Redraw the board */
    if (board_grid_widget)
        gtk_widget_queue_draw(board_grid_widget);
}

void reset_board() {
    char* initial_board[8][8] = {
        {WHITE_ROOK, WHITE_KNIGHT, WHITE_BISHOP, WHITE_KING, WHITE_QUEEN, WHITE_BISHOP, WHITE_KNIGHT, WHITE_ROOK},
        {WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN, WHITE_PAWN},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {"","","","","","", "", ""},
        {BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN, BLACK_PAWN},
        {BLACK_ROOK, BLACK_KNIGHT, BLACK_BISHOP, BLACK_KING, BLACK_QUEEN, BLACK_BISHOP, BLACK_KNIGHT, BLACK_ROOK}
    };
    for(int i = 0; i < 8; i++) {
        for(int j = 0; j < 8; j++) {
            chess_board[i][j] = initial_board[i][j];
        }
    }
    selected_square = NULL;
    target_square = NULL;
    bot_src_row = -1;
    bot_src_col = -1;
    bot_dest[0] = -1;
    bot_dest[1] = -1;
    bot_used_capture = 1;
    current_turn = WHITE_TURN;
    history_top = 0;   /* clear undo stack */
    history_max = 0;   /* clear redo stack */
    
    if (captured_pieces_list != NULL) {
        g_list_free(captured_pieces_list);
        captured_pieces_list = NULL;
    }
}
