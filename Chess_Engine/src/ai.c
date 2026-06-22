/*
 * ai.c — Minimax with Alpha-Beta Pruning for Chess_Game
 *
 * Architecture
 * ============
 *  evaluate_board()    – static material evaluation (black's POV)
 *  get_all_moves()     – generates every legal move for one colour,
 *                        using the existing move-generator + filter from game.c
 *  apply_move()        – makes a move on a scratch board
 *  minimax()           – recursive alpha-beta search
 *  best_move_minimax() – public entry point used by ai_random replacement
 */

#include "ai.h"
#include "game.h"
#include "globals.h"

#include <string.h>
#include <stdlib.h>
#include <limits.h>

extern int get_cache_sync(const char* fen, int depth);
extern void set_cache_async(const char* fen, int depth, int score);
extern int current_nodes_evaluated;
extern int current_cache_hits;

void board_to_string(char* board[8][8], char* out) {
    int idx = 0;
    for (int r=0; r<8; r++) {
        for (int c=0; c<8; c++) {
            char* p = board[r][c];
            if (!p || p[0] == '\0') out[idx++] = '_';
            else if (p[0] == 'w') out[idx++] = p[1] - 32;
            else out[idx++] = p[1];
        }
    }
    out[64] = '\0';
}

/* ------------------------------------------------------------------ */
/* Helper: piece value lookup                                           */
/* ------------------------------------------------------------------ */
static int piece_value(char *p) {
    if (p == NULL || strcmp(p, "") == 0) return 0;
    if (p == WHITE_PAWN   || p == BLACK_PAWN)   return VAL_PAWN;
    if (p == WHITE_KNIGHT || p == BLACK_KNIGHT) return VAL_KNIGHT;
    if (p == WHITE_BISHOP || p == BLACK_BISHOP) return VAL_BISHOP;
    if (p == WHITE_ROOK   || p == BLACK_ROOK)   return VAL_ROOK;
    if (p == WHITE_QUEEN  || p == BLACK_QUEEN)  return VAL_QUEEN;
    if (p == WHITE_KING   || p == BLACK_KING)   return VAL_KING;
    return 0;
}

/* ------------------------------------------------------------------ */
/* evaluate_board — material count from BLACK's perspective            */
/* +ve  →  good for black  |  -ve  →  good for white                  */
/* ------------------------------------------------------------------ */
int evaluate_board(char* board[8][8]) {
    int score = 0;
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char *p = board[r][c];
            if (p == NULL || strcmp(p, "") == 0) continue;
            int v = piece_value(p);
            if (is_black_piece(p))  score += v;
            else                    score -= v;
        }
    }
    return score;
}

/* ------------------------------------------------------------------ */
/* Move list structure                                                  */
/* ------------------------------------------------------------------ */
#define MAX_MOVES 256

typedef struct {
    int fr, fc, tr, tc;
} Move;

/* ------------------------------------------------------------------ */
/* get_all_moves — fill moves[] for one colour                         */
/*                                                                     */
/* We reuse the existing per-piece generators + filter functions.      */
/* Because those write into the global possible_moves / num_possible_  */
/* moves, we save & restore them around each call so nested searches   */
/* don't corrupt the outer frame.                                      */
/* ------------------------------------------------------------------ */

/* generate moves for every black piece */
static int get_all_moves_black(char* board[8][8], Move moves[]) {
    int count = 0;

    /* save global state */
    int saved_npm = num_possible_moves;
    int saved_pm[100][2];
    memcpy(saved_pm, possible_moves, sizeof(possible_moves));

    for (int r = 0; r < 8 && count < MAX_MOVES - 30; r++) {
        for (int c = 0; c < 8 && count < MAX_MOVES - 30; c++) {
            char *p = board[r][c];
            if (!is_black_piece(p)) continue;

            num_possible_moves = 0;
            if      (p == BLACK_PAWN)   pawn_moves(r, c, board, COLOR_BLACK);
            else if (p == BLACK_ROOK)   rook_moves(r, c, board, COLOR_BLACK);
            else if (p == BLACK_KNIGHT) knight_moves(r, c, board, COLOR_BLACK);
            else if (p == BLACK_BISHOP) bishop_moves(r, c, board, COLOR_BLACK);
            else if (p == BLACK_QUEEN)  queen_moves(r, c, board, COLOR_BLACK);
            else if (p == BLACK_KING)   king_moves(r, c, board, COLOR_BLACK);

            filter_invalid_moves(r, c, board, COLOR_BLACK);

            for (int k = 0; k < num_possible_moves; k++) {
                moves[count].fr = r;
                moves[count].fc = c;
                moves[count].tr = possible_moves[k][0];
                moves[count].tc = possible_moves[k][1];
                count++;
            }
        }
    }

    /* restore global state */
    num_possible_moves = saved_npm;
    memcpy(possible_moves, saved_pm, sizeof(possible_moves));
    return count;
}

