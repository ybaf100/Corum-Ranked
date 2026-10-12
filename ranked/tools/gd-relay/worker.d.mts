export interface GdRelayEnvironment { GD_RELAY_TOKEN?:string; }
export function createGdRelayWorker(upstreamFetch?:typeof fetch):{
  fetch(request:Request,env:GdRelayEnvironment):Promise<Response>;
};
declare const worker:ReturnType<typeof createGdRelayWorker>;
export default worker;
