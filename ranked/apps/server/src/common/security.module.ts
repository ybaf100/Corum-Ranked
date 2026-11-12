import { Global, Module } from "@nestjs/common";
import { TokenService } from "./token.service.js";
import { ArgonService } from "../session/argon.service.js";

@Global()
@Module({
  providers: [TokenService, ArgonService],
  exports: [TokenService, ArgonService],
})
export class SecurityModule {}
