import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Bell, CheckCheck } from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { useNavigate } from 'react-router';
import { Avatar } from '../../components/Avatar';
import { cn } from '../../components/cn';
import { RelativeTime } from '../../components/RelativeTime';
import { EmptyState } from '../../components/States';
import { api } from '../../lib/api';
import { describeNotification } from '../../lib/notifications';
import { useNotifications } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';

export function NotificationsMenu() {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const { data } = useNotifications();
  const unread = data?.unreadCount ?? 0;

  const markRead = useMutation({
    mutationFn: (body: { all: true } | { ids: number[] }) => api.post('/notifications/read', body),
    onSuccess: () => queryClient.invalidateQueries({ queryKey: queryKeys.notifications }),
  });

  useEffect(() => {
    if (!open) return;
    const onPointer = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) setOpen(false);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setOpen(false);
    };
    document.addEventListener('pointerdown', onPointer);
    document.addEventListener('keydown', onKey);
    return () => {
      document.removeEventListener('pointerdown', onPointer);
      document.removeEventListener('keydown', onKey);
    };
  }, [open]);

  return (
    <div ref={rootRef} className="relative">
      <button
        type="button"
        onClick={() => setOpen((value) => !value)}
        aria-expanded={open}
        aria-haspopup="dialog"
        aria-label={unread > 0 ? `Notifications, ${unread} unread` : 'Notifications'}
        className="relative inline-flex size-10 items-center justify-center rounded-xl text-muted hover:bg-surface-2 hover:text-ink"
      >
        <Bell aria-hidden className="size-5" />
        {unread > 0 ? (
          <span className="absolute right-1.5 top-1.5 inline-flex min-w-4 items-center justify-center rounded-full bg-danger px-1 text-[10px] font-bold leading-4 text-white">
            {unread > 9 ? '9+' : unread}
          </span>
        ) : null}
      </button>
      {open ? (
        <div
          role="dialog"
          aria-label="Notifications"
          className="card fixed inset-x-3 top-16 z-40 animate-fade-in overflow-hidden sm:absolute sm:inset-x-auto sm:right-0 sm:top-full sm:mt-2 sm:w-96"
        >
          <div className="flex items-center justify-between border-b border-line px-4 py-3">
            <h2 className="text-sm font-semibold">Notifications</h2>
            <button
              type="button"
              disabled={unread === 0 || markRead.isPending}
              onClick={() => markRead.mutate({ all: true })}
              className="inline-flex items-center gap-1 text-xs font-medium text-accent disabled:text-subtle"
            >
              <CheckCheck aria-hidden className="size-3.5" />
              Mark all as read
            </button>
          </div>
          <ul className="scrollbar-thin max-h-[60vh] overflow-y-auto">
            {data && data.notifications.length === 0 ? (
              <li>
                <EmptyState icon={<Bell className="size-5" />} title="You're all caught up" description="Friend requests and invitations show up here." />
              </li>
            ) : null}
            {data?.notifications.map((notification) => {
              const { text, href } = describeNotification(notification.data);
              return (
                <li key={notification.id}>
                  <button
                    type="button"
                    onClick={() => {
                      if (!notification.readAt) markRead.mutate({ ids: [notification.id] });
                      setOpen(false);
                      void navigate(href);
                    }}
                    className={cn(
                      'flex w-full items-start gap-3 border-b border-line px-4 py-3 text-left transition-colors hover:bg-surface-2',
                      !notification.readAt && 'bg-accent-soft/50',
                    )}
                  >
                    <Avatar avatar={notification.data.actor.avatar} name={notification.data.actor.displayName} size="sm" />
                    <span className="min-w-0 flex-1">
                      <span className="block text-sm text-ink">{text}</span>
                      <RelativeTime iso={notification.createdAt} className="text-xs text-subtle" />
                    </span>
                    {!notification.readAt ? <span className="mt-2 size-2 shrink-0 rounded-full bg-accent" aria-label="Unread" /> : null}
                  </button>
                </li>
              );
            })}
          </ul>
        </div>
      ) : null}
    </div>
  );
}
