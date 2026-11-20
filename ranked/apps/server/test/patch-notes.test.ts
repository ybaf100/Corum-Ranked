import { describe, expect, it } from "vitest";
import { PATCH_NOTES, patchNotes } from "../src/patch-notes/patch-notes.js";
import { PatchNotesController } from "../src/patch-notes/patch-notes.controller.js";
import { REQUIRED_RANKED_CLIENT_VERSION, normalizeRankedClientVersion } from "../src/session/client-version.js";

const order = (version: string) => Number(/beta\.(\d+)$/.exec(version)?.[1] ?? -1);

describe("patch notes", () => {
  it("lists every release newest first, the same lines in English and Korean", () => {
    expect(PATCH_NOTES.length).toBeGreaterThan(0);
    expect(new Set(PATCH_NOTES.map(n => n.version)).size).toBe(PATCH_NOTES.length);
    for (let i = 1; i < PATCH_NOTES.length; i++) expect(order(PATCH_NOTES[i - 1]!.version)).toBeGreaterThan(order(PATCH_NOTES[i]!.version));
    for (const note of PATCH_NOTES) {
      expect(note.version).toMatch(/^\d+\.\d+\.\d+-beta\.\d+$/);
      expect(note.date).toMatch(/^\d{4}-\d{2}-\d{2}$/);
      expect(note.en.length).toBeGreaterThan(0);
      expect(note.ko.length).toBe(note.en.length);
      for (const line of [...note.en, ...note.ko]) expect(line.trim().length).toBeGreaterThan(0);
      for (const line of note.ko) expect(line).toMatch(/[가-힣]/);
    }
  });
  it("has an entry for the release this server requires (each release adds its notes)", () => {
    expect(PATCH_NOTES[0]!.version).toBe(normalizeRankedClientVersion(REQUIRED_RANKED_CLIENT_VERSION));
  });
  it("serves the newest entries publicly", () => {
    expect(new PatchNotesController().list()).toEqual({ notes: patchNotes() });
    expect(patchNotes(2)).toEqual(PATCH_NOTES.slice(0, 2));
  });
});
