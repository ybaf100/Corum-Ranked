import { Module } from "@nestjs/common";
import { PatchNotesController } from "./patch-notes.controller.js";

@Module({ controllers: [PatchNotesController] })
export class PatchNotesModule {}
