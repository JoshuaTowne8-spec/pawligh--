import { setting } from '@/lib/server';

export async function gateway(path:string,init:RequestInit={}){
  const base=setting('GATEWAY_BASE_URL')||'http://127.0.0.1:8080';
  const headers=new Headers(init.headers);
  const token=setting('CONTROL_TOKEN');
  if(token)headers.set('Authorization',`Bearer ${token}`);
  const response=await fetch(`${base}${path}`,{...init,headers,cache:'no-store'});
  const text=await response.text();
  let body:unknown={};
  try{body=text?JSON.parse(text):{};}catch{body={error:text||'网关返回了无效内容'};}
  if(!response.ok){
    const detail=body&&typeof body==='object'&&'detail' in body?String((body as {detail:unknown}).detail):'硬件网关暂时不可用';
    throw Error(detail);
  }
  return body;
}

export function gatewayFailure(error:unknown){
  return Response.json({error:error instanceof Error?error.message:'硬件网关暂时不可用'},{status:503});
}
