/** FIFO fence: a settings refresh and a new match admission cannot overlap. */
export class ModeAdmissionFence {
  private tail: Promise<void> = Promise.resolve();
  public async run<T>(operation: () => Promise<T>): Promise<T> {
    const previous = this.tail;
    let release!: () => void;
    this.tail = new Promise<void>(resolve => { release = resolve; });
    await previous;
    try { return await operation(); } finally { release(); }
  }
}
