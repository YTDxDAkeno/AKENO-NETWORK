import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Check, KeyRound, Laptop, LogOut, Plus, Save, Settings, ShieldAlert, Smartphone, Trash2, X } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { useSearchParams } from 'react-router';
import {
  AVATAR_PRESETS,
  GAME_ACTIVITY,
  LIMITS,
  checkPasswordQuality,
  type AuthSessionDto,
  type CurrentUser,
  type PrivacySettings,
} from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { cn } from '../../components/cn';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField, Switch, TextArea, TextField } from '../../components/Field';
import { Modal } from '../../components/Modal';
import { PageHeader } from '../../components/PageHeader';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { ErrorState, InlineError, Notice } from '../../components/States';
import { Tabs } from '../../components/Tabs';
import { useToast } from '../../components/Toast';
import { ApiError, api, errorMessage } from '../../lib/api';
import { getControllerNavigation, getTheme, setControllerNavigation, setTheme, type Theme } from '../../lib/preferences';
import { useAuthSessions, useGameActivity } from '../../lib/queries';
import { queryKeys } from '../../lib/query-keys';
import { useAuth, useCurrentUser } from '../auth/AuthProvider';
import { PairedDevicesCard } from '../devices/PairedDevicesCard';

type Tab = 'profile' | 'privacy' | 'security' | 'preferences';

