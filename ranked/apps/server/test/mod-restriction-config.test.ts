import { describe, expect, it } from "vitest";
import { RankedConfigController } from "../src/config/ranked-config.controller.js";
import { RankedConfigService } from "../src/config/ranked-config.service.js";
import { configDocumentFixture, environmentFixture } from "./fixtures.js";
import { evaluateRankedClientVersion, REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";

describe("mod restriction config publication", () => {
  it("defaults missing to TRUE and publishes both transitions without losing policy data", async () => {
    const document=configDocumentFixture();
    const service=new RankedConfigService({fetchConfig:async()=>document},environmentFixture());
    const controller=new RankedConfigController(service);
    for(const enabled of [undefined,false,true,false]) {
      Object.assign(document.operational,{modRestrictionEnabled:enabled});
      expect(await service.refresh()).toBe(true);
      expect(service.getSnapshot().operational.modRestrictionEnabled).toBe(enabled??true);
      const response=await controller.getClientConfig();
      expect(response).toMatchObject({modRestrictionEnabled:enabled??true,
        allowedMods:document.allowedMods.filter(m=>m.enabled),cbf:document.operational.cbf,
        requiredClientVersion:REQUIRED_RANKED_CLIENT_VERSION});
    }
  });
  it.each(["FALSE",null,0])("rejects invalid %s and retains the validated snapshot", async value => {
    const document=configDocumentFixture();
    const service=new RankedConfigService({fetchConfig:async()=>document},environmentFixture());
    expect(await service.refresh()).toBe(true);
    Object.assign(document.operational,{modRestrictionEnabled:value});
    expect(await service.refresh()).toBe(false);
    expect(service.getSnapshot().operational.modRestrictionEnabled).toBe(true);
  });
  it("keeps the independent client version gate when generic mod restrictions are off", () => {
    expect(evaluateRankedClientVersion("old",[]).allowed).toBe(false);
    expect(evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION,[]).allowed).toBe(false);
    expect(evaluateRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION,[{
      id:"hwanhee1.corum_ranked",version:REQUIRED_RANKED_CLIENT_VERSION,enabled:true,loaded:true,
    }]).allowed).toBe(true);
  });
});