/* generate moves for every white piece */
static int get_all_moves_white(char* board[8][8], Move moves[]) {
    int count = 0;

    int saved_npm = num_possible_moves;
    int saved_pm[100][2];
    memcpy(saved_pm, possible_moves, sizeof(possible_moves));

    for (int r = 0; r < 8 && count < MAX_MOVES - 30; r++) {
        for (int c = 0; c < 8 && count < MAX_MOVES - 30; c++) {
            char *p = board[r][c];
            if (!is_white_piece(p)) continue;

            num_possible_moves = 0;
            if      (p == WHITE_PAWN)   pawn_moves(r, c, board, COLOR_WHITE);
            else if (p == WHITE_ROOK)   rook_moves(r, c, board, COLOR_WHITE);
            else if (p == WHITE_KNIGHT) knight_moves(r, c, board, COLOR_WHITE);
            else if (p == WHITE_BISHOP) bishop_moves(r, c, board, COLOR_WHITE);
            else if (p == WHITE_QUEEN)  queen_moves(r, c, board, COLOR_WHITE);
            else if (p == WHITE_KING)   king_moves(r, c, board, COLOR_WHITE);

            filter_invalid_moves(r, c, board, COLOR_WHITE);

            for (int k = 0; k < num_possible_moves; k++) {
                moves[count].fr = r;
                moves[count].fc = c;
                moves[count].tr = possible_moves[k][0];
                moves[count].tc = possible_moves[k][1];
                count++;
            }
        }
    }

    num_possible_moves = saved_npm;
    memcpy(possible_moves, saved_pm, sizeof(possible_moves));
    return count;
}

/* ------------------------------------------------------------------ */
/* apply_move / undo_move on a scratch board                           */
/* ------------------------------------------------------------------ */
static inline void apply_move(char* board[8][8], Move m, char **captured_out, char **promoted_from_out) {
    *captured_out = board[m.tr][m.tc];
    *promoted_from_out = NULL;
    char *p = board[m.fr][m.fc];
    if (p == WHITE_PAWN && m.tr == 0) {
        p = WHITE_QUEEN;
        *promoted_from_out = WHITE_PAWN;
    } else if (p == BLACK_PAWN && m.tr == 7) {
        p = BLACK_QUEEN;
        *promoted_from_out = BLACK_PAWN;
    }
    board[m.tr][m.tc] = p;
    board[m.fr][m.fc] = "";
}

static inline void undo_move(char* board[8][8], Move m, char *captured, char *promoted_from) {
    if (promoted_from) {
        board[m.fr][m.fc] = promoted_from;
    } else {
        board[m.fr][m.fc] = board[m.tr][m.tc];
    }
    board[m.tr][m.tc] = captured;
}

/* ------------------------------------------------------------------ */
/* minimax with alpha-beta pruning                                     */
/* is_maximizing == 1  →  black to move  (bot)                        */
/* ------------------------------------------------------------------ */
int minimax(char* board[8][8], int depth,
            int alpha, int beta,
            int is_maximizing) {

    current_nodes_evaluated++;

    if (depth == 0) return evaluate_board(board);

    char stateStr[65];
    if (depth >= 2) {
        board_to_string(board, stateStr);
        int cached = get_cache_sync(stateStr, depth);
        if (cached != -999999) {
            current_cache_hits++;
            return cached;
        }
    }

    Move moves[MAX_MOVES];
    int n;

    if (is_maximizing) {
        /* BLACK moves */
        n = get_all_moves_black(board, moves);
        if (n == 0) {
            /* No moves: either checkmate or stalemate.
               Return a very bad score (black lost) */
            return -INF;
        }
        int best = -INF;
        for (int i = 0; i < n; i++) {
            char *cap, *promoted;
            apply_move(board, moves[i], &cap, &promoted);
            int score = minimax(board, depth - 1, alpha, beta, 0);
            undo_move(board, moves[i], cap, promoted);
            if (score > best) best = score;
            if (score > alpha) alpha = score;
            if (beta <= alpha) break;   /* alpha-beta cut */
        }
        if (depth >= 2) set_cache_async(stateStr, depth, best);
        return best;
    } else {
        /* WHITE moves */
        n = get_all_moves_white(board, moves);
        if (n == 0) return INF;   /* white has no moves = white lost */
        int best = INF;
        for (int i = 0; i < n; i++) {
            char *cap, *promoted;
            apply_move(board, moves[i], &cap, &promoted);
            int score = minimax(board, depth - 1, alpha, beta, 1);
            undo_move(board, moves[i], cap, promoted);
            if (score < best) best = score;
            if (score < beta) beta = score;
            if (beta <= alpha) break;
        }
        if (depth >= 2) set_cache_async(stateStr, depth, best);
        return best;
    }
}

/* ------------------------------------------------------------------ */
/* best_move_minimax — public entry point                             */
/* ------------------------------------------------------------------ */
void best_move_minimax(char* board[8][8],
                       int *fr, int *fc,
                       int *tr, int *tc,
                       int depth) {
    Move moves[MAX_MOVES];
    int n = get_all_moves_black(board, moves);

    if (n == 0) {
        /* Truly no moves – leave outputs at -1 */
        *fr = *fc = *tr = *tc = -1;
        return;
    }

    int best_score = -INF;
    Move best = moves[0];   /* sensible default */

    for (int i = 0; i < n; i++) {
        char *cap, *promoted;
        apply_move(board, moves[i], &cap, &promoted);
        int score = minimax(board, depth - 1, -INF, INF, 0);
        undo_move(board, moves[i], cap, promoted);

        if (score > best_score) {
            best_score = score;
            best = moves[i];
        }
    }

    *fr = best.fr;
    *fc = best.fc;
    *tr = best.tr;
    *tc = best.tc;
}
