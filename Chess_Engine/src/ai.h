#ifndef AI_H
#define AI_H

#include "globals.h"

/* --- Piece values (centipawns) --- */
#define VAL_PAWN   100
#define VAL_KNIGHT 320
#define VAL_BISHOP 330
#define VAL_ROOK   500
#define VAL_QUEEN  900
#define VAL_KING   20000

#define MINIMAX_DEPTH 3   /* search depth – raise to 4 for harder play */
#define INF 9999999

/*
 * evaluate_board: material count from BLACK's perspective.
 * Black is the maximising player (bot plays black).
 * Positive = good for black, negative = good for white.
 */
int evaluate_board(char* board[8][8]);

/*
 * minimax: recursive alpha-beta search.
 *   is_maximizing == 1  →  black's turn  (bot)
 *   is_maximizing == 0  →  white's turn  (opponent)
 */
int minimax(char* board[8][8], int depth,
            int alpha, int beta,
            int is_maximizing);

/*
 * best_move_minimax: entry point called from game.c.
 * Writes the best (from row, from col, to row, to col) into
 * *fr, *fc, *tr, *tc.
 */
void best_move_minimax(char* board[8][8],
                       int *fr, int *fc,
                       int *tr, int *tc,
                       int depth);

#endif /* AI_H */
