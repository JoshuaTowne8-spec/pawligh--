import { gateway,gatewayFailure } from '@/lib/device';
import { owner } from '@/lib/server';
export async function POST(req:Request,context:{params:Promise<{id:string}>}){try{owner(req);const {id}=await context.params;return Response.json(await gateway(`/v1/control/events/${encodeURIComponent(id)}/ack`,{method:'POST'}));}catch(error){return gatewayFailure(error);}}
