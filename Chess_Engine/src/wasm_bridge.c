#include <emscripten.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ai.h"
#include "game.h"

// Define the global board variable for WASM since globals.c might not be linked or we need a clean slate
char* chess_board[8][8];

// Define globals needed by game.c
int num_possible_moves = 0;
int possible_moves[100][2];
int bot_src_row = 0;
int bot_src_col = 0;
int bot_used_capture = 0;
int bot_dest[2] = {0, 0};
Turn current_turn = WHITE_TURN;

// We need to provide the piece strings
char WHITE_PAWN[] = "wp";
char WHITE_ROOK[] = "wr";
char WHITE_KNIGHT[] = "wn";
char WHITE_BISHOP[] = "wb";
char WHITE_QUEEN[] = "wq";
char WHITE_KING[] = "wk";

char BLACK_PAWN[] = "bp";
char BLACK_ROOK[] = "br";
char BLACK_KNIGHT[] = "bn";
char BLACK_BISHOP[] = "bb";
char BLACK_QUEEN[] = "bq";
char BLACK_KING[] = "bk";

// A dummy reset board function if needed
void reset_board() {}

EMSCRIPTEN_KEEPALIVE
int init_engine() {
    return 1;
}

// Convert a simple 64-char string representation to the internal board
// empty = ' ', pawn = 'p'/'P', knight = 'n'/'N', etc. (lowercase = black, uppercase = white)
EMSCRIPTEN_KEEPALIVE
void set_board_from_string(const char* state) {
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            char ch = state[r * 8 + c];
            chess_board[r][c] = "";
            switch (ch) {
                case 'P': chess_board[r][c] = WHITE_PAWN; break;
                case 'R': chess_board[r][c] = WHITE_ROOK; break;
                case 'N': chess_board[r][c] = WHITE_KNIGHT; break;
                case 'B': chess_board[r][c] = WHITE_BISHOP; break;
                case 'Q': chess_board[r][c] = WHITE_QUEEN; break;
                case 'K': chess_board[r][c] = WHITE_KING; break;
                case 'p': chess_board[r][c] = BLACK_PAWN; break;
                case 'r': chess_board[r][c] = BLACK_ROOK; break;
                case 'n': chess_board[r][c] = BLACK_KNIGHT; break;
                case 'b': chess_board[r][c] = BLACK_BISHOP; break;
                case 'q': chess_board[r][c] = BLACK_QUEEN; break;
                case 'k': chess_board[r][c] = BLACK_KING; break;
            }
        }
    }
}

int current_nodes_evaluated = 0;
int current_cache_hits = 0;

EM_JS(int, get_cache_sync, (const char* fen, int depth), {
    var fenStr = UTF8ToString(fen);
    var req = new XMLHttpRequest();
    // Use synchronous XHR
    try {
        req.open("POST", "/api/takshak", false);
        req.setRequestHeader("Content-Type", "application/json");
        req.send(JSON.stringify({ cmd: "GET chess_cache_" + fenStr }));
        if (req.status === 200) {
            var res = JSON.parse(req.responseText);
            if (res.payload && res.payload.indexOf("$-1") === -1 && res.payload !== "") {
                // Parse the RESP response (e.g., "$37\r\n{\"depth\":2,\"score\":-10}\r\n")
                var lines = res.payload.split("\r\n");
                var jsonStr = lines[1] ? lines[1] : res.payload;
                if (jsonStr.startsWith("{")) {
                   var cached = JSON.parse(jsonStr);
                   if (cached.depth >= depth) {
                       return cached.score;
                   }
                }
            }
        }
    } catch(e) { }
    return -999999;
});

EM_JS(void, set_cache_async, (const char* fen, int depth, int score), {
    var fenStr = UTF8ToString(fen);
    var valStr = JSON.stringify({depth: depth, score: score});
    fetch("/api/takshak", {
        method: "POST",
        body: JSON.stringify({ cmd: "SET chess_cache_" + fenStr + " " + valStr }),
        headers: { "Content-Type": "application/json" }
    });
});

EM_JS(void, publish_telemetry, (int nodes, int hits, int depth), {
    var payload = JSON.stringify({ channel: "chess_telemetry", payload: { nodes: nodes, hits: hits, depth: depth } });
    fetch("/api/takshak", {
        method: "POST",
        body: JSON.stringify({ cmd: "PUBLISH telemetry '" + payload + "'" }),
        headers: { "Content-Type": "application/json" }
    });
});

// Returns move as an integer (from_row * 1000 + from_col * 100 + to_row * 10 + to_col)
// e.g., e2 to e4 -> row 6 col 4 to row 4 col 4 -> 6444
EMSCRIPTEN_KEEPALIVE
int get_best_move_wasm(int depth) {
    current_nodes_evaluated = 0;
    current_cache_hits = 0;
    int fr = -1, fc = -1, tr = -1, tc = -1;
    best_move_minimax(chess_board, &fr, &fc, &tr, &tc, depth);
    publish_telemetry(current_nodes_evaluated, current_cache_hits, depth);
    if (fr == -1) return -1;
    return fr * 1000 + fc * 100 + tr * 10 + tc;
}

// Direct JS-readable getters — no need to parse TakshakDB telemetry
EMSCRIPTEN_KEEPALIVE
int get_nodes_evaluated() { return current_nodes_evaluated; }

EMSCRIPTEN_KEEPALIVE
int get_cache_hits_count() { return current_cache_hits; }
