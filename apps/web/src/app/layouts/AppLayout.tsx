import {
  Compass,
  Gamepad2,
  House,
  LogOut,
  Menu as MenuIcon,
  MessagesSquare,
  PartyPopper,
  Settings,
  ShieldCheck,
  UserRound,
  Users,
  UsersRound,
  X,
  type LucideIcon,
} from 'lucide-react';
import { useEffect, useState, type ReactNode } from 'react';
import { NavLink, Outlet, useLocation } from 'react-router';
import { CountBadge } from '../../components/Badge';
import { cn } from '../../components/cn';
import { Logo } from '../../components/Logo';
import { useToast } from '../../components/Toast';
import { useAuth, useCurrentUser } from '../../features/auth/AuthProvider';
import { useGamepadNavigation } from '../../hooks/useGamepadNavigation';
import { getControllerNavigation } from '../../lib/preferences';
import { useApplyUserTheme } from '../../lib/theme';
import { VoiceBar, VoicePartyWatcher } from '../../features/voice/VoiceBar';
import { useVoice } from '../../features/voice/VoiceProvider';
import { useConversations, useCurrentParty, useFriendRequests, useGroups } from '../../lib/queries';
import { ConnectionBanner, ConnectionPill } from './ConnectionStatus';
import { NotificationsMenu } from './NotificationsMenu';
import { PresenceMenu } from './PresenceMenu';

interface NavItem {
  to: string;
  label: string;
  icon: LucideIcon;
  badge?: number;
  dot?: boolean;
  end?: boolean;
}

function useNavItems(): NavItem[] {
  const requests = useFriendRequests();
  const conversations = useConversations();
  const groups = useGroups();
  const party = useCurrentParty();
  const unreadMessages = (conversations.data ?? []).reduce((sum, conversation) => sum + conversation.unreadCount, 0);
  const unreadGroups = (groups.data ?? []).reduce((sum, group) => sum + group.unreadCount, 0);
  return [
    { to: '/', label: 'Home', icon: House, end: true },
    { to: '/friends', label: 'Friends', icon: Users, badge: requests.data?.incoming.length ?? 0 },
    { to: '/messages', label: 'Messages', icon: MessagesSquare, badge: unreadMessages },
    { to: '/groups', label: 'Groups', icon: UsersRound, badge: unreadGroups },
    { to: '/parties', label: 'Parties', icon: PartyPopper, dot: Boolean(party.data) },
    { to: '/sessions', label: 'Game Sessions', icon: Gamepad2 },
    { to: '/discover', label: 'Discover', icon: Compass },
  ];
}

function NavEntry({ item, onNavigate }: { item: NavItem; onNavigate?: () => void }) {
  const Icon = item.icon;
  return (
    <NavLink
      to={item.to}
      end={item.end}
      onClick={onNavigate}
      className={({ isActive }) =>
        cn(
          'group flex items-center gap-3 rounded-xl px-3 py-2.5 text-sm font-medium transition-colors',
          isActive ? 'bg-accent-soft text-ink' : 'text-muted hover:bg-surface-2 hover:text-ink',
        )
      }
    >
      {({ isActive }) => (
        <>
          <Icon aria-hidden className={cn('size-5', isActive ? 'text-accent' : 'text-subtle group-hover:text-ink')} />
          <span className="flex-1">{item.label}</span>
          {item.badge ? <CountBadge count={item.badge} label={`${item.badge} new`} /> : null}
          {item.dot ? <span className="size-2 rounded-full bg-status-online" aria-label="Active" /> : null}
        </>
      )}
    </NavLink>
  );
}

function SecondaryLinks({ onNavigate }: { onNavigate?: () => void }) {
  const user = useCurrentUser();
  const items: NavItem[] = [
    { to: `/u/${user.onlineId}`, label: 'Profile', icon: UserRound },
    { to: '/settings', label: 'Settings', icon: Settings },
  ];
  if (user.role === 'admin') items.push({ to: '/admin', label: 'Administration', icon: ShieldCheck });
  return (
    <>
      {items.map((item) => (
        <NavEntry key={item.to} item={item} {...(onNavigate ? { onNavigate } : {})} />
      ))}
    </>
  );
}

function SignOutButton({ className }: { className?: string }) {
  const { logout } = useAuth();
  const toast = useToast();
  return (
    <button
      type="button"
      onClick={() => {
        void logout().then(() => toast.success('You signed out.'));
      }}
      className={cn(
        'flex w-full items-center gap-3 rounded-xl px-3 py-2.5 text-sm font-medium text-muted transition-colors hover:bg-surface-2 hover:text-ink',
        className,
      )}
    >
      <LogOut aria-hidden className="size-5 text-subtle" />
      Sign out
    </button>
  );
}

