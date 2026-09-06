import { gateway,gatewayFailure } from '@/lib/device';
import { owner } from '@/lib/server';
export async function POST(req:Request){try{owner(req);return Response.json(await gateway('/v1/control/mode',{method:'POST',headers:{'Content-Type':'application/json'},body:await req.text()}));}catch(error){return gatewayFailure(error);}}
