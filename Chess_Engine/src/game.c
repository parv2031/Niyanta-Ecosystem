#ifndef __EMSCRIPTEN__
#include <gtk/gtk.h>
#endif
#include "game.h"
#include "globals.h"
#include <stdbool.h>
#include <stdlib.h>

bool is_white_piece(char* p) {
    return p == WHITE_PAWN || p == WHITE_ROOK || p == WHITE_KNIGHT || p == WHITE_BISHOP || p == WHITE_QUEEN || p == WHITE_KING;
}

bool is_black_piece(char* p) {
    return p == BLACK_PAWN || p == BLACK_ROOK || p == BLACK_KNIGHT || p == BLACK_BISHOP || p == BLACK_QUEEN || p == BLACK_KING;
}

bool is_friend(char *p, Color color) {
    if (strcmp(p, "") == 0) return false;
    return (color == COLOR_WHITE) ? is_white_piece(p) : is_black_piece(p);
}

bool is_enemy(char *p, Color color) {
    if (strcmp(p, "") == 0) return false;
    return (color == COLOR_WHITE) ? is_black_piece(p) : is_white_piece(p);
}

void pawn_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int dir = (color == COLOR_WHITE) ? 1 : -1;
    int start_row = (color == COLOR_WHITE) ? 1 : 6;

    if (src_row == start_row) {
        if (strcmp(board[src_row+dir][src_col], "") == 0) {
            possible_moves[num_possible_moves][0] = src_row+dir;
            possible_moves[num_possible_moves][1] = src_col;
            num_possible_moves++;
            if (strcmp(board[src_row+2*dir][src_col], "") == 0) {
                possible_moves[num_possible_moves][0] = src_row+2*dir;
                possible_moves[num_possible_moves][1] = src_col;
                num_possible_moves++;
            }
        }
    }
    if (src_row+dir >= 0 && src_row+dir < 8 && strcmp(board[src_row+dir][src_col], "") == 0) {
        possible_moves[num_possible_moves][0] = src_row+dir;
        possible_moves[num_possible_moves][1] = src_col;
        num_possible_moves++;
    }
    if (src_row+dir >= 0 && src_row+dir < 8) {
        if (src_col > 0 && is_enemy(board[src_row+dir][src_col-1], color)) {
            possible_moves[num_possible_moves][0] = src_row+dir;
            possible_moves[num_possible_moves][1] = src_col-1;
            num_possible_moves++;
        }
        if (src_col < 7 && is_enemy(board[src_row+dir][src_col+1], color)) {
            possible_moves[num_possible_moves][0] = src_row+dir;
            possible_moves[num_possible_moves][1] = src_col+1;
            num_possible_moves++;
        }
    }
}

void rook_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
    for (int d = 0; d < 4; d++) {
        for (int step = 1; step < 8; step++) {
            int r = src_row + dirs[d][0] * step;
            int c = src_col + dirs[d][1] * step;
            if (r < 0 || r >= 8 || c < 0 || c >= 8) break;
            if (is_friend(board[r][c], color)) break;
            possible_moves[num_possible_moves][0] = r;
            possible_moves[num_possible_moves][1] = c;
            num_possible_moves++;
            if (is_enemy(board[r][c], color)) break;
        }
    }
}

void bishop_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int dirs[4][2] = {{1,1}, {1,-1}, {-1,1}, {-1,-1}};
    for (int d = 0; d < 4; d++) {
        for (int step = 1; step < 8; step++) {
            int r = src_row + dirs[d][0] * step;
            int c = src_col + dirs[d][1] * step;
            if (r < 0 || r >= 8 || c < 0 || c >= 8) break;
            if (is_friend(board[r][c], color)) break;
            possible_moves[num_possible_moves][0] = r;
            possible_moves[num_possible_moves][1] = c;
            num_possible_moves++;
            if (is_enemy(board[r][c], color)) break;
        }
    }
}

void queen_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int dirs[8][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}};
    for (int d = 0; d < 8; d++) {
        for (int step = 1; step < 8; step++) {
            int r = src_row + dirs[d][0] * step;
            int c = src_col + dirs[d][1] * step;
            if (r < 0 || r >= 8 || c < 0 || c >= 8) break;
            if (is_friend(board[r][c], color)) break;
            possible_moves[num_possible_moves][0] = r;
            possible_moves[num_possible_moves][1] = c;
            num_possible_moves++;
            if (is_enemy(board[r][c], color)) break;
        }
    }
}

void knight_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int moves[8][2] = {{-2,-1}, {-2,1}, {-1,-2}, {-1,2}, {1,-2}, {1,2}, {2,-1}, {2,1}};
    for (int i = 0; i < 8; i++) {
        int r = src_row + moves[i][0];
        int c = src_col + moves[i][1];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (!is_friend(board[r][c], color)) {
                possible_moves[num_possible_moves][0] = r;
                possible_moves[num_possible_moves][1] = c;
                num_possible_moves++;
            }
        }
    }
}