function ProfileSettings() {
  const user = useCurrentUser();
  const { setUser } = useAuth();
  const toast = useToast();
  const queryClient = useQueryClient();
  const [form, setForm] = useState({
    displayName: user.profile.displayName,
    avatar: user.profile.avatar,
    statusMessage: user.profile.statusMessage,
    bio: user.profile.bio,
    activity: user.profile.activity ?? '',
    favoriteGames: user.profile.favoriteGames,
  });
  const [gameDraft, setGameDraft] = useState('');
  const [errors, setErrors] = useState<Record<string, string>>({});

  const save = useMutation({
    mutationFn: () =>
      api.patch<CurrentUser>('/profile', {
        displayName: form.displayName,
        avatar: form.avatar,
        statusMessage: form.statusMessage,
        bio: form.bio,
        activity: form.activity.trim() ? form.activity : null,
        favoriteGames: form.favoriteGames,
      }),
    onSuccess: (updated) => {
      setUser(updated);
      setErrors({});
      void queryClient.invalidateQueries({ queryKey: queryKeys.profiles });
      toast.success('Profile saved.');
    },
    onError: (error) => {
      if (error instanceof ApiError) setErrors(error.fields);
      toast.error('Could not save your profile', errorMessage(error));
    },
  });

  const addGame = () => {
    const name = gameDraft.trim();
    if (!name || form.favoriteGames.length >= LIMITS.favoriteGamesMax) return;
    if (form.favoriteGames.some((game) => game.toLowerCase() === name.toLowerCase())) return setGameDraft('');
    setForm((current) => ({ ...current, favoriteGames: [...current.favoriteGames, name.slice(0, LIMITS.favoriteGameNameMax)] }));
    setGameDraft('');
  };

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (!form.displayName.trim()) return setErrors({ displayName: 'Display name is required.' });
    save.mutate();
  };

  return (
    <form onSubmit={onSubmit} className="flex flex-col gap-6" noValidate>
      <Card>
        <CardHeader title="Identity" description="How other players see you across the network." />
        <div className="grid gap-5 p-5 md:grid-cols-[auto_1fr]">
          <Avatar avatar={form.avatar} name={form.displayName || user.onlineId} size="xl" className="mx-auto" />
          <div className="flex flex-col gap-4">
            <TextField
              label="Display name"
              name="displayName"
              value={form.displayName}
              maxLength={LIMITS.displayNameMax}
              onChange={(event) => setForm((current) => ({ ...current, displayName: event.target.value }))}
              error={errors.displayName}
              hint={`Your Online ID @${user.onlineId} stays the same.`}
            />
            <TextField
              label="Status message"
              name="statusMessage"
              value={form.statusMessage}
              maxLength={LIMITS.statusMessageMax}
              onChange={(event) => setForm((current) => ({ ...current, statusMessage: event.target.value }))}
              error={errors.statusMessage}
              placeholder="Looking for a squad tonight"
            />
          </div>
        </div>
        <fieldset className="border-t border-line p-5">
          <legend className="mb-3 text-sm font-medium">Avatar</legend>
          <div className="grid grid-cols-4 gap-3 sm:grid-cols-8" role="radiogroup" aria-label="Avatar">
            {AVATAR_PRESETS.map((preset) => {
              const selected = preset.id === form.avatar;
              return (
                <button
                  key={preset.id}
                  type="button"
                  role="radio"
                  aria-checked={selected}
                  aria-label={preset.label}
                  onClick={() => setForm((current) => ({ ...current, avatar: preset.id }))}
                  className={cn(
                    'flex flex-col items-center gap-1.5 rounded-xl border p-2 text-xs transition-colors',
                    selected ? 'border-accent bg-accent-soft text-ink' : 'border-transparent text-muted hover:bg-surface-2',
                  )}
                >
                  <Avatar avatar={preset.id} name={preset.label} size="md" />
                  {preset.label}
                </button>
              );
            })}
          </div>
        </fieldset>
      </Card>

      <Card>
        <CardHeader title="About you" />
        <div className="flex flex-col gap-4 p-5">
          <TextArea
            label="Bio"
            name="bio"
            value={form.bio}
            maxLength={LIMITS.bioMax}
            onChange={(event) => setForm((current) => ({ ...current, bio: event.target.value }))}
            error={errors.bio}
            hint={`${form.bio.length}/${LIMITS.bioMax} characters`}
            placeholder="Tell your friends what you like to play."
          />
          <TextField
            label="What are you playing?"
            name="activity"
            value={form.activity}
            maxLength={LIMITS.activityMax}
            onChange={(event) => setForm((current) => ({ ...current, activity: event.target.value }))}
            error={errors.activity}
            hint="Self-reported and shown with your presence. AKENO NETWORK does not detect what you play on any console or PC."
            placeholder="Playing a co-op adventure"
          />
          <div>
            <p className="mb-1.5 text-sm font-medium">Favorite games</p>
            <div className="flex gap-2">
              <label className="sr-only" htmlFor="favorite-game-input">
                Add a favorite game
              </label>
              <input
                id="favorite-game-input"
                value={gameDraft}
                maxLength={LIMITS.favoriteGameNameMax}
                onChange={(event) => setGameDraft(event.target.value)}
                onKeyDown={(event) => {
                  if (event.key === 'Enter') {
                    event.preventDefault();
                    addGame();
                  }
                }}
                placeholder="Add a game and press Enter"
                disabled={form.favoriteGames.length >= LIMITS.favoriteGamesMax}
                className="h-11 flex-1 rounded-xl border border-line-strong bg-surface-2 px-3.5 text-sm focus:border-accent focus:outline-none focus:ring-2 focus:ring-accent/30 disabled:opacity-60"
              />
              <Button onClick={addGame} disabled={!gameDraft.trim() || form.favoriteGames.length >= LIMITS.favoriteGamesMax} icon={<Plus aria-hidden className="size-4" />}>
                Add
              </Button>
            </div>
            <p className="mt-1.5 text-xs text-muted">
              Up to {LIMITS.favoriteGamesMax} games. {form.favoriteGames.length}/{LIMITS.favoriteGamesMax} used.
            </p>
            {form.favoriteGames.length > 0 ? (
              <ul className="mt-3 flex flex-wrap gap-2">
                {form.favoriteGames.map((game) => (
                  <li key={game} className="inline-flex items-center gap-1 rounded-lg border border-line bg-surface-2 py-1 pl-2.5 pr-1 text-sm">
                    {game}
                    <button
                      type="button"
                      onClick={() => setForm((current) => ({ ...current, favoriteGames: current.favoriteGames.filter((entry) => entry !== game) }))}
                      className="rounded p-0.5 text-subtle hover:text-ink"
                      aria-label={`Remove ${game}`}
                    >
                      <X aria-hidden className="size-3.5" />
                    </button>
                  </li>
                ))}
              </ul>
            ) : null}
          </div>
        </div>
      </Card>
      <div className="flex justify-end">
        <Button type="submit" variant="primary" loading={save.isPending} icon={<Save aria-hidden className="size-4" />}>
          Save profile
        </Button>
      </div>
    </form>
  );
}

