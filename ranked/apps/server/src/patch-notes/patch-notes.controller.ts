import { Controller, Get, Header } from "@nestjs/common";
import { patchNotes } from "./patch-notes.js";

// Public: an outdated client reads the new version's notes while it updates.
@Controller("api/ranked/patch-notes")
export class PatchNotesController {
  @Get()
  @Header("Cache-Control", "public, max-age=60")
  public list() { return { notes: patchNotes() }; }
}
