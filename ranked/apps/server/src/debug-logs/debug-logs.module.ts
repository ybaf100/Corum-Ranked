import { Module } from "@nestjs/common";
import { SessionModule } from "../session/session.module.js";
import { DebugLogsController } from "./debug-logs.controller.js";

@Module({ imports: [SessionModule], controllers: [DebugLogsController] })
export class DebugLogsModule {}