const AUDIENCE = [
  { value: 'everyone', label: 'Everyone on this network' },
  { value: 'friends', label: 'Friends only' },
  { value: 'nobody', label: 'Nobody' },
];

function PrivacySettingsPanel() {
  const user = useCurrentUser();
  const { setUser } = useAuth();
  const toast = useToast();
  const confirm = useConfirm();
  const queryClient = useQueryClient();
  const [form, setForm] = useState<PrivacySettings>(user.privacy);
  const save = useMutation({
    mutationFn: () => api.patch<CurrentUser>('/profile/privacy', form),
    onSuccess: (updated) => {
      setUser(updated);
      toast.success('Privacy settings saved.');
    },
    onError: (error) => toast.error('Could not save privacy settings', errorMessage(error)),
  });
  const clearHistory = useMutation({
    mutationFn: () => api.delete<{ deleted: number }>('/activity/history'),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.allActivity });
      toast.success('Play history cleared.');
    },
    onError: (error) => toast.error('Could not clear play history', errorMessage(error)),
  });
  const set = <K extends keyof PrivacySettings>(key: K) => (event: React.ChangeEvent<HTMLSelectElement>) =>
    setForm((current) => ({ ...current, [key]: event.target.value as PrivacySettings[K] }));

  return (
    <Card>
      <CardHeader title="Privacy" description="Control who can see you and contact you. Blocked users can never see or contact you." />
      <form
        className="grid gap-5 p-5 md:grid-cols-2"
        onSubmit={(event) => {
          event.preventDefault();
          save.mutate();
        }}
      >
        <SelectField
          label="Who can see your profile details"
          value={form.profileVisibility}
          onChange={set('profileVisibility')}
          options={AUDIENCE.filter((option) => option.value !== 'nobody')}
          hint="Your Online ID, display name and avatar are always visible to signed-in members."
        />
        <SelectField
          label="Who can see when you are online"
          value={form.presenceVisibility}
          onChange={set('presenceVisibility')}
          options={AUDIENCE}
          hint="Use the Invisible status to appear offline temporarily."
        />
        <SelectField
          label="Who can see when you were last online"
          value={form.lastSeenVisibility}
          onChange={set('lastSeenVisibility')}
          options={AUDIENCE}
          hint="Only shown to people who can also see your presence."
        />
        <SelectField
          label="Who can send you direct messages"
          value={form.messagePrivacy}
          onChange={set('messagePrivacy')}
          options={AUDIENCE.filter((option) => option.value !== 'nobody')}
        />
        <SelectField
          label="Who can send you friend requests"
          value={form.friendRequestPrivacy}
          onChange={set('friendRequestPrivacy')}
          options={[
            { value: 'everyone', label: 'Everyone on this network' },
            { value: 'nobody', label: 'Nobody' },
          ]}
        />
        <fieldset className="grid gap-5 border-t border-line pt-5 md:col-span-2 md:grid-cols-2" aria-labelledby="game-activity-privacy">
          <legend id="game-activity-privacy" className="sr-only">
            Game activity
          </legend>
          <div className="md:col-span-2">
            <Switch
              label="Share the game I am playing"
              description="AKENO for PS5 detects the game running on your paired console and records how long you play. Turning this off ends the running session and stops new tracking; existing history is kept until you clear it."
              checked={form.gameActivityTracking}
              onChange={(value) => setForm((current) => ({ ...current, gameActivityTracking: value }))}
            />
          </div>
          <SelectField
            label="Who can see what you are playing"
            value={form.gameActivityVisibility}
            onChange={set('gameActivityVisibility')}
            options={AUDIENCE}
            disabled={!form.gameActivityTracking}
            hint="Only shown while you appear online. Invisible mode and blocks always hide it."
          />
          <SelectField
            label="Who can see your play history"
            value={form.gameHistoryVisibility}
            onChange={set('gameHistoryVisibility')}
            options={AUDIENCE}
            hint="Recent games, last played and total tracked playtime on your profile."
          />
          <div className="md:col-span-2">
            <Switch
              label="Show my playtime"
              description="When off, people who can see your play history see which games you played and when, but not for how long."
              checked={form.gamePlaytimeVisible}
              onChange={(value) => setForm((current) => ({ ...current, gamePlaytimeVisible: value }))}
            />
          </div>
          <ExcludedGamesField
            value={form.gameExcludedTitles}
            onChange={(titles) => setForm((current) => ({ ...current, gameExcludedTitles: titles }))}
          />
          <div className="flex flex-wrap items-center justify-between gap-3 md:col-span-2">
            <p className="text-xs text-muted">Playtime from before AKENO started tracking is never shown or estimated.</p>
            <Button
              type="button"
              variant="secondary"
              loading={clearHistory.isPending}
              icon={<Trash2 aria-hidden className="size-4" />}
              onClick={async () => {
                const ok = await confirm({
                  title: 'Clear your play history?',
                  message: 'All tracked game sessions and totals are deleted. This cannot be undone.',
                  confirmLabel: 'Clear history',
                });
                if (ok) clearHistory.mutate();
              }}
            >
              Clear play history
            </Button>
          </div>
        </fieldset>
        <div className="flex items-end justify-end md:col-span-2">
          <Button type="submit" variant="primary" loading={save.isPending} icon={<Save aria-hidden className="size-4" />}>
            Save privacy settings
          </Button>
        </div>
      </form>
    </Card>
  );
}

