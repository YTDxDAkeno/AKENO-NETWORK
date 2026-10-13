export function toIso(value: Date): string;
export function toIso(value: Date | null | undefined): string | null;
export function toIso(value: Date | null | undefined): string | null {
  return value ? value.toISOString() : null;
}

export function addMs(date: Date, ms: number): Date {
  return new Date(date.getTime() + ms);
}

export function hoursFromNow(hours: number): Date {
  return new Date(Date.now() + hours * 60 * 60 * 1000);
}
