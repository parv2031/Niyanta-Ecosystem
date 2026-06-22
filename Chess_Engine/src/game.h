#ifndef GAME_H
#define GAME_H
#include"globals.h"
#include <stdbool.h>
#include "ai.h"

bool is_white_piece(char* p);
bool is_black_piece(char* p);
bool is_friend(char *p, Color color);
bool is_enemy(char *p, Color color);

void pawn_moves(int src_row, int src_col, char* board[8][8], Color color);
void rook_moves(int src_row, int src_col, char* board[8][8], Color color);
void knight_moves(int src_row, int src_col, char* board[8][8], Color color);
void bishop_moves(int src_row, int src_col, char* board[8][8], Color color);
void queen_moves(int src_row, int src_col, char* board[8][8], Color color);
void king_moves(int src_row, int src_col, char* board[8][8], Color color);

void filter_invalid_moves(int src_row, int src_col, char* board[8][8], Color color);
int is_in_check(char* board[8][8], Color color);
int is_checkmate(char* board[8][8], Color color);

void random_move(int k, int s[][2]);
void ai_random(char* board[8][8]);

#endif