/** Games that are never tracked; earlier sessions of them are hidden from others. */
function ExcludedGamesField({ value, onChange }: { value: string[]; onChange: (titles: string[]) => void }) {
  const user = useCurrentUser();
  const activity = useGameActivity(user.id);
  const [draft, setDraft] = useState('');
  const normalized = draft.trim().toUpperCase();
  const valid = GAME_ACTIVITY.titleIdPattern.test(normalized);
  const full = value.length >= GAME_ACTIVITY.excludedTitlesMax;
  const add = (titleId: string) => {
    if (value.includes(titleId) || full) return;
    onChange([...value, titleId]);
    setDraft('');
  };
  const names = new Map((activity.data?.recentGames ?? []).map((game) => [game.titleId, game.name]));
  const suggestions = (activity.data?.recentGames ?? []).filter((game) => !value.includes(game.titleId)).slice(0, 5);
  return (
    <div className="md:col-span-2" data-testid="excluded-games">
      <p className="mb-1.5 text-sm font-medium">Games excluded from game activity</p>
      <p className="mb-2 text-xs text-muted">
        Excluded games are never tracked or shown as your current game, and their earlier sessions are hidden from everyone else. Use the
        title ID shown in your play history (for example PPSA01234).
      </p>
      <div className="flex gap-2">
        <label className="sr-only" htmlFor="excluded-game-input">
          Title ID to exclude
        </label>
        <input
          id="excluded-game-input"
          className="h-11 w-48 rounded-xl border border-line-strong bg-surface-2 px-3.5 font-mono text-sm focus:border-accent focus:outline-none focus:ring-2 focus:ring-accent/30 disabled:opacity-60"
          value={draft}
          maxLength={9}
          placeholder="PPSA01234"
          onChange={(event) => setDraft(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === 'Enter') {
              event.preventDefault();
              if (valid) add(normalized);
            }
          }}
          disabled={full}
        />
        <Button type="button" onClick={() => add(normalized)} disabled={!valid || full} icon={<Plus aria-hidden className="size-4" />}>
          Exclude
        </Button>
      </div>
      {suggestions.length > 0 ? (
        <p className="mt-2 flex flex-wrap items-center gap-2 text-xs text-muted">
          Recently played:
          {suggestions.map((game) => (
            <button key={game.titleId} type="button" className="rounded-md border border-line px-2 py-0.5 hover:bg-surface-2" onClick={() => add(game.titleId)}>
              Exclude {game.name ?? game.titleId}
            </button>
          ))}
        </p>
      ) : null}
      {value.length > 0 ? (
        <ul className="mt-2 flex flex-wrap gap-2">
          {value.map((titleId) => (
            <li key={titleId} className="inline-flex items-center gap-1 rounded-lg border border-line bg-surface-2 py-1 pl-2.5 pr-1 text-sm">
              {names.get(titleId) ? `${names.get(titleId)} · ` : ''}
              <span className="font-mono">{titleId}</span>
              <button
                type="button"
                className="rounded p-0.5 text-subtle hover:text-ink"
                onClick={() => onChange(value.filter((entry) => entry !== titleId))}
                aria-label={`Stop excluding ${titleId}`}
              >
                <X aria-hidden className="size-3.5" />
              </button>
            </li>
          ))}
        </ul>
      ) : null}
      <p className="mt-1 text-xs text-subtle">Saved with the privacy settings below.</p>
    </div>
  );
}

