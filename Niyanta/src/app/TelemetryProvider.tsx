'use client';

import React, { createContext, useContext, useEffect, useState } from 'react';

type TelemetryState = {
  status: 'disconnected' | 'connected' | 'error';
  channels: Record<string, any>;
};

const TelemetryContext = createContext<TelemetryState>({
  status: 'disconnected',
  channels: {},
});

export function TelemetryProvider({ children }: { children: React.ReactNode }) {
  const [status, setStatus] = useState<TelemetryState['status']>('disconnected');
  const [channels, setChannels] = useState<Record<string, any>>({});
  
  // Decouple high-frequency network events from React's render cycle
  const latestChannelsRef = React.useRef<Record<string, any>>({});

  useEffect(() => {
    let eventSource: EventSource | null = null;
    let timeoutId: NodeJS.Timeout;

    // Async recursive loop to flush network data to React state
    // setTimeout prevents browser queueing if the main thread blocks during heavy canvas paints
    const syncState = () => {
      setChannels(prev => {
        let hasChanges = false;
        for (const key in latestChannelsRef.current) {
           if (latestChannelsRef.current[key] !== prev[key]) {
              hasChanges = true;
              break;
           }
        }
        if (hasChanges) {
           return { ...prev, ...latestChannelsRef.current };
        }
        return prev;
      });
      timeoutId = setTimeout(syncState, 33); // ~30 FPS is plenty for smooth UI
    };
    
    timeoutId = setTimeout(syncState, 33);
    
    const connect = () => {
      eventSource = new EventSource('/api/stream');
      
      eventSource.onopen = () => {
         setStatus('connected');
      };

      eventSource.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          
          if (data.status) {
             setStatus(data.status);
          } else if (data.channel) {
             // Mutate the ref instantly without triggering React's update queue
             latestChannelsRef.current[data.channel] = data.payload;
          } else if (data.error) {
             setStatus('error');
          }
        } catch (e) {
          console.error('Failed to parse SSE data', e);
        }
      };

      eventSource.onerror = () => {
        setStatus('error');
        eventSource?.close();
        // Retry connection after 5s
        setTimeout(connect, 5000);
      };
    };

    connect();

    return () => {
      eventSource?.close();
      clearTimeout(timeoutId);
    };
  }, []);

  return (
    <TelemetryContext.Provider value={{ status, channels }}>
      {children}
    </TelemetryContext.Provider>
  );
}

export const useTelemetry = () => useContext(TelemetryContext);
