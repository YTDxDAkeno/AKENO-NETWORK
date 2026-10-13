/** Returns the two IDs ordered so that low < high, matching the friendships/conversations constraints. */
export function orderedPair(a: string, b: string): [low: string, high: string] {
  return a < b ? [a, b] : [b, a];
}
