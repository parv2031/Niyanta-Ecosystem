'use client';

import { useState, useEffect, useRef } from 'react';
import { Chess } from 'chess.js';
import { useTelemetry } from '../TelemetryProvider';

/* ── Unicode pieces ─────────────────────────────────────────────────── */
const U: Record<string, string> = {
  wK:'♚', wQ:'♛', wR:'♜', wB:'♝', wN:'♞', wP:'♟',
  bK:'♚', bQ:'♛', bR:'♜', bB:'♝', bN:'♞', bP:'♟',
};

const PV: Record<string, number> = { p:100, n:320, b:330, r:500, q:900, k:20000 };
const START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

function evalMat(chess: Chess): number {
  let s = 0;
  chess.board().forEach(row => row.forEach(sq => {
    if (!sq) return;
    s += (sq.color === 'w' ? 1 : -1) * (PV[sq.type] ?? 0);
  }));
  return s;
}

/** Build the 64-char ASCII board string the WASM engine expects */
function boardStr(chess: Chess): string {
  const board = chess.board();
  let s = '';
  for (let r = 0; r < 8; r++) {
    const jr = 7 - r;
    for (let c = 0; c < 8; c++) {
      const p = board[jr][c];
      s += p ? (p.color === 'w' ? p.type.toUpperCase() : p.type) : ' ';
    }
  }
  return s;
}

/** One snapshot per half-move (ply) */
interface Snap {
  fen:     string;
  label:   string;      // e.g. "e2→e4"
  capW:    string[];    // cumulative black pieces captured by white
  capB:    string[];    // cumulative white pieces captured by black
  nodes:   number;
  hits:    number;
  ms:      number;
}

