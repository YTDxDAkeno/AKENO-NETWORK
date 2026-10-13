import { useSyncExternalStore } from 'react';

const TYPING_TTL_MS = 5000;

/** Ephemeral "is typing" indicators, keyed by conversation. Never persisted. */
class TypingStore {
  private readonly entries = new Map<string, Map<string, number>>();
  private readonly listeners = new Set<() => void>();
  private snapshots = new Map<string, string[]>();

  add(conversationId: string, userId: string): void {
    let users = this.entries.get(conversationId);
    if (!users) {
      users = new Map();
      this.entries.set(conversationId, users);
    }
    users.set(userId, Date.now() + TYPING_TTL_MS);
    this.emit(conversationId);
    window.setTimeout(() => this.prune(conversationId), TYPING_TTL_MS + 50);
  }

  remove(conversationId: string, userId: string): void {
    if (this.entries.get(conversationId)?.delete(userId)) this.emit(conversationId);
  }

  get(conversationId: string): string[] {
    return this.snapshots.get(conversationId) ?? EMPTY;
  }

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  private prune(conversationId: string): void {
    const users = this.entries.get(conversationId);
    if (!users) return;
    const now = Date.now();
    let changed = false;
    for (const [userId, expiresAt] of users) {
      if (expiresAt <= now) {
        users.delete(userId);
        changed = true;
      }
    }
    if (changed) this.emit(conversationId);
  }

  private emit(conversationId: string): void {
    this.snapshots = new Map(this.snapshots);
    this.snapshots.set(conversationId, [...(this.entries.get(conversationId)?.keys() ?? [])]);
    for (const listener of this.listeners) listener();
  }
}

const EMPTY: string[] = [];
export const typingStore = new TypingStore();

export function useTypingUsers(conversationId: string | null): string[] {
  return useSyncExternalStore(typingStore.subscribe, () => (conversationId ? typingStore.get(conversationId) : EMPTY));
}
