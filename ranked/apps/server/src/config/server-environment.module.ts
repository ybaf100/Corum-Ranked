import { Global, Module } from "@nestjs/common";
import {
  SERVER_ENVIRONMENT,
  loadServerEnvironment,
} from "./server-environment.js";

@Global()
@Module({
  providers: [
    {
      provide: SERVER_ENVIRONMENT,
      useFactory: loadServerEnvironment,
    },
  ],
  exports: [SERVER_ENVIRONMENT],
})
export class ServerEnvironmentModule {}