function describeDevice(session: AuthSessionDto): { label: string; mobile: boolean } {
  const agent = session.userAgent ?? '';
  const mobile = /Mobile|Android|iPhone|iPad/i.test(agent);
  const browser = /Edg\//.test(agent)
    ? 'Edge'
    : /Firefox\//.test(agent)
      ? 'Firefox'
      : /Chrome\//.test(agent)
        ? 'Chrome'
        : /Safari\//.test(agent)
          ? 'Safari'
          : 'Browser';
  const os = /Windows/.test(agent)
    ? 'Windows'
    : /Android/.test(agent)
      ? 'Android'
      : /iPhone|iPad|iOS/.test(agent)
        ? 'iOS'
        : /Mac OS X/.test(agent)
          ? 'macOS'
          : /Linux/.test(agent)
            ? 'Linux'
            : /PlayStation/.test(agent)
              ? 'Console browser'
              : 'Unknown system';
  return { label: `${browser} on ${os}`, mobile };
}

function SecuritySettings() {
  const toast = useToast();
  const confirm = useConfirm();
  const queryClient = useQueryClient();
  const { signedOut } = useAuth();
  const user = useCurrentUser();
  const sessions = useAuthSessions();
  const [passwords, setPasswords] = useState({ currentPassword: '', newPassword: '', confirm: '' });
  const [passwordErrors, setPasswordErrors] = useState<Record<string, string>>({});
  const [deleteOpen, setDeleteOpen] = useState(false);
  const [deleteForm, setDeleteForm] = useState({ password: '', confirmation: '' });

  const changePassword = useMutation({
    mutationFn: () => api.post('/auth/password', { currentPassword: passwords.currentPassword, newPassword: passwords.newPassword }),
    onSuccess: () => {
      setPasswords({ currentPassword: '', newPassword: '', confirm: '' });
      setPasswordErrors({});
      void queryClient.invalidateQueries({ queryKey: queryKeys.authSessions });
      toast.success('Password changed.', 'All your other devices were signed out.');
    },
    onError: (error) => {
      if (error instanceof ApiError) setPasswordErrors(error.fields);
      toast.error('Could not change your password', errorMessage(error));
    },
  });

  const revoke = useMutation({
    mutationFn: (id: string) => api.delete(`/auth/sessions/${id}`),
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.authSessions });
      toast.success('Session signed out.');
    },
    onError: (error) => toast.error('Could not sign out the session', errorMessage(error)),
  });

  const revokeOthers = useMutation({
    mutationFn: () => api.post<{ revoked: number }>('/auth/sessions/revoke-others'),
    onSuccess: (result) => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.authSessions });
      toast.success(result.revoked === 1 ? 'Signed out 1 other session.' : `Signed out ${result.revoked} other sessions.`);
    },
    onError: (error) => toast.error('Could not sign out other sessions', errorMessage(error)),
  });

  const deleteAccount = useMutation({
    mutationFn: () => api.delete('/users/me', deleteForm),
    onSuccess: () => {
      setDeleteOpen(false);
      signedOut('Your account was deleted. Thanks for being part of AKENO NETWORK.');
    },
  });

  const submitPassword = (event: FormEvent) => {
    event.preventDefault();
    const errors: Record<string, string> = {};
    if (!passwords.currentPassword) errors.currentPassword = 'Enter your current password.';
    if (passwords.newPassword.length < LIMITS.passwordMin) errors.newPassword = `Use at least ${LIMITS.passwordMin} characters.`;
    else {
      const quality = checkPasswordQuality(passwords.newPassword, user.onlineId);
      if (quality) errors.newPassword = quality;
    }
    if (passwords.confirm !== passwords.newPassword) errors.confirm = 'The passwords do not match.';
    setPasswordErrors(errors);
    if (Object.keys(errors).length === 0) changePassword.mutate();
  };

  return (
    <div className="flex flex-col gap-6">
      <Card>
        <CardHeader title="Change password" icon={<KeyRound className="size-5" />} description="Changing your password signs out all other devices." />
        <form className="grid gap-4 p-5 md:grid-cols-3" onSubmit={submitPassword} noValidate>
          <TextField
            label="Current password"
            type="password"
            autoComplete="current-password"
            value={passwords.currentPassword}
            onChange={(event) => setPasswords((current) => ({ ...current, currentPassword: event.target.value }))}
            error={passwordErrors.currentPassword}
          />
          <TextField
            label="New password"
            type="password"
            autoComplete="new-password"
            value={passwords.newPassword}
            onChange={(event) => setPasswords((current) => ({ ...current, newPassword: event.target.value }))}
            error={passwordErrors.newPassword}
            hint={`At least ${LIMITS.passwordMin} characters.`}
          />
          <TextField
            label="Confirm new password"
            type="password"
            autoComplete="new-password"
            value={passwords.confirm}
            onChange={(event) => setPasswords((current) => ({ ...current, confirm: event.target.value }))}
            error={passwordErrors.confirm}
          />
          <div className="flex justify-end md:col-span-3">
            <Button type="submit" variant="primary" loading={changePassword.isPending}>
              Change password
            </Button>
          </div>
        </form>
      </Card>

      <Card>
        <CardHeader
          title="Signed-in devices"
          description="Sessions expire after inactivity. Sign out any device you do not recognize."
          action={
            <Button
              size="sm"
              variant="ghost"
              loading={revokeOthers.isPending}
              onClick={() => revokeOthers.mutate()}
              icon={<LogOut aria-hidden className="size-4" />}
            >
              Sign out others
            </Button>
          }
        />
        {sessions.isPending ? (
          <Skeleton className="m-5 h-20" />
        ) : sessions.isError ? (
          <ErrorState error={sessions.error} onRetry={() => void sessions.refetch()} />
        ) : (
          <ul className="divide-y divide-line">
            {sessions.data.map((session) => {
              const device = describeDevice(session);
              const Icon = device.mobile ? Smartphone : Laptop;
              return (
                <li key={session.id} className="flex items-center gap-3 px-5 py-3">
                  <Icon aria-hidden className="size-5 text-subtle" />
                  <div className="min-w-0 flex-1">
                    <p className="flex items-center gap-2 text-sm font-medium">
                      {device.label}
                      {session.current ? <Badge tone="success" icon={<Check aria-hidden className="size-3" />}>This device</Badge> : null}
                    </p>
                    <p className="text-xs text-muted">
                      Last active <RelativeTime iso={session.lastUsedAt} />
                      {session.ipAddress ? ` · ${session.ipAddress}` : ''}
                    </p>
                  </div>
                  {!session.current ? (
                    <Button size="sm" variant="ghost" onClick={() => revoke.mutate(session.id)}>
                      Sign out
                    </Button>
                  ) : null}
                </li>
              );
            })}
          </ul>
        )}
      </Card>

      <PairedDevicesCard />

      <Card className="border-danger/30">
        <CardHeader title="Delete account" icon={<ShieldAlert className="size-5 text-danger" />} description="Permanently remove your account from this network." />
        <div className="flex flex-col gap-4 p-5 sm:flex-row sm:items-center sm:justify-between">
          <p className="max-w-xl text-sm text-muted">
            Your profile, friends, blocks, notifications and sessions are removed and your Online ID is released. Messages
            you already sent stay visible to their recipients as “Deleted user”.
          </p>
          <Button
            variant="danger"
            icon={<Trash2 aria-hidden className="size-4" />}
            onClick={async () => {
              const ok = await confirm({
                title: 'Delete your account?',
                message: 'This cannot be undone. You will be asked for your password next.',
                confirmLabel: 'Continue',
              });
              if (ok) setDeleteOpen(true);
            }}
          >
            Delete account
          </Button>
        </div>
      </Card>

      <Modal
        open={deleteOpen}
        onClose={() => setDeleteOpen(false)}
        title="Confirm account deletion"
        description="Enter your password and type DELETE to confirm."
        footer={
          <>
            <Button variant="ghost" onClick={() => setDeleteOpen(false)}>
              Cancel
            </Button>
            <Button
              variant="danger"
              loading={deleteAccount.isPending}
              disabled={deleteForm.confirmation !== 'DELETE' || !deleteForm.password}
              onClick={() => deleteAccount.mutate()}
            >
              Delete my account
            </Button>
          </>
        }
      >
        <div className="flex flex-col gap-4">
          <TextField
            label="Password"
            type="password"
            autoComplete="current-password"
            value={deleteForm.password}
            onChange={(event) => setDeleteForm((current) => ({ ...current, password: event.target.value }))}
          />
          <TextField
            label='Type "DELETE" to confirm'
            value={deleteForm.confirmation}
            onChange={(event) => setDeleteForm((current) => ({ ...current, confirmation: event.target.value }))}
          />
          <InlineError message={deleteAccount.isError ? errorMessage(deleteAccount.error) : null} />
        </div>
      </Modal>
    </div>
  );
}

