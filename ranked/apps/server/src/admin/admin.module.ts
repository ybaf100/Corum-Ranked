import { Body, Controller, Inject, Module, Post, UseGuards } from "@nestjs/common";
import { SessionGuard } from "../session/session.guard.js";
import { CurrentSession } from "../session/current-session.decorator.js";
import type { RankedSessionContext } from "../session/session.types.js";
import { RankedConfigModule } from "../config/ranked-config.module.js";
import { AdminActionDto } from "./admin.dto.js";
import { AdminService } from "./admin.service.js";
import { SessionModule } from "../session/session.module.js";
@Controller("api/ranked/debug/admin") @UseGuards(SessionGuard)
export class AdminController {
  public constructor(@Inject(AdminService) private readonly admin:AdminService) {}
  @Post() public execute(@CurrentSession() session:RankedSessionContext,@Body() body:AdminActionDto) {
    return this.admin.execute(session,body);
  }
}
@Module({imports:[RankedConfigModule,SessionModule],controllers:[AdminController],providers:[AdminService]})
export class AdminModule {}
