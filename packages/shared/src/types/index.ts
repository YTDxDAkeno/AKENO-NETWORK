/** Generic helper types shared across packages. */

/** Makes selected keys of T required. */
export type WithRequired<T, K extends keyof T> = T & { [P in K]-?: T[P] };

/** ISO-8601 timestamp string in UTC. */
export type IsoTimestamp = string;
