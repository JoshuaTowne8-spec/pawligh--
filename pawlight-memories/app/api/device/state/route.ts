import { gateway,gatewayFailure } from '@/lib/device';
import { owner } from '@/lib/server';
export async function GET(req:Request){try{owner(req);return Response.json(await gateway('/v1/control/state'));}catch(error){return gatewayFailure(error);}}