function MobileDrawer({ open, onClose, items }: { open: boolean; onClose: () => void; items: NavItem[] }) {
  useEffect(() => {
    if (!open) return;
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onClose();
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [open, onClose]);
  if (!open) return null;
  return (
    <div className="fixed inset-0 z-50 lg:hidden" role="dialog" aria-modal="true" aria-label="Navigation">
      <button type="button" aria-label="Close navigation" className="absolute inset-0 bg-black/60" onClick={onClose} />
      <nav className="absolute inset-y-0 left-0 flex w-72 max-w-[85vw] animate-fade-in flex-col gap-1 overflow-y-auto border-r border-line bg-surface p-4">
        <div className="mb-4 flex items-center justify-between">
          <Logo />
          <button type="button" onClick={onClose} className="rounded-lg p-2 text-muted hover:bg-surface-2" aria-label="Close navigation">
            <X aria-hidden className="size-5" />
          </button>
        </div>
        {items.map((item) => (
          <NavEntry key={item.to} item={item} onNavigate={onClose} />
        ))}
        <div className="my-3 border-t border-line" />
        <SecondaryLinks onNavigate={onClose} />
        <SignOutButton />
      </nav>
    </div>
  );
}

const MOBILE_TABS = ['/', '/friends', '/messages', '/parties', '/sessions'];

export function AppLayout({ children }: { children?: ReactNode }) {
  const items = useNavItems();
  const me = useCurrentUser();
  useApplyUserTheme(me.profile.appearance);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const location = useLocation();
  const [controller, setController] = useState(getControllerNavigation);
  useGamepadNavigation(controller);

  useEffect(() => {
    const onChange = () => setController(getControllerNavigation());
    window.addEventListener('akeno:preferences', onChange);
    return () => window.removeEventListener('akeno:preferences', onChange);
  }, []);

  const isChatRoute = /^\/(messages\/[^/]+|groups\/[^/]+|parties)/.test(location.pathname);
  const voice = useVoice();
  const voiceBarShown = Boolean(voice && voice.partyId && voice.status !== 'idle' && voice.status !== 'error');

  return (
    <div className="min-h-dvh lg:pl-64">
      <a href="#main" className="sr-only focus:not-sr-only focus:fixed focus:left-4 focus:top-4 focus:z-50 focus:rounded-lg focus:bg-surface focus:px-4 focus:py-2">
        Skip to content
      </a>

      <aside className="fixed inset-y-0 left-0 z-30 hidden w-64 flex-col border-r border-line bg-surface/80 px-4 py-5 backdrop-blur-xl lg:flex">
        <NavLink to="/" className="mb-7 px-2" aria-label="AKENO NETWORK home">
          <Logo />
        </NavLink>
        <nav aria-label="Main" className="flex flex-col gap-1">
          {items.map((item) => (
            <NavEntry key={item.to} item={item} />
          ))}
        </nav>
        <div className="mt-auto flex flex-col gap-1 border-t border-line pt-4">
          <VoiceBar className="mb-2" />
          <SecondaryLinks />
          <SignOutButton />
        </div>
      </aside>

      <header className="sticky top-0 z-20 flex h-16 items-center gap-2 border-b border-line bg-canvas/80 px-3 backdrop-blur-xl sm:px-6">
        <button
          type="button"
          className="rounded-xl p-2 text-muted hover:bg-surface-2 lg:hidden"
          onClick={() => setDrawerOpen(true)}
          aria-label="Open navigation"
        >
          <MenuIcon aria-hidden className="size-5" />
        </button>
        <NavLink to="/" className="lg:hidden" aria-label="AKENO NETWORK home">
          <Logo compact />
        </NavLink>
        <div className="ml-auto flex items-center gap-1.5 sm:gap-3">
          <ConnectionPill />
          <NotificationsMenu />
          <PresenceMenu />
        </div>
      </header>

      <main id="main" className={cn('mx-auto w-full max-w-7xl px-3 pt-5 sm:px-6 lg:pb-10', voiceBarShown ? 'pb-44' : 'pb-28', isChatRoute && 'lg:pb-6')}>
        <ConnectionBanner />
        {children ?? <Outlet />}
      </main>

      <VoicePartyWatcher />
      <div className="fixed inset-x-3 bottom-[calc(4.25rem+env(safe-area-inset-bottom))] z-30 lg:hidden">
        <VoiceBar className="shadow-card" />
      </div>

      <nav
        aria-label="Quick navigation"
        className="fixed inset-x-0 bottom-0 z-30 grid grid-cols-5 border-t border-line bg-surface/95 pb-[env(safe-area-inset-bottom)] backdrop-blur-xl lg:hidden"
      >
        {items
          .filter((item) => MOBILE_TABS.includes(item.to))
          .map((item) => {
            const Icon = item.icon;
            return (
              <NavLink
                key={item.to}
                to={item.to}
                end={item.end}
                className={({ isActive }) =>
                  cn('relative flex flex-col items-center gap-1 py-2.5 text-[11px] font-medium', isActive ? 'text-accent' : 'text-muted')
                }
              >
                <Icon aria-hidden className="size-5" />
                {item.label === 'Game Sessions' ? 'Sessions' : item.label}
                {item.badge ? (
                  <span className="absolute right-[calc(50%-1.4rem)] top-1.5 size-2 rounded-full bg-danger" aria-label={`${item.badge} new`} />
                ) : null}
              </NavLink>
            );
          })}
      </nav>

      <MobileDrawer open={drawerOpen} onClose={() => setDrawerOpen(false)} items={items} />
    </div>
  );
}