void king_moves(int src_row, int src_col, char* board[8][8], Color color) {
    num_possible_moves = 0;
    int dirs[8][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}};
    for (int d = 0; d < 8; d++) {
        int r = src_row + dirs[d][0];
        int c = src_col + dirs[d][1];
        if (r >= 0 && r < 8 && c >= 0 && c < 8) {
            if (!is_friend(board[r][c], color)) {
                possible_moves[num_possible_moves][0] = r;
                possible_moves[num_possible_moves][1] = c;
                num_possible_moves++;
            }
        }
    }
}

int is_in_check(char* board[8][8], Color color) {
    int king_r = -1, king_c = -1;
    char *king_piece = (color == COLOR_WHITE) ? WHITE_KING : BLACK_KING;
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (board[i][j] == king_piece) {
                king_r = i; king_c = j; break;
            }
        }
        if (king_r != -1) break;
    }
    if (king_r == -1) return 0;

    int original_num_moves = num_possible_moves;
    int original_moves[100][2];
    memcpy(original_moves, possible_moves, sizeof(possible_moves));

    Color opp_color = (color == COLOR_WHITE) ? COLOR_BLACK : COLOR_WHITE;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (is_friend(board[i][j], opp_color)) {
                num_possible_moves = 0;
                char *p = board[i][j];
                if (p == WHITE_PAWN || p == BLACK_PAWN) pawn_moves(i, j, board, opp_color);
                else if (p == WHITE_ROOK || p == BLACK_ROOK) rook_moves(i, j, board, opp_color);
                else if (p == WHITE_KNIGHT || p == BLACK_KNIGHT) knight_moves(i, j, board, opp_color);
                else if (p == WHITE_BISHOP || p == BLACK_BISHOP) bishop_moves(i, j, board, opp_color);
                else if (p == WHITE_QUEEN || p == BLACK_QUEEN) queen_moves(i, j, board, opp_color);
                else if (p == WHITE_KING || p == BLACK_KING) king_moves(i, j, board, opp_color);

                for (int m = 0; m < num_possible_moves; m++) {
                    if (possible_moves[m][0] == king_r && possible_moves[m][1] == king_c) {
                        num_possible_moves = original_num_moves;
                        memcpy(possible_moves, original_moves, sizeof(possible_moves));
                        return 1;
                    }
                }
            }
        }
    }
    num_possible_moves = original_num_moves;
    memcpy(possible_moves, original_moves, sizeof(possible_moves));
    return 0;
}

void filter_invalid_moves(int src_row, int src_col, char* board[8][8], Color color) {
    int valid_moves[100][2];
    int num_valid_moves = 0;
    
    char* original_src = board[src_row][src_col];
    
    for (int i = 0; i < num_possible_moves; i++) {
        int r = possible_moves[i][0];
        int c = possible_moves[i][1];
        
        char* original_dest = board[r][c];
        board[r][c] = original_src;
        board[src_row][src_col] = "";
        
        if (!is_in_check(board, color)) {
            valid_moves[num_valid_moves][0] = r;
            valid_moves[num_valid_moves][1] = c;
            num_valid_moves++;
        }
        
        board[src_row][src_col] = original_src;
        board[r][c] = original_dest;
    }
    
    num_possible_moves = num_valid_moves;
    memcpy(possible_moves, valid_moves, sizeof(valid_moves));
}

int is_checkmate(char* board[8][8], Color color) {
    int total_moves = 0;
    
    int saved_num_moves = num_possible_moves;
    int saved_moves[100][2];
    memcpy(saved_moves, possible_moves, sizeof(possible_moves));

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if (is_friend(board[r][c], color)) {
                num_possible_moves = 0;
                char *p = board[r][c];
                if (p == WHITE_PAWN || p == BLACK_PAWN) pawn_moves(r, c, board, color);
                else if (p == WHITE_ROOK || p == BLACK_ROOK) rook_moves(r, c, board, color);
                else if (p == WHITE_KNIGHT || p == BLACK_KNIGHT) knight_moves(r, c, board, color);
                else if (p == WHITE_BISHOP || p == BLACK_BISHOP) bishop_moves(r, c, board, color);
                else if (p == WHITE_QUEEN || p == BLACK_QUEEN) queen_moves(r, c, board, color);
                else if (p == WHITE_KING || p == BLACK_KING) king_moves(r, c, board, color);
                
                filter_invalid_moves(r, c, board, color);
                total_moves += num_possible_moves;
            }
        }
    }
    
    num_possible_moves = saved_num_moves;
    memcpy(possible_moves, saved_moves, sizeof(possible_moves));
    
    return total_moves == 0 ? 1 : 0;
}

void random_move(int k, int s[][2]) {
    if (k > 0) {
        int randomR = rand() % k;
        bot_dest[0] = s[randomR][0];
        bot_dest[1] = s[randomR][1];
    }
}

void ai_random(char* board[8][8]) {
    int fr = -1, fc = -1, tr = -1, tc = -1;
    best_move_minimax(board, &fr, &fc, &tr, &tc, MINIMAX_DEPTH);

    bot_src_row = fr;
    bot_src_col = fc;
    bot_dest[0] = tr;
    bot_dest[1] = tc;
    bot_used_capture = 0;
}
