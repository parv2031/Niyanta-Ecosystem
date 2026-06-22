'use client';

import { useEffect, useRef, useState, useMemo } from 'react';
import { useTelemetry } from '../TelemetryProvider';

export default function AMRPanel() {
  const { channels } = useTelemetry();
  const mapDataStr = channels['amr_map'];
  const odomDataStr = channels['amr_odom'];
  const cmdVelStr = channels['amr_cmd_vel'];
  const pathDataStr = channels['amr_path'];
  const scanDataStr = channels['amr_scan'];

  const mapCanvasRef = useRef<HTMLCanvasElement>(null);
  const robotCanvasRef = useRef<HTMLCanvasElement>(null);
  
  const [clickMode, setClickMode] = useState<'goal' | 'initial' | null>(null);

  // Parse Strings Synchronously with useMemo to avoid setState cascading loops
  const mapInfo = useMemo(() => {
    if (!mapDataStr) return null;
    try { return JSON.parse(mapDataStr); } catch (e) { return null; }
  }, [mapDataStr]);

  const odom = useMemo(() => {
    if (!odomDataStr) return null;
    try { return JSON.parse(odomDataStr); } catch (e) { return null; }
  }, [odomDataStr]);

  const cmdVel = useMemo(() => {
    if (!cmdVelStr) return null;
    try { return JSON.parse(cmdVelStr); } catch (e) { return null; }
  }, [cmdVelStr]);

  const plan = useMemo(() => {
    if (!pathDataStr) return null;
    try { return JSON.parse(pathDataStr); } catch (e) { return null; }
  }, [pathDataStr]);

  const scan = useMemo(() => {
    if (!scanDataStr) return null;
    try { return JSON.parse(scanDataStr); } catch (e) { return null; }
  }, [scanDataStr]);

  // Draw Static Canvas once MapInfo is set and Canvas is mounted
  useEffect(() => {
    if (mapInfo && mapCanvasRef.current) {
      try {
        const payload = mapInfo;
        
        // Decode RLE
        const rle = payload.data_rle;
        const data = new Int8Array(payload.width * payload.height);
        let ptr = 0;
        for (let i = 0; i < rle.length; i += 2) {
          const val = rle[i];
          const count = rle[i+1];
          for (let j = 0; j < count; j++) {
            data[ptr++] = val;
          }
        }

        const ctx = mapCanvasRef.current.getContext('2d');
        if (!ctx) return;
        
        ctx.clearRect(0, 0, payload.width, payload.height);
        
        // We will create an ImageData object for max performance
        const imgData = ctx.createImageData(payload.width, payload.height);
        
        for (let j = 0; j < payload.height; j++) {
          for (let i = 0; i < payload.width; i++) {
            // ROS map is typically row-major starting bottom-left. Canvas is top-left.
            const rosIdx = j * payload.width + i;
            // Flip Y for Canvas
            const canvasIdx = ((payload.height - 1 - j) * payload.width + i) * 4;
            
            const val = data[rosIdx];
            if (val === -1) {
              // Unknown - transparent to show background
              imgData.data[canvasIdx] = 0;
              imgData.data[canvasIdx+1] = 0;
              imgData.data[canvasIdx+2] = 0;
              imgData.data[canvasIdx+3] = 0;
            } else if (val === 0) {
              // Free space - dark slate (slate-800)
              imgData.data[canvasIdx] = 30;
              imgData.data[canvasIdx+1] = 41;
              imgData.data[canvasIdx+2] = 59;
              imgData.data[canvasIdx+3] = 255;
            } else if (val === 100) {
              // Obstacle - bright cyan (cyan-500)
              imgData.data[canvasIdx] = 6;
              imgData.data[canvasIdx+1] = 182;
              imgData.data[canvasIdx+2] = 212;
              imgData.data[canvasIdx+3] = 255;
            } else {
              // Probability gray
              const intensity = 255 - Math.floor((val / 100) * 255);
              imgData.data[canvasIdx] = intensity;
              imgData.data[canvasIdx+1] = intensity;
              imgData.data[canvasIdx+2] = intensity;
              imgData.data[canvasIdx+3] = 255;
            }
          }
        }
        ctx.putImageData(imgData, 0, 0);

      } catch (e) {
        console.error("Map render error", e);
      }
    }
  }, [mapInfo]);

  // Parse Odom & Draw Robot Canvas
  useEffect(() => {
    if (robotCanvasRef.current && mapInfo) {
      try {
        const ctx = robotCanvasRef.current.getContext('2d');
        if (!ctx) return;
        
        // Clear previous frame
        ctx.clearRect(0, 0, mapInfo.width, mapInfo.height);
        
        // Draw Planned Path
        if (plan && plan.path && plan.path.length > 0) {
          ctx.save();
          ctx.beginPath();
          ctx.strokeStyle = '#f59e0b'; // Amber-500 for high visibility
          ctx.lineWidth = Math.max(1, mapInfo.width * 0.003);
          ctx.setLineDash([mapInfo.width * 0.01, mapInfo.width * 0.01]); // Dashed line
          ctx.lineCap = 'round';
          ctx.lineJoin = 'round';
          
          for (let i = 0; i < plan.path.length; i += 2) {
            const rx = plan.path[i];
            const ry = plan.path[i+1];
            const px = (rx - mapInfo.origin_x) / mapInfo.resolution;
            const py = mapInfo.height - ((ry - mapInfo.origin_y) / mapInfo.resolution);
            if (i === 0) ctx.moveTo(px, py);
            else ctx.lineTo(px, py);
          }
          ctx.stroke();
          ctx.restore();
        }

        // Draw Robot
        if (odom) {
          const payload = odom;
          // Convert real-world (x,y) to pixel (px, py)
          const px = (payload.x - mapInfo.origin_x) / mapInfo.resolution;
          const py = mapInfo.height - ((payload.y - mapInfo.origin_y) / mapInfo.resolution);
          
          // Draw Laser Scan BEFORE Robot so it's under the glowing triangle
          if (scan) {
            ctx.save();
            ctx.translate(px, py);
            ctx.rotate(-payload.theta); // -theta because canvas Y is inverted
            
            ctx.fillStyle = '#ef4444'; // Red-500
            const ranges = scan.ranges;
            for (let i = 0; i < ranges.length; i++) {
              const r = ranges[i];
              if (r > scan.range_min && r < scan.range_max) {
                const angle = scan.angle_min + i * scan.angle_increment;
                const ppx = (r * Math.cos(angle)) / mapInfo.resolution;
                const ppy = (-r * Math.sin(angle)) / mapInfo.resolution;
                ctx.fillRect(ppx, ppy, 2, 2);
              }
            }
            ctx.restore();
          }

          // Draw Robot glowing triangle
          ctx.save();
          ctx.translate(px, py);
          ctx.rotate(-payload.theta); // -theta because canvas Y is inverted
          
          ctx.shadowBlur = 10;
          ctx.shadowColor = '#4ade80'; // Green glow
          ctx.fillStyle = '#4ade80';
          
          // Draw a triangle pointing right (since 0 theta is usually along X axis)
          ctx.beginPath();
          const size = Math.max(3, mapInfo.width * 0.02); // dynamically scale robot size
          ctx.moveTo(size, 0);
          ctx.lineTo(-size, size/1.5);
          ctx.lineTo(-size, -size/1.5);
          ctx.closePath();
          ctx.fill();
          
          ctx.restore();
        }
        
      } catch (e) {}
    }
  }, [odom, plan, scan, mapInfo]);

  const navStatus = useMemo(() => {
    const statusStr = channels['amr_nav_status'];
    if (!statusStr) return null;
    try { return JSON.parse(statusStr); } catch (e) { return null; }
  }, [channels['amr_nav_status']]);

  const [dragStart, setDragStart] = useState<{x: number, y: number} | null>(null);
  const [dragCurrent, setDragCurrent] = useState<{x: number, y: number} | null>(null);

  const handleMouseDown = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!clickMode || !mapInfo) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const pos = {
      x: e.clientX - rect.left,
      y: e.clientY - rect.top
    };
    setDragStart(pos);
    setDragCurrent(pos);
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLDivElement>) => {
    if (!dragStart) return;
    const rect = e.currentTarget.getBoundingClientRect();
    setDragCurrent({
      x: e.clientX - rect.left,
      y: e.clientY - rect.top
    });
  };

  const handleMouseUp = async (e: React.MouseEvent<HTMLDivElement>) => {
    if (!clickMode || !mapInfo || !dragStart) return;
    
    const rect = e.currentTarget.getBoundingClientRect();
    const dropX = e.clientX - rect.left;
    const dropY = e.clientY - rect.top;

    const scaleX = mapInfo.width / rect.width;
    const scaleY = mapInfo.height / rect.height;
    
    // Position comes from dragStart (initial click)
    const px = dragStart.x * scaleX;
    const py = dragStart.y * scaleY;

    const rosX = mapInfo.origin_x + (px * mapInfo.resolution);
    const rosY = mapInfo.origin_y + ((mapInfo.height - py) * mapInfo.resolution);

    // Calculate Theta from drag direction (dx, dy)
    // Note: Canvas Y goes down, ROS Y goes up. 
    // We want the angle in ROS world coordinates.
    const dx = (dropX - dragStart.x) * scaleX;
    const dy = (dragStart.y - dropY) * scaleY; // Invert Y for ROS

    let theta = 0.0;
    const dist = Math.sqrt(dx*dx + dy*dy);
    if (dist > 5) { // If dragged more than 5 canvas pixels
      theta = Math.atan2(dy, dx);
    }

    setDragStart(null);
    setDragCurrent(null);

    const payload = { x: rosX, y: rosY, theta: theta };
    const channel = clickMode === 'goal' ? 'amr_goal' : 'amr_initialpose';

    try {
      await fetch('/api/takshak', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cmd: `PUBLISH ${channel} ${JSON.stringify(payload)}` })
      });
      setClickMode(null);
    } catch (err) {
      console.error("Failed to publish pose", err);
    }
  };

  const isConnected = odom !== null;

  return (
    <section className="glass-panel p-5 flex flex-col gap-4">
      <header className="flex justify-between items-center border-b border-slate-700/50 pb-2">
        <h2 className="text-lg font-semibold text-slate-200 flex items-center gap-2">
          <span className="text-green-400 animate-pulse">🤖</span> AMR Navigation Telemetry
        </h2>
        <div className="flex gap-2 items-center">
           <button 
             onClick={() => setClickMode(clickMode === 'initial' ? null : 'initial')}
             className={`px-3 py-1 text-xs font-mono rounded border transition-all ${clickMode === 'initial' ? 'bg-amber-500/20 text-amber-400 border-amber-500/50 shadow-[0_0_8px_rgba(245,158,11,0.3)]' : 'bg-slate-800 text-slate-400 border-slate-700 hover:bg-slate-700'}`}
           >
             📍 INIT POSE
           </button>
           <button 
             onClick={() => setClickMode(clickMode === 'goal' ? null : 'goal')}
             className={`px-3 py-1 text-xs font-mono rounded border transition-all ${clickMode === 'goal' ? 'bg-cyan-500/20 text-cyan-400 border-cyan-500/50 shadow-[0_0_8px_rgba(6,182,212,0.3)]' : 'bg-slate-800 text-slate-400 border-slate-700 hover:bg-slate-700'}`}
           >
             🎯 GOAL POSE
           </button>
           <span className={`text-xs font-mono px-2 py-1 rounded transition-colors ${isConnected ? 'bg-green-500/20 text-green-400 shadow-[0_0_8px_rgba(74,222,128,0.3)]' : 'bg-slate-800 text-slate-500'}`}>
             {isConnected ? 'ONLINE' : 'AWAITING'}
           </span>
        </div>
      </header>
      
      <div className="flex-grow flex flex-col items-center justify-center border border-slate-800 bg-slate-950/50 rounded-lg min-h-[300px] relative overflow-hidden">
        
        {!mapInfo && (
          <p className="text-slate-500 font-mono text-sm animate-pulse z-10">
            [ Listening for /map OccupancyGrid... ]
          </p>
        )}

        {/* Scaled map wrapper */}
        {mapInfo && (
          <div className="relative w-full h-full flex items-center justify-center bg-slate-900 overflow-hidden group">
            <div 
              onMouseDown={handleMouseDown}
              onMouseMove={handleMouseMove}
              onMouseUp={handleMouseUp}
              className={`relative shadow-2xl border rounded-md overflow-hidden bg-slate-950 transition-colors ${clickMode === 'goal' ? 'border-cyan-500 cursor-crosshair' : clickMode === 'initial' ? 'border-amber-500 cursor-crosshair' : 'border-slate-800'}`}
              style={{
                aspectRatio: `${mapInfo.width} / ${mapInfo.height}`,
                maxWidth: '100%',
                maxHeight: '100%'
              }}
            >
              <canvas 
                ref={mapCanvasRef}
                width={mapInfo.width} 
                height={mapInfo.height}
                className="w-full h-full object-contain pointer-events-none block"
              />
              <canvas 
                ref={robotCanvasRef}
                width={mapInfo.width} 
                height={mapInfo.height}
                className="absolute inset-0 w-full h-full object-contain pointer-events-none"
              />
              
              {/* Drag Arrow Overlay */}
              {dragStart && dragCurrent && (
                <svg className="absolute inset-0 w-full h-full pointer-events-none z-50">
                  <defs>
                    <marker id="arrowhead" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
                      <polygon points="0 0, 10 3.5, 0 7" fill={clickMode === 'goal' ? '#06b6d4' : '#f59e0b'} />
                    </marker>
                  </defs>
                  <line 
                    x1={dragStart.x} 
                    y1={dragStart.y} 
                    x2={dragCurrent.x} 
                    y2={dragCurrent.y} 
                    stroke={clickMode === 'goal' ? '#06b6d4' : '#f59e0b'} 
                    strokeWidth="3" 
                    markerEnd="url(#arrowhead)" 
                    strokeDasharray="4 4"
                  />
                </svg>
              )}
            </div>

            {/* Coordinates HUD Overlay */}
            <div className="absolute bottom-2 left-2 flex gap-2 font-mono text-[10px] bg-slate-950/80 p-2 rounded backdrop-blur border border-slate-800/50">
               <div className="flex flex-col">
                 <span className="text-slate-500">X</span>
                 <span className="text-green-400">{odom?.x?.toFixed(2) || '0.00'}</span>
               </div>
               <div className="flex flex-col">
                 <span className="text-slate-500">Y</span>
                 <span className="text-green-400">{odom?.y?.toFixed(2) || '0.00'}</span>
               </div>
               <div className="flex flex-col">
                 <span className="text-slate-500">θ</span>
                 <span className="text-green-400">{odom?.theta?.toFixed(2) || '0.00'}</span>
               </div>
            </div>

            {/* Velocity & Status HUD Overlay */}
            <div className="absolute top-3 right-3 flex flex-col gap-2 font-mono text-xs bg-slate-950/80 p-3 rounded backdrop-blur border border-slate-800/50 min-w-[220px]">
               <div className="flex justify-between items-center border-b border-slate-700/50 pb-1.5 mb-1">
                 <span className="text-slate-400">NAV2</span>
                 <span className="text-cyan-400 animate-pulse font-bold">EXECUTING</span>
               </div>
               
               <div className="flex flex-col gap-1.5">
                 <div className="flex justify-between">
                   <span className="text-slate-500">LIN VEL</span>
                   <span className="text-slate-200">{cmdVel?.linear?.toFixed(2) || '0.00'} m/s</span>
                 </div>
                 <div className="w-full h-1.5 bg-slate-800 rounded overflow-hidden flex relative mb-1">
                    <div className="absolute left-1/2 top-0 h-full w-[1px] bg-slate-600 z-10"></div>
                    <div 
                      className="h-full bg-cyan-400 transition-all duration-100" 
                      style={{ 
                        width: `${Math.min(50, Math.abs(cmdVel?.linear || 0) * 50)}%`, 
                        marginLeft: (cmdVel?.linear || 0) < 0 ? `calc(50% - ${Math.min(50, Math.abs(cmdVel?.linear || 0) * 50)}%)` : '50%' 
                      }}
                    ></div>
                 </div>

                 <div className="flex justify-between mt-1">
                   <span className="text-slate-500">ANG VEL</span>
                   <span className="text-slate-200">{cmdVel?.angular?.toFixed(2) || '0.00'} r/s</span>
                 </div>
                 <div className="w-full h-1.5 bg-slate-800 rounded overflow-hidden flex relative">
                    <div className="absolute left-1/2 top-0 h-full w-[1px] bg-slate-600 z-10"></div>
                    <div 
                      className="h-full bg-amber-400 transition-all duration-100" 
                      style={{ 
                        width: `${Math.min(50, Math.abs((cmdVel?.angular || 0)/2) * 50)}%`, 
                        marginLeft: (cmdVel?.angular || 0) < 0 ? `calc(50% - ${Math.min(50, Math.abs((cmdVel?.angular || 0)/2) * 50)}%)` : '50%' 
                      }}
                    ></div>
                 </div>
               </div>

               {/* Nav2 Status Terminal */}
               <div className="flex flex-col gap-1 mt-2 border-t border-slate-700/50 pt-2.5">
                 <span className="text-[10px] font-bold text-slate-500 uppercase tracking-wider mb-0.5">BEHAVIOR TREE</span>
                 <div className={`px-2.5 py-2 rounded bg-slate-900/50 border flex flex-col ${
                   navStatus?.level === 'error' ? 'border-red-500/50 text-red-400 shadow-[inset_0_0_10px_rgba(239,68,68,0.2)]' :
                   navStatus?.level === 'warning' ? 'border-amber-500/50 text-amber-400 shadow-[inset_0_0_10px_rgba(245,158,11,0.2)]' :
                   navStatus?.level === 'success' ? 'border-green-500/50 text-green-400 shadow-[inset_0_0_10px_rgba(34,197,94,0.2)]' :
                   'border-cyan-500/30 text-cyan-400'
                 }`}>
                   <span className="font-bold text-[11px] mb-1">{navStatus ? navStatus.event : 'IDLE'}</span>
                   <span className="text-[10px] opacity-80 leading-snug">
                     {navStatus ? navStatus.msg.replace(/_/g, ' ') : 'Waiting for goal...'}
                   </span>
                 </div>
               </div>
            </div>
          </div>
        )}
      </div>
    </section>
  );
}
