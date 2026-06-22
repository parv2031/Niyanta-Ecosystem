'use client';

import { useState, useEffect } from 'react';
import { useTelemetry } from '../TelemetryProvider';

interface RLTelemetry {
  obs: number[];
  reward: number;
  step: number;
  metrics: {
    n_overloaded: number;
    avg_util: number;
    max_severity: number;
    power_shed_est?: number;
  };
}

interface Point {
  step: number;
  reward: number;
  overloads: number;
}

export default function PowerGridPanel() {
  const { channels } = useTelemetry();
  const dataStr = channels['rl_telemetry'];
  
  const [history, setHistory] = useState<Point[]>([]);
  const [latest, setLatest] = useState<RLTelemetry | null>(null);
  const [isConnected, setIsConnected] = useState(false);

  useEffect(() => {
    if (dataStr) {
      setIsConnected(true);
      try {
        const d: RLTelemetry = JSON.parse(dataStr);
        setLatest(d);
        setHistory(prev => {
          const next = [...prev, { step: d.step, reward: d.reward, overloads: d.metrics.n_overloaded }];
          if (next.length > 60) next.shift(); // Keep last 60 points
          return next;
        });
      } catch (e) {}

      const timer = setTimeout(() => setIsConnected(false), 2000);
      return () => clearTimeout(timer);
    }
  }, [dataStr]);

  if (!latest) {
    return (
      <section className="glass-panel p-5 flex flex-col gap-4 col-span-1 xl:col-span-2 min-h-[400px]">
        <header className="flex justify-between items-center border-b border-slate-700/50 pb-2">
          <h2 className="text-lg font-semibold text-slate-200 flex items-center gap-2">
            <span className="text-orange-400 animate-pulse">⚡</span> Power Grid Operations
          </h2>
          <span className={`text-xs font-mono px-2 py-1 rounded transition-colors ${isConnected ? 'bg-green-500/20 text-green-400 shadow-[0_0_8px_rgba(74,222,128,0.3)]' : 'bg-orange-500/10 text-orange-400'}`}>
            {isConnected ? 'SAC RL AGENT (ONLINE)' : 'SAC RL AGENT (OFFLINE)'}
          </span>
        </header>
        <div className="flex-grow flex items-center justify-center border border-slate-800 bg-slate-950/50 rounded-lg">
          <p className="text-slate-500 font-mono text-sm animate-pulse">Waiting for RL Telemetry Stream...</p>
        </div>
      </section>
    );
  }

  // Parse obs array
  const N_SUBS = 100;
  const N_FEATS = 8;
  const n_edges = latest.obs.length - N_SUBS - N_FEATS;
  const edgeUtils = latest.obs.slice(0, n_edges);

  // Fixed SVG Chart Scaling: -5 to 5
  const maxReward = 5;
  const minReward = -5;
  const range = maxReward - minReward; // 10
  
  const chartW = 400;
  const chartH = 200;

  // Generate SVG Path for Reward
  const points = history.map((h, i) => {
    const x = history.length > 1 ? (i / (history.length - 1)) * chartW : chartW;
    // Cap values between -5 and 5 to prevent going completely out of bounds even with overflow-hidden
    const clampedReward = Math.max(minReward, Math.min(maxReward, h.reward));
    const y = chartH - ((clampedReward - minReward) / range) * chartH;
    return `${x},${y}`;
  }).join(' L ');

  const pathD = history.length > 0 ? `M ${points}` : '';

  return (
    <section className="glass-panel p-5 flex flex-col gap-4 col-span-1 xl:col-span-2">
      <header className="flex justify-between items-center border-b border-slate-700/50 pb-2">
        <h2 className="text-lg font-semibold text-slate-200 flex items-center gap-2">
          <span className="text-orange-400">⚡</span> Power Grid Operations
        </h2>
        <span className={`text-xs font-mono px-2 py-1 rounded transition-colors ${isConnected ? 'bg-green-500/20 text-green-400 shadow-[0_0_8px_rgba(74,222,128,0.3)]' : 'bg-orange-500/10 text-orange-400'}`}>
          {isConnected ? 'SAC RL AGENT (ONLINE)' : 'SAC RL AGENT (OFFLINE)'}
        </span>
      </header>
      
      <div className="flex flex-col gap-4">
        {/* Top Row: Metrics and Heatmap */}
        <div className="flex gap-4 flex-col lg:flex-row">
          
          {/* Left: Metrics HUD */}
          <div className="flex-1 grid grid-cols-2 gap-2 font-mono text-xs">
            <div className="bg-slate-900/50 border border-slate-800 p-2 rounded flex flex-col justify-center">
              <span className="text-slate-500 block mb-1">STEP</span>
              <span className="text-slate-200 text-lg">{latest.step.toLocaleString()}</span>
            </div>
            <div className="bg-slate-900/50 border border-slate-800 p-2 rounded flex flex-col justify-center">
              <span className="text-slate-500 block mb-1">RL REWARD</span>
              <span className={`text-lg ${latest.reward < 0 ? 'text-red-400' : 'text-green-400'}`}>
                {latest.reward.toFixed(2)}
              </span>
            </div>
            <div className={`bg-slate-900/50 border ${latest.metrics.n_overloaded > 0 ? 'border-red-500/50 bg-red-950/20' : 'border-slate-800'} p-2 rounded flex flex-col justify-center`}>
              <span className="text-slate-500 block mb-1">OVERLOADED</span>
              <span className={`text-lg ${latest.metrics.n_overloaded > 0 ? 'text-red-400 font-bold' : 'text-slate-200'}`}>
                {latest.metrics.n_overloaded} <span className="text-xs font-normal">edges</span>
              </span>
            </div>
            <div className="bg-slate-900/50 border border-slate-800 p-2 rounded flex flex-col justify-center">
              <span className="text-slate-500 block mb-1">AVG LOAD</span>
              <span className="text-cyan-400 text-lg">
                {(latest.metrics.avg_util * 100).toFixed(1)}%
              </span>
            </div>
          </div>

          {/* Right: Grid Edge Heatmap */}
          <div className="flex-[2] bg-slate-950 border border-slate-800 rounded-lg p-3 flex flex-col min-h-[120px]">
            <div className="flex justify-between items-end mb-3">
               <span className="text-[10px] font-mono text-slate-500 uppercase tracking-widest">Edge Load Heatmap</span>
               <span className="text-[10px] font-mono text-slate-600">{n_edges} ACTIVE EDGES</span>
            </div>
            
            <div className="flex-grow flex content-start flex-wrap gap-1 overflow-hidden">
              {edgeUtils.map((util, i) => {
                const absUtil = Math.abs(util);
                let color = 'bg-slate-800';
                if (absUtil > 1.0) color = 'bg-red-500 shadow-[0_0_5px_#ef4444]';
                else if (absUtil > 0.8) color = 'bg-orange-400';
                else if (absUtil > 0.3) color = 'bg-cyan-500';
                else if (absUtil > 0.01) color = 'bg-cyan-900';
                
                return (
                  <div 
                    key={i} 
                    className={`w-3 h-3 ${color} rounded-sm`}
                    title={`Edge ${i}: ${(absUtil*100).toFixed(1)}%`}
                  />
                );
              })}
            </div>
          </div>
        </div>

        {/* Bottom Row: Full Width Line Chart */}
        <div className="w-full bg-slate-950 border border-slate-800 rounded-lg p-3 relative h-[220px] flex flex-col">
          <span className="text-[10px] font-mono text-slate-500 uppercase tracking-widest absolute top-2 right-3">Reward History</span>
          <div className="flex-grow w-full relative mt-4 overflow-hidden">
            <svg viewBox={`0 0 ${chartW} ${chartH}`} className="w-full h-full overflow-hidden" preserveAspectRatio="none">
              <line 
                x1="0" y1={chartH - ((0 - minReward) / range) * chartH} 
                x2={chartW} y2={chartH - ((0 - minReward) / range) * chartH} 
                stroke="#334155" strokeWidth="1" strokeDasharray="4 4" 
              />
              <path 
                d={pathD} 
                fill="none" 
                stroke="#fb923c" 
                strokeWidth="2" 
                strokeLinejoin="round"
              />
            </svg>
          </div>
        </div>
      </div>
    </section>
  );
}
