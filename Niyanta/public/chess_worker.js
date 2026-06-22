// chess_worker.js — runs off the main thread so depth-4 searches don't freeze the UI

importScripts('/chess_engine.js');

var engine = null;

createChessModule().then(function(mod) {
  engine = mod;
  mod._init_engine();
  self.postMessage({ type: 'ready' });
}).catch(function(e) {
  self.postMessage({ type: 'error', message: String(e) });
});

self.onmessage = function(e) {
  if (e.data.type !== 'search' || !engine) return;

  var boardStr = e.data.boardStr;  // 64-char ASCII board state
  var depth    = e.data.depth;

  // The board string is pure ASCII (64 chars + null), so byte length = length + 1.
  // Use the Emscripten-exported stringToUTF8 — avoids HEAPU8 initialisation race.
  var byteLen = boardStr.length + 1;
  var ptr = engine._malloc(byteLen);
  engine.stringToUTF8(boardStr, ptr, byteLen);

  engine._set_board_from_string(ptr);

  var t0    = performance.now();
  var mv    = engine._get_best_move_wasm(depth);
  var ms    = Math.round(performance.now() - t0);
  var nodes = engine._get_nodes_evaluated  ? engine._get_nodes_evaluated()   : 0;
  var hits  = engine._get_cache_hits_count ? engine._get_cache_hits_count()  : 0;

  engine._free(ptr);

  self.postMessage({ type: 'result', mv: mv, nodes: nodes, hits: hits, ms: ms });
};
