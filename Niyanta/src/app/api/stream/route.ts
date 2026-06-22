import { NextRequest } from 'next/server';
import net from 'net';

export const dynamic = 'force-dynamic';

export async function GET(req: NextRequest) {
  const encoder = new TextEncoder();
  
  const customReadable = new ReadableStream({
    start(controller) {
      const client = new net.Socket();
      
      const host = process.env.TAKSHAK_HOST || '127.0.0.1';
      client.connect(6379, host, () => {
        console.log(`Connected to TakshakDB via TCP for SSE at ${host}:6379`);
        // Subscribe to global telemetry channels
        client.write('SUBSCRIBE telemetry\r\n');
        client.write('SUBSCRIBE rl_telemetry\r\n');
        client.write('SUBSCRIBE amr_map\r\n');
        client.write('SUBSCRIBE amr_odom\r\n');
        client.write('SUBSCRIBE amr_cmd_vel\r\n');
        client.write('SUBSCRIBE amr_nav_status\r\n');
        client.write('SUBSCRIBE amr_path\r\n');
        client.write('SUBSCRIBE amr_scan\r\n');
        
        controller.enqueue(encoder.encode(`data: ${JSON.stringify({ status: 'connected' })}\n\n`));
      });

      let buffer = '';
      client.on('data', (data) => {
        buffer += data.toString();
        
        const lines = buffer.split('\r\n');
        // We need 7 segments per message: *3, $7, message, $<ch_len>, <ch>, $<msg_len>, <msg>
        while (lines.length >= 8) { // A full message has 7 lines + the next item
            if (lines[0] === '*3' && lines[2] === 'message') {
                const channel = lines[4];
                const payload = lines[6];
                
                // Removed debug log writing to hardcoded host filesystem path
                controller.enqueue(encoder.encode(`data: ${JSON.stringify({ channel, payload })}\n\n`));
                
                // Remove the processed 7 lines
                lines.splice(0, 7);
            } else {
                // Handle non-message responses (like subscribe confirmation)
                lines.shift();
            }
        }
        
        // Rejoin the remaining lines to the buffer
        buffer = lines.join('\r\n');
      });

      client.on('close', () => {
        console.log('TakshakDB connection closed');
        try { controller.close(); } catch (e) {}
      });

      client.on('error', (err) => {
        console.error('TakshakDB TCP Error:', err);
        try { controller.enqueue(encoder.encode(`data: ${JSON.stringify({ error: err.message })}\n\n`)); } catch (e) {}
        try { controller.close(); } catch (e) {}
      });

      req.signal.addEventListener('abort', () => {
        console.log('Client disconnected, closing TakshakDB connection');
        client.end();
        try { controller.close(); } catch (e) {}
      });
    }
  });

  return new Response(customReadable, {
    headers: {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache, no-transform',
      'Connection': 'keep-alive',
    },
  });
}
