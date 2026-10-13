import { Gamepad2, MessagesSquare, PartyPopper, Users } from 'lucide-react';
import type { ReactNode } from 'react';
import { Logo } from '../../components/Logo';

const HIGHLIGHTS = [
  { icon: Users, text: 'Private friends list with real-time presence' },
  { icon: MessagesSquare, text: 'Direct and group chat that is saved securely' },
  { icon: PartyPopper, text: 'Parties for hanging out between matches' },
  { icon: Gamepad2, text: 'A lobby directory to organize game sessions' },
];

export function AuthLayout({ children }: { children: ReactNode }) {
  return (
    <div className="flex min-h-dvh flex-col">
      <div className="mx-auto grid w-full max-w-6xl flex-1 items-center gap-10 px-4 py-10 lg:grid-cols-[1.1fr_1fr] lg:px-8">
        <section className="hidden lg:block" aria-label="About AKENO NETWORK">
          <Logo className="mb-10" />
          <h1 className="text-5xl font-bold leading-tight text-ink">
            Your crew.
            <br />
            <span className="text-gradient">Your network.</span>
          </h1>
          <p className="mt-5 max-w-md text-lg text-muted">
            AKENO NETWORK is a private, self-hosted home for your gaming group: friends, chat, parties and game sessions on
            hardware you control.
          </p>
          <ul className="mt-8 space-y-3">
            {HIGHLIGHTS.map(({ icon: Icon, text }) => (
              <li key={text} className="flex items-center gap-3 text-sm text-ink">
                <span className="rounded-lg bg-accent-soft p-2 text-accent">
                  <Icon aria-hidden className="size-4" />
                </span>
                {text}
              </li>
            ))}
          </ul>
        </section>
        <div className="mx-auto w-full max-w-md">
          <Logo className="mb-8 lg:hidden" />
          {children}
        </div>
      </div>
      <footer className="px-4 pb-6 text-center text-xs text-subtle">
        AKENO NETWORK is an independent community platform. It is not affiliated with, endorsed by, or connected to Sony
        Interactive Entertainment, PlayStation Network or any console manufacturer.
      </footer>
    </div>
  );
}
