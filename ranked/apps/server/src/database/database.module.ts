import { Global, Module } from "@nestjs/common";
import { DatabaseService } from "./database.service.js";
import { DATABASE } from "./database.port.js";

@Global()
@Module({
  providers: [
    DatabaseService,
    {
      provide: DATABASE,
      useExisting: DatabaseService,
    },
  ],
  exports: [DatabaseService, DATABASE],
})
export class DatabaseModule {}