export default function ChessPanel() {
  /* Ordered history of game states. snaps[0] is start position. */
  const [snaps,   setSnaps]   = useState<Snap[]>([{
    fen: START, label: 'Start', capW: [], capB: [], nodes: 0, hits: 0, ms: 0
  }]);
  /* cursor points to the snap currently being viewed/played */
  const [cursor,  setCursor]  = useState(0);
  /* selected square for click-to-move — only used when cursor === tip */
  const [sel,     setSel]     = useState<string|null>(null);
  /* engine */
  const [ready,   setReady]   = useState(false);
  const [thinking,setThinking]= useState(false);
  const [status,  setStatus]  = useState('Loading WASM…');
  const [depth,   setDepth]   = useState(3);
  const workerRef  = useRef<Worker | null>(null);
  const pendingRef = useRef<((r: {mv:number,nodes:number,hits:number,ms:number}) => void) | null>(null);
  const { channels } = useTelemetry();

  const tip        = snaps.length - 1;
  // Clamp cursor so a stale setCursor never overshoots the snaps array
  // (React flushes state updates separately, causing a brief mismatch)
  const safeCursor = Math.min(Math.max(0, cursor), tip);
  const atTip      = safeCursor === tip;
  const inReview   = !atTip;

  const currentSnap = snaps[safeCursor]!;
  const chess        = new Chess(currentSnap.fen);
  const score        = evalMat(chess);
  const evalPct      = Math.min(100, Math.max(0, 50 + score / 20));

  /* Legal move squares for selected piece */
  const legal = (sel && atTip && !thinking)
    ? new Set(chess.moves({ square: sel as any, verbose: true }).map((m: any) => m.to))
    : new Set<string>();

  /* ── Spin up Web Worker once ─────────────────────────────────────── */
  useEffect(() => {
    const worker = new Worker('/chess_worker.js');
    workerRef.current = worker;

    worker.onmessage = (e) => {
      const { type, message, mv, nodes, hits, ms } = e.data;
      if (type === 'ready') {
        setReady(true);
        setStatus('You play White — click a piece');
      } else if (type === 'error') {
        setStatus('Worker error: ' + message);
      } else if (type === 'result' && pendingRef.current) {
        pendingRef.current({ mv, nodes, hits, ms });
        pendingRef.current = null;
      }
    };
    worker.onerror = (e) => setStatus('Worker crashed: ' + e.message);

    return () => worker.terminate();
  }, []);

  /** Send a search to the worker; resolves with the result */
  function searchAsync(fen: string, d: number): Promise<{mv:number,nodes:number,hits:number,ms:number}> {
    return new Promise((resolve) => {
      pendingRef.current = resolve;
      workerRef.current!.postMessage({ type: 'search', boardStr: boardStr(new Chess(fen)), depth: d });
    });
  }

  /* ── Engine call (async, runs in Worker) ────────────────────────── */
  async function runEngine(currentFen: string, prevCapW: string[], prevCapB: string[], d: number) {
    try {
      const { mv, nodes, hits, ms } = await searchAsync(currentFen, d);
      setThinking(false);

      if (mv === -1) { setStatus('Checkmate / Stalemate'); return; }

      const fr = (mv / 1000) | 0, fc = ((mv % 1000) / 100) | 0;
      const tr = ((mv % 100) / 10) | 0, tc = mv % 10;
      const F = ['a','b','c','d','e','f','g','h'];
      const R = ['1','2','3','4','5','6','7','8'];
      const from = F[fc] + R[fr], to = F[tc] + R[tr];

      const reply = new Chess(currentFen);
      const capPiece = reply.get(to as any);
      let ok = false;
      try { reply.move({ from, to }); ok = true; }
      catch (_) { try { reply.move({ from, to, promotion: 'q' }); ok = true; } catch (_2) {} }

      if (!ok) { setStatus(`Bad engine move: ${from}→${to}`); return; }

      const newCapW = capPiece ? [...prevCapW, capPiece.type] : [...prevCapW];
      const newSnap: Snap = {
        fen: reply.fen(), label: `${from}→${to}`,
        capW: newCapW, capB: [...prevCapB],
        nodes, hits, ms,
      };

      setSnaps(prev => [...prev, newSnap]);
      setCursor(c => c + 1);

      setStatus(
        reply.isCheckmate() ? '✕ Checkmate — Black wins!' :
        reply.isCheck()     ? `⚡ Check! Engine: ${from}→${to}` :
        reply.isStalemate() ? '½ Stalemate' :
        `Engine: ${from}→${to} (${ms}ms · ${nodes.toLocaleString()} nodes)`
      );
    } catch (e: any) {
      setThinking(false);
      setStatus('Worker error: ' + e.message);
    }
  }

  /* ── Square click (only active at tip, human's turn) ─────────────── */
  function click(sq: string) {
    if (!ready || thinking || inReview) return;
    if (chess.isGameOver()) return;

    if (sel) {
      const capPiece = chess.get(sq as any);
      let ok = false;
      const copy = new Chess(currentSnap.fen);
      try { copy.move({ from: sel, to: sq }); ok = true; }
      catch (_) { try { copy.move({ from: sel, to: sq, promotion: 'q' }); ok = true; } catch (_2) {} }

      if (ok) {
        const newCapB = capPiece ? [...currentSnap.capB, capPiece.type] : [...currentSnap.capB];
        const humanSnap: Snap = {
          fen: copy.fen(), label: `${sel}→${sq}`,
          capW: [...currentSnap.capW], capB: newCapB,
          nodes: 0, hits: 0, ms: 0,
        };
        const newSnaps = [...snaps.slice(0, cursor + 1), humanSnap];
        setSnaps(newSnaps);
        setCursor(cursor + 1);
        setSel(null);

        if (copy.isCheckmate()) { setStatus('✕ Checkmate — White wins!'); return; }
        if (copy.isStalemate()) { setStatus('½ Stalemate'); return; }

        setThinking(true);
        setStatus(`Engine thinking at depth ${depth}…`);
        runEngine(humanSnap.fen, humanSnap.capW, humanSnap.capB, depth);
        return;
      }
      setSel(null);
    }

    // Select a white piece
    if (chess.turn() === 'w') {
      const p = chess.get(sq as any);
      if (p && p.color === 'w') setSel(sq);
    }
  }

  /* ── Undo / Redo (read-only navigation) ─────────────────────────── */
  function undo() {
    if (safeCursor > 0) { setCursor(safeCursor - 1); setSel(null); }
  }
  function redo() {
    if (safeCursor < tip) { setCursor(safeCursor + 1); setSel(null); }
  }

  /* ── Reset ───────────────────────────────────────────────────────── */
  function reset() {
    setSnaps([{ fen: START, label: 'Start', capW: [], capB: [], nodes: 0, hits: 0, ms: 0 }]);
    setCursor(0);
    setSel(null);
    setThinking(false);
    setStatus('You play White — click a piece');
  }

  /* ── Board squares ───────────────────────────────────────────────── */
  const squares = [];
  for (let rank = 7; rank >= 0; rank--) {
    for (let file = 0; file < 8; file++) {
      const sq = String.fromCharCode(97 + file) + (rank + 1);
      squares.push({ sq, piece: chess.get(sq as any), rank, file });
    }
  }

  const moveList = snaps.slice(1).map(s => s.label);

  return (
    <section className="glass-panel p-4 flex flex-col gap-3">
      {/* Header */}
      <header className="flex justify-between items-center border-b border-slate-700/50 pb-2">
        <h2 className="text-base font-semibold text-slate-200 flex items-center gap-2">
          <span className="text-blue-400">♟</span> Chess AI Arena
        </h2>
        <div className="flex items-center gap-2">
          {inReview && (
            <span className="text-[10px] font-mono px-1.5 py-0.5 rounded bg-amber-500/15 text-amber-400 border border-amber-500/30">
              REVIEW {cursor}/{tip}
            </span>
          )}
          {thinking && <span className="flex gap-0.5">{[0,1,2].map(i => (
            <span key={i} className="w-1 h-1 rounded-full bg-blue-400 animate-bounce"
              style={{ animationDelay: `${i*0.15}s` }} />
          ))}</span>}
          <span className={`text-[10px] font-mono px-1.5 py-0.5 rounded ${
            ready ? 'bg-green-500/10 text-green-400' : 'bg-slate-700 text-slate-400'
          }`}>{ready ? 'WASM LIVE' : 'LOADING'}</span>
        </div>
      </header>

      {/* Board + eval bar */}
      <div className="flex gap-2 items-stretch">
        {/* Eval bar */}
        <div className="flex flex-col items-center gap-0.5 flex-shrink-0" style={{ width: 10 }}>
          <span className="text-[8px] text-slate-600">W</span>
          <div className="flex-1 w-full rounded overflow-hidden border border-slate-700/60"
            style={{ position: 'relative', minHeight: 0 }}>
            <div style={{ position:'absolute', inset:0, background:'#1e293b' }}/>
            <div style={{
              position:'absolute', bottom:0, left:0, right:0,
              height:`${evalPct}%`,
              background: score >= 0
                ? 'linear-gradient(to top,#f1f5f9,#94a3b8)'
                : 'linear-gradient(to top,#334155,#1e293b)',
              transition: 'height 0.4s ease',
            }}/>
          </div>
          <span className="text-[8px] text-slate-600">B</span>
        </div>

        {/* Board */}
        <div className="flex-1 min-w-0">
          <div className="w-full" style={{ aspectRatio: '1/1' }}>
            <div className="w-full h-full border border-slate-600 rounded overflow-hidden select-none"
              style={{ display:'grid', gridTemplateColumns:'repeat(8,1fr)', gridTemplateRows:'repeat(8,1fr)' }}>
              {squares.map(({ sq, piece, rank, file }) => {
                const light  = (file + rank) % 2 !== 0;
                const isSel  = sq === sel;
                const isLeg  = legal.has(sq);
                let bg = light ? '#b58863' : '#f0d9b5';
                if (isSel) bg = '#f6f669';
                else if (isLeg && piece) bg = '#cc4444';
                else if (isLeg) bg = light ? '#cdd26a' : '#aaa23a';
                const key = piece ? `${piece.color}${piece.type.toUpperCase()}` : null;
                return (
                  <div key={sq} onClick={() => click(sq)} style={{
                    backgroundColor: bg,
                    cursor: ready && !thinking && !inReview ? 'pointer' : 'default',
                    display:'flex', alignItems:'center', justifyContent:'center',
                    position:'relative', overflow:'hidden',
                  }}>
                    {key && (
                      <span style={{
                        fontSize: 'min(7vw, 56px)', lineHeight: 1, userSelect: 'none',
                        color: piece!.color === 'w' ? '#fff' : '#111',
                        textShadow: piece!.color === 'w' ? '0 1px 2px rgba(0,0,0,0.4)' : 'none',
                      }}>{U[key]}</span>
                    )}
                    {isLeg && !piece && (
                      <span style={{
                        width:'28%', height:'28%', borderRadius:'50%',
                        backgroundColor:'rgba(0,0,0,0.25)', position:'absolute',
                      }}/>
                    )}
                    {file === 0 && (
                      <span style={{
                        position:'absolute', top:1, left:2, fontSize:'min(1.2vw,8px)',
                        color: light ? '#8b5e3c' : '#b58863', fontWeight:700, lineHeight:1,
                      }}>{rank+1}</span>
                    )}
                    {rank === 0 && (
                      <span style={{
                        position:'absolute', bottom:1, right:2, fontSize:'min(1.2vw,8px)',
                        color: light ? '#8b5e3c' : '#b58863', fontWeight:700, lineHeight:1,
                      }}>{sq[0]}</span>
                    )}
                  </div>
                );
              })}
            </div>
          </div>
        </div>
      </div>

      {/* Status */}
      <div className={`rounded px-2 py-1 text-[11px] font-mono leading-snug border ${
        chess.isCheckmate() ? 'border-red-500/50 bg-red-500/10 text-red-400' :
        chess.isCheck()     ? 'border-yellow-500/50 bg-yellow-500/10 text-yellow-300' :
        thinking            ? 'border-blue-500/30 bg-blue-500/10 text-blue-400' :
        inReview            ? 'border-amber-500/30 bg-amber-500/5 text-amber-400' :
                              'border-slate-700 bg-slate-900/60 text-slate-400'
      }`}>
        {inReview ? `📼 Review mode — use ↩ ↪ to navigate (move ${cursor} of ${tip})` : status}
      </div>

      {/* Compact HUD */}
      <div className="grid grid-cols-2 gap-2 text-[11px] font-mono">

        {/* Eval + Turn */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 flex flex-col gap-1">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider">Eval / Turn</div>
          <div className="flex justify-between items-center">
            <span className={score >= 0 ? 'text-slate-200' : 'text-slate-400'}>
              {score >= 0 ? '+' : ''}{(score/100).toFixed(2)}
            </span>
            <div className="flex items-center gap-1">
              <span className={`w-2 h-2 rounded-full border ${
                chess.turn()==='w' ? 'bg-white border-slate-300' : 'bg-slate-900 border-slate-400'
              }`}/>
              <span className="text-slate-400">{chess.turn()==='w' ? 'White' : 'Black'}</span>
            </div>
          </div>
        </div>

        {/* Depth */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 flex flex-col gap-1">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider">Search Depth</div>
          <div className="flex gap-1">
            {[1,2,3,4].map(d => (
              <button key={d} onClick={() => setDepth(d)} disabled={thinking}
                className={`flex-1 rounded text-[10px] py-0.5 transition-colors ${
                  depth===d ? 'bg-blue-600 text-white' : 'bg-slate-800 text-slate-400 hover:bg-slate-700'
                }`}>{d}</button>
            ))}
          </div>
        </div>

        {/* Minimax metrics — for current snapshot (engine moves only) */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 col-span-2 flex flex-col gap-0.5">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider mb-0.5">Minimax α-β · Last engine move</div>
          <div className="grid grid-cols-3 gap-x-2">
            {[
              { l:'Move Time',  v: currentSnap.ms   > 0 ? `${currentSnap.ms}ms`             : '—', c:'text-cyan-400'   },
              { l:'Nodes',      v: currentSnap.nodes > 0 ? currentSnap.nodes.toLocaleString() : '—', c:'text-purple-400' },
              { l:'Cache Hits', v: currentSnap.hits  > 0 ? currentSnap.hits.toLocaleString()  : '—', c:'text-orange-400' },
            ].map(({ l, v, c }) => (
              <div key={l} className="flex flex-col">
                <span className="text-[9px] text-slate-600">{l}</span>
                <span className={c}>{v}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Piece values */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 col-span-2">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider mb-1">Piece Values (centipawns)</div>
          <div className="grid grid-cols-6 gap-0.5 text-center">
            {[['♙','100'],['♘','320'],['♗','330'],['♖','500'],['♛','900'],['♔','20k']].map(([sym,val]) => (
              <div key={sym} className="flex flex-col items-center">
                <span className="text-slate-300 text-sm leading-none">{sym}</span>
                <span className="text-[9px] text-slate-500">{val}</span>
              </div>
            ))}
          </div>
        </div>

        {/* Captured — reflects current snapshot */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 col-span-2">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider mb-1">Captured</div>
          <div className="flex justify-between gap-2">
            <div className="flex-1">
              <div className="text-[9px] text-slate-600 mb-0.5">White took</div>
              <div className="flex flex-wrap gap-0.5 min-h-[16px]">
                {currentSnap.capW.length
                  ? currentSnap.capW.map((t, i) => (
                      <span key={i} className="text-slate-200 leading-none" style={{ fontSize:14 }}>
                        {U[`b${t.toUpperCase()}`]}
                      </span>
                    ))
                  : <span className="text-slate-700 text-[10px]">—</span>
                }
              </div>
            </div>
            <div className="flex-1 text-right">
              <div className="text-[9px] text-slate-600 mb-0.5">Black took</div>
              <div className="flex flex-wrap gap-0.5 min-h-[16px] justify-end">
                {currentSnap.capB.length
                  ? currentSnap.capB.map((t, i) => (
                      <span key={i} className="text-white leading-none" style={{ fontSize:14 }}>
                        {U[`w${t.toUpperCase()}`]}
                      </span>
                    ))
                  : <span className="text-slate-700 text-[10px]">—</span>
                }
              </div>
            </div>
          </div>
        </div>

        {/* Move history — scrollable, current position highlighted */}
        <div className="rounded border border-slate-700 bg-slate-900/60 px-2 py-1.5 col-span-2">
          <div className="text-[9px] text-slate-500 uppercase tracking-wider mb-1">Move History</div>
          <div className="flex flex-wrap gap-1 max-h-12 overflow-y-auto">
            {moveList.length === 0
              ? <span className="text-slate-700 text-[10px]">—</span>
              : moveList.map((m, i) => {
                  const snapIdx = i + 1;
                  const isCurrent = snapIdx === cursor;
                  return (
                    <span
                      key={i}
                      onClick={() => setCursor(snapIdx)}
                      title={`Go to move ${snapIdx}`}
                      className={`text-[10px] px-1 rounded cursor-pointer transition-colors ${
                        isCurrent
                          ? 'bg-blue-500 text-white'
                          : i % 2 === 0
                            ? 'bg-slate-800 text-slate-300 hover:bg-slate-700'
                            : 'bg-blue-900/30 text-blue-300 hover:bg-blue-900/50'
                      }`}
                    >
                      {Math.floor(i/2)+1}{i%2===0?'.':''} {m}
                    </span>
                  );
                })
            }
          </div>
        </div>
      </div>

      {/* Controls */}
      <div className="flex gap-2">
        <button id="chess-undo-btn" onClick={undo} disabled={cursor === 0}
          title="Step back one move"
          className="flex-1 py-1.5 bg-slate-700 hover:bg-slate-600 disabled:opacity-30 disabled:cursor-not-allowed text-slate-300 text-[11px] font-mono rounded transition-colors">
          ↩ Undo
        </button>
        <button id="chess-redo-btn" onClick={redo} disabled={cursor === tip}
          title="Step forward one move"
          className="flex-1 py-1.5 bg-slate-700 hover:bg-slate-600 disabled:opacity-30 disabled:cursor-not-allowed text-slate-300 text-[11px] font-mono rounded transition-colors">
          ↪ Redo
        </button>
        {inReview && (
          <button onClick={() => { setCursor(tip); setSel(null); }}
            className="flex-1 py-1.5 bg-blue-700 hover:bg-blue-600 text-white text-[11px] font-mono rounded transition-colors">
            ▶ Resume
          </button>
        )}
        <button id="chess-reset-btn" onClick={reset}
          className="px-3 py-1.5 bg-red-900/40 hover:bg-red-900/60 text-red-400 text-[11px] font-mono rounded transition-colors">
          Reset
        </button>
      </div>
    </section>
  );
}
