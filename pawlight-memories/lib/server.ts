import { env } from 'cloudflare:workers';
type AppBindings={DB:D1Database;FILES:R2Bucket;LOCAL_SINGLE_USER_ID?:string;GATEWAY_BASE_URL?:string;CONTROL_TOKEN?:string};
export const bindings=()=>env as unknown as AppBindings;
export function setting(name:'LOCAL_SINGLE_USER_ID'|'GATEWAY_BASE_URL'|'CONTROL_TOKEN'){
  const value=bindings()[name]??process.env[name];
  return typeof value==='string'?value.trim():'';
}
let schemaReady:Promise<void>|undefined;
export function ensureSchema(){
  if(!schemaReady){
    const db=bindings().DB;
    schemaReady=(async()=>{
      await db.prepare('CREATE TABLE IF NOT EXISTS archives (owner TEXT PRIMARY KEY NOT NULL, data TEXT NOT NULL, version INTEGER NOT NULL DEFAULT 0)').run();
      await db.prepare('CREATE TABLE IF NOT EXISTS media (id TEXT PRIMARY KEY NOT NULL, owner TEXT NOT NULL, type TEXT NOT NULL, name TEXT NOT NULL)').run();
    })().catch(error=>{schemaReady=undefined;throw error;});
  }
  return schemaReady;
}
export function owner(req:Request){const id=req.headers.get('oai-authenticated-user-id')||setting('LOCAL_SINGLE_USER_ID');if(!id)throw new Response(JSON.stringify({error:'请先登录后保存你的记忆'}),{status:401,headers:{'Content-Type':'application/json'}});if(req.method!=='GET'&&req.headers.get('sec-fetch-site')==='cross-site')throw new Response('Forbidden',{status:403});return id;}
export function failure(error:unknown){if(error instanceof Response)return error;console.error(error);return Response.json({error:error instanceof Error?error.message:'暂时无法保存，请重试'},{status:400});}
