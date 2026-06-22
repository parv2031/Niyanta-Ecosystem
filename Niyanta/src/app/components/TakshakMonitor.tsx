'use client';

import { useState, useEffect } from 'react';
import { useTelemetry } from '../TelemetryProvider';

interface TakshakInfo {
  uptime_seconds: number;
  active_connections: number;
  total_keys: number;
  max_capacity: number;
  cache_hits: number;
  cache_misses: number;
  evictions: number;
}

export default function TakshakMonitor() {
  const { status } = useTelemetry();
  const [info, setInfo] = useState<TakshakInfo | null>(null);

  useEffect(() => {
    if (status !== 'connected') return;

    const fetchInfo = async () => {
      try {
        const res = await fetch('/api/takshak', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ cmd: 'INFO' })
        });
        const data = await res.json();
        if (data.payload) {
          // data.payload is a RESP string like "$89\r\n{\"uptime_seconds\":...}\r\n"
          const lines = data.payload.split('\r\n');
          const jsonStr = lines[1] || data.payload;
          if (jsonStr.startsWith('{')) {
            setInfo(JSON.parse(jsonStr));
          }
        }
      } catch (err) {
        console.error('Failed to fetch Takshak INFO:', err);
      }
    };

    fetchInfo();
    const interval = setInterval(fetchInfo, 1000);
    return () => clearInterval(interval);
  }, [status]);

  if (!info) return null;

  const hitRate = info.cache_hits + info.cache_misses > 0 
    ? ((info.cache_hits / (info.cache_hits + info.cache_misses)) * 100).toFixed(1)
    : '0.0';

  const usagePct = ((info.total_keys / info.max_capacity) * 100).toFixed(2);

  const formatUptime = (sec: number) => {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
  };

  return (
    <div className="w-full glass-panel border border-cyan-900/50 bg-cyan-950/20 px-4 py-3 flex flex-wrap items-center justify-between gap-4 font-mono text-xs mb-6">
      <div className="flex items-center gap-3">
        <span className="text-cyan-400 font-bold tracking-widest uppercase flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-cyan-400 animate-pulse"></span>
          TakshakDB Health
        </span>
        <span className="text-slate-500">|</span>
        <span className="text-slate-300">UPTIME: <span className="text-cyan-300">{formatUptime(info.uptime_seconds)}</span></span>
      </div>

      <div className="flex items-center gap-6">
        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Connections</span>
          <span className="text-slate-200 text-sm">{info.active_connections} TCP</span>
        </div>

        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Memory (Keys)</span>
          <div className="flex items-center gap-2">
            <span className="text-slate-200 text-sm">{info.total_keys.toLocaleString()}</span>
            <span className="text-slate-500 text-[10px]">/ {info.max_capacity.toLocaleString()} ({usagePct}%)</span>
          </div>
        </div>

        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Cache Hits</span>
          <span className="text-green-400 text-sm">{info.cache_hits.toLocaleString()}</span>
        </div>

        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Cache Misses</span>
          <span className="text-orange-400 text-sm">{info.cache_misses.toLocaleString()}</span>
        </div>

        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Hit Rate</span>
          <span className="text-cyan-300 text-sm">{hitRate}%</span>
        </div>

        <div className="flex flex-col">
          <span className="text-slate-500 text-[10px] uppercase tracking-wider">Evictions</span>
          <span className="text-red-400 text-sm">{info.evictions.toLocaleString()}</span>
        </div>
      </div>
    </div>
  );
}
