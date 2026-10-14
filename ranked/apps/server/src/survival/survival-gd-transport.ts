import { RankedDomainError } from "@corum-ranked/rules";

/** Only Survival uses this transport; the map provider still validates GD data. */
export interface SurvivalGdRelayEnvironment {
  readonly origin:string;
  readonly token:string;
}

export class SurvivalGdRelayError extends Error {
  public constructor(message:string) {super(message);this.name="SurvivalGdRelayError";}
}

export function loadSurvivalGdRelayEnvironment(environment:NodeJS.ProcessEnv):SurvivalGdRelayEnvironment|undefined {
  const origin=environment.SURVIVAL_GD_RELAY_ORIGIN?.trim();
  const token=environment.SURVIVAL_GD_RELAY_TOKEN?.trim();
  if(!origin && !token)return undefined;
  if(!origin || /^<.+>$/.test(origin))throw new RankedDomainError("INVALID_CONFIG","SURVIVAL_GD_RELAY_ORIGIN must be explicitly configured");
  if(!token || !/^[A-Za-z0-9_-]{32,256}$/.test(token))
    throw new RankedDomainError("INVALID_CONFIG","SURVIVAL_GD_RELAY_TOKEN must contain 32 to 256 random URL-safe characters");
  let url:URL;
  try{url=new URL(origin);}catch{throw new RankedDomainError("INVALID_CONFIG","SURVIVAL_GD_RELAY_ORIGIN must be a valid HTTPS origin");}
  if(url.protocol!=="https:" || url.username || url.password || url.pathname!=="/" || url.search || url.hash)
    throw new RankedDomainError("INVALID_CONFIG","SURVIVAL_GD_RELAY_ORIGIN must be an HTTPS origin without credentials, path or query");
  return Object.freeze({origin:url.origin,token});
}

const GD_ORIGIN="https://www.boomlings.com";
const ENDPOINTS=new Set(["/database/getGJLevels21.php","/database/downloadGJLevel22.php"]);
const UPSTREAM_HEADERS=["server","content-type","cf-ray","cf-mitigated","retry-after"] as const;

export function createSurvivalGdFetch(relay:SurvivalGdRelayEnvironment|undefined,fetcher:typeof fetch=fetch):typeof fetch {
  if(!relay)return fetcher;
  return async(input,init)=>{
    const url=new URL(typeof input==="string"?input:input instanceof URL?input.href:input.url);
    if(url.origin!==GD_ORIGIN || !ENDPOINTS.has(url.pathname) || url.search || url.hash || url.username || url.password ||
      init?.method!=="POST" || typeof init.body!=="string")throw new SurvivalGdRelayError("GD relay request is not an allowed Survival endpoint");
    // Rebuild headers: the relay key must never reach GD or a redirect target.
    let response:Response;
    try {response=await fetcher(`${relay.origin}${url.pathname.replace("/database","")}`,{
      method:"POST",redirect:"manual",headers:{"content-type":"application/x-www-form-urlencoded",accept:"*/*",
        authorization:`Bearer ${relay.token}`},body:init.body,...(init.signal?{signal:init.signal}:{}),
    });}catch{throw new SurvivalGdRelayError("GD relay connection failed or timed out");}
    let legacySearch=false;
    if(url.pathname.endsWith("getGJLevels21.php") && response.headers.get("x-corum-gd-relay-error")==="INVALID_REQUEST") {
      await response.body?.cancel().catch(()=>{});
      const form=new URLSearchParams(init.body);form.set("type","4");form.set("len","0,1,2,3,4");form.set("page",String(Math.min(255,Number(form.get("page")??0))));
      response=await fetcher(`${relay.origin}/getGJLevels21.php`,{method:"POST",redirect:"manual",headers:{"content-type":"application/x-www-form-urlencoded",accept:"*/*",authorization:`Bearer ${relay.token}`},body:form.toString(),...(init.signal?{signal:init.signal}:{})});
      legacySearch=true;
    }
    const marker=response.headers.get("x-corum-gd-relay");
    const relayError=response.headers.get("x-corum-gd-relay-error");
    if(marker!=="v1" || relayError || response.headers.get("x-corum-gd-upstream-status")!==String(response.status)) {
      await response.body?.cancel().catch(()=>{});
      const knownErrors=new Set(["UNAUTHORIZED","NOT_CONFIGURED","INVALID_REQUEST","REQUEST_TOO_LARGE","UPSTREAM_TIMEOUT",
        "UPSTREAM_UNAVAILABLE","RESPONSE_TOO_LARGE","UPSTREAM_REDIRECT"]);
      const reason=relayError && knownErrors.has(relayError)?relayError:"INVALID_RELAY_RESPONSE";
      throw new SurvivalGdRelayError(`GD relay ${reason} (HTTP ${response.status})`);
    }
    // Cloudflare adds its own Server/Ray headers to Worker responses. Use the
    // explicit upstream copies so a relay failure is not mistaken for a GD ban.
    const headers=new Headers();
    for(const name of UPSTREAM_HEADERS) {
      const value=response.headers.get(`x-corum-gd-upstream-${name}`);
      if(value!==null)headers.set(name,value);
    }
    if(legacySearch)headers.set("x-corum-gd-legacy-search","1");
    return new Response(response.body,{status:response.status,headers});
  };
}
