import { NextRequest, NextResponse } from 'next/server';
import net from 'net';

export async function POST(req: NextRequest) {
  try {
    const { cmd } = await req.json();
    if (!cmd) {
      return NextResponse.json({ error: 'Missing cmd' }, { status: 400 });
    }

    return new Promise<NextResponse>((resolve) => {
      const client = new net.Socket();
      let responseData = '';

      const host = process.env.TAKSHAK_HOST || '127.0.0.1';
      client.connect(6379, host, () => {
        client.write(cmd + '\r\n');
      });

      client.on('data', (data) => {
        responseData += data.toString();
        // Since TakshakDB usually replies with a single RESP message, we can just close after first chunk
        // Or wait for a bit, but for REST API we want fast response
        client.destroy(); 
      });

      client.on('close', () => {
        resolve(NextResponse.json({ payload: responseData.trim() }));
      });

      client.on('error', (err) => {
        resolve(NextResponse.json({ error: err.message }, { status: 500 }));
      });
    });
  } catch (error: any) {
    return NextResponse.json({ error: error.message }, { status: 500 });
  }
}