function PreferencesPanel() {
  const [theme, setThemeState] = useState<Theme>(getTheme);
  const [controller, setController] = useState(getControllerNavigation);
  return (
    <Card>
      <CardHeader title="This device" description="These preferences are stored in this browser only." />
      <div className="flex flex-col gap-5 p-5">
        <Switch
          label="Light theme"
          description="Dark mode is the default. Switch to a light interface on this device."
          checked={theme === 'light'}
          onChange={(value) => {
            const next = value ? 'light' : 'dark';
            setTheme(next);
            setThemeState(next);
          }}
        />
        <Switch
          label="Controller navigation (experimental)"
          description="Use a gamepad connected to this browser: D-pad or left stick moves focus, A selects, B goes back. Uses the standard browser Gamepad API only."
          checked={controller}
          onChange={(value) => {
            setControllerNavigation(value);
            setController(value);
          }}
        />
        <Notice>
          Voice chat is not available in this release. Parties currently support text chat only.
        </Notice>
      </div>
    </Card>
  );
}

export function SettingsPage() {
  const [params, setParams] = useSearchParams();
  const tab = (['profile', 'privacy', 'security', 'preferences'] as const).find((value) => value === params.get('tab')) ?? 'profile';
  return (
    <div className="mx-auto max-w-4xl">
      <PageHeader title="Settings" description="Manage your profile, privacy and account security." icon={<Settings />} />
      <Tabs<Tab>
        className="mb-6"
        label="Settings sections"
        value={tab}
        onChange={(value) => setParams(value === 'profile' ? {} : { tab: value }, { replace: true })}
        items={[
          { value: 'profile', label: 'Profile' },
          { value: 'privacy', label: 'Privacy' },
          { value: 'security', label: 'Account & security' },
          { value: 'preferences', label: 'Preferences' },
        ]}
      />
      <div role="tabpanel" aria-label={tab}>
        {tab === 'profile' ? <ProfileSettings /> : null}
        {tab === 'privacy' ? <PrivacySettingsPanel /> : null}
        {tab === 'security' ? <SecuritySettings /> : null}
        {tab === 'preferences' ? <PreferencesPanel /> : null}
      </div>
    </div>
  );
}
