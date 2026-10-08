import { randomInt, randomUUID } from "node:crypto";
import { Global, Module } from "@nestjs/common";
import type { RandomSource } from "@corum-ranked/rules";

export const SERVER_CLOCK = Symbol("SERVER_CLOCK");
export const ID_GENERATOR = Symbol("ID_GENERATOR");
export const RANDOM_SOURCE = Symbol("RANDOM_SOURCE");

export interface ServerClock {
  now(): Date;
}

export interface IdGenerator {
  next(): string;
}

class SystemClock implements ServerClock {
  public now(): Date {
    return new Date();
  }
}

class UuidGenerator implements IdGenerator {
  public next(): string {
    return randomUUID();
  }
}

class CryptoRandomSource implements RandomSource {
  public next(): number {
    return randomInt(0, 0x1_0000_0000) / 0x1_0000_0000;
  }
}

@Global()
@Module({
  providers: [
    { provide: SERVER_CLOCK, useClass: SystemClock },
    { provide: ID_GENERATOR, useClass: UuidGenerator },
    { provide: RANDOM_SOURCE, useClass: CryptoRandomSource },
  ],
  exports: [SERVER_CLOCK, ID_GENERATOR, RANDOM_SOURCE],
})
export class RuntimeModule {}
