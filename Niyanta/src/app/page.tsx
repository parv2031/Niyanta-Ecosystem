'use client';

import { useTelemetry } from './TelemetryProvider';
import ChessPanel from './components/ChessPanel';
import TakshakMonitor from './components/TakshakMonitor';
import PowerGridPanel from './components/PowerGridPanel';
import AMRPanel from './components/AMRPanel';

export default function Home() {
  const { status, channels } = useTelemetry();

  return (
    <main className="min-h-screen p-8 flex flex-col gap-8 bg-[radial-gradient(ellipse_at_top,_var(--tw-gradient-stops))] from-slate-900 via-[#030712] to-[#030712]">
      {/* Header */}
      <header className="flex items-center justify-between border-b border-slate-800 pb-4">
        <div>
          <h1 className="text-4xl font-bold tracking-tighter glow-text-cyan text-cyan-400">NIYANTA</h1>
          <p className="text-slate-400 text-sm font-mono mt-1 uppercase tracking-widest">Unified Autonomous Systems Mission Control</p>
        </div>
        <div className="flex items-center gap-4">
          <div className="flex items-center gap-2">
            <div className={`w-3 h-3 rounded-full ${status === 'connected' ? 'bg-green-500 animate-pulse shadow-[0_0_8px_#4ade80]' : status === 'disconnected' ? 'bg-orange-500' : 'bg-red-500 shadow-[0_0_8px_#f87171]'}`}></div>
            <span className={`text-sm font-mono ${status === 'connected' ? 'text-green-400' : status === 'disconnected' ? 'text-orange-400' : 'text-red-400'}`}>
              TAKSHAK.DB {status.toUpperCase()}
            </span>
          </div>
          <div className="text-xs font-mono text-slate-500 bg-slate-900 px-3 py-1 rounded-md border border-slate-800">
            v1.0.0-rc
          </div>
        </div>
      </header>

      {/* Database Monitor Ticker */}
      <TakshakMonitor />

      {/* Grid Layout for Panels */}
      <div className="grid grid-cols-1 lg:grid-cols-2 xl:grid-cols-3 gap-6 flex-grow">
        
        {/* Panel 1: AMR Telemetry (Replaced AEOLUS) */}
        <div className="xl:col-span-2">
          <AMRPanel />
        </div>

        {/* Panel 2: Chess Arena */}
        <div className="xl:col-span-1 xl:row-span-2">
          <ChessPanel />
        </div>

        {/* Panel 3: Power Grid (RL Box spanning 2 cols) */}
        <div className="xl:col-span-2 lg:col-span-2">
          <PowerGridPanel />
        </div>

      </div>
    </main>
  );
}
