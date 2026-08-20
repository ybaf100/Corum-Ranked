import { createParamDecorator, type ExecutionContext } from "@nestjs/common";
import type { RankedRequest, RankedSessionContext } from "./session.types.js";

export const CurrentSession = createParamDecorator(
  (_data: unknown, context: ExecutionContext): RankedSessionContext => {
    const request = context.switchToHttp().getRequest<RankedRequest>();
    if (!request.rankedSession) throw new Error("SessionGuard did not attach a session");
    return request.rankedSession;
  },
);
