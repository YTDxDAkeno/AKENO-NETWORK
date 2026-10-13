/**
 * Collects side effects (realtime events, notifications pushes) that must only run after a
 * database transaction has committed successfully.
 */
export class Effects {
  private readonly queue: Array<() => void | Promise<void>> = [];

  add(effect: () => void | Promise<void>): void {
    this.queue.push(effect);
  }

  merge(other: Effects): void {
    this.queue.push(...other.queue);
  }

  /** Runs all collected effects. Failures are reported but never thrown to the caller. */
  async run(onError: (error: unknown) => void = () => {}): Promise<void> {
    const pending = this.queue.splice(0, this.queue.length);
    for (const effect of pending) {
      try {
        await effect();
      } catch (error) {
        onError(error);
      }
    }
  }
}
