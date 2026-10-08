import { HttpException } from "@nestjs/common";
import { describe, expect, it, vi } from "vitest";
import { REQUIRED_RANKED_CLIENT_VERSION } from "../src/session/client-version.js";
import { SessionController } from "../src/session/session.controller.js";
import type { CreateSessionDto } from "../src/session/session.dto.js";
import type { SessionService } from "../src/session/session.service.js";

const dto = (version: string): CreateSessionDto => ({
  gdAccountId: "7001",
  gdUsername: "VersionTest",
  clientVersion: version,
  installedMods: [
    {
      id: "hwanhee1.corum_ranked",
      version,
      enabled: true,
      loaded: true,
      internal: false,
      system: false,
    },
  ],
});

const controllerWithCreate = () => {
  const create = vi.fn(async () => ({ ok: true }));
  const service = { create } as unknown as SessionService;
  return { controller: new SessionController(service), create };
};

describe("Ranked HTTP session version admission", () => {
  it("admits the exact current client", async () => {
    const { controller, create } = controllerWithCreate();
    await expect(controller.create(dto(REQUIRED_RANKED_CLIENT_VERSION))).resolves.toEqual({ ok: true });
    expect(create).toHaveBeenCalledTimes(1);
  });

  it("returns HTTP 426 before creating a session for an older client", async () => {
    const { controller, create } = controllerWithCreate();
    try {
      await controller.create(dto("v0.4.0-alpha.36"));
      throw new Error("Expected stale client rejection");
    } catch (error) {
      expect(error).toBeInstanceOf(HttpException);
      expect((error as HttpException).getStatus()).toBe(426);
      expect((error as HttpException).getResponse()).toMatchObject({
        code: "RANKED_CLIENT_UPDATE_REQUIRED",
        requiredVersion: REQUIRED_RANKED_CLIENT_VERSION,
      });
    }
    expect(create).not.toHaveBeenCalled();
  });
});
