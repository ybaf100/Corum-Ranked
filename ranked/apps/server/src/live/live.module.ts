import { Controller, Get, Header, Module } from "@nestjs/common";
import { LiveService } from "./live.service.js";

/** Public read-only GET /api/ranked/live for the website's LIVE board. */
@Controller("api/ranked/live")
export class LiveController {
  public constructor(private readonly live: LiveService) {}

  @Get()
  @Header("Cache-Control", "public, max-age=5, stale-while-revalidate=10")
  public snapshot() { return this.live.snapshot(); }
}

@Module({
  controllers: [LiveController],
  providers: [LiveService],
})
export class LiveModule {}
