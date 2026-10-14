import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { Check, Gamepad2, ImagePlus, Link2, Plus, RefreshCw, Search, Trash2, X } from 'lucide-react';
import { useState } from 'react';
import {
  GAME_ACTIVITY,
  GAME_ARTWORK_SOURCES,
  GAME_ARTWORK_SOURCE_LABELS,
  GAME_ARTWORK_SLOTS,
  GAME_CATALOG_LIMITS,
  type DetectedTitleDto,
  type GameArtworkSlot,
  type GameArtworkSource,
  type GameDto,
} from '@akeno/shared';
import { Badge } from '../../components/Badge';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { cn } from '../../components/cn';
import { useConfirm } from '../../components/ConfirmDialog';
import { SelectField, Switch, TextField } from '../../components/Field';
import { GameArt } from '../../components/GameArt';
import { Modal } from '../../components/Modal';
import { RelativeTime } from '../../components/RelativeTime';
import { Skeleton } from '../../components/Spinner';
import { EmptyState, ErrorState, InlineError, Notice } from '../../components/States';
import { Tabs } from '../../components/Tabs';
import { useToast } from '../../components/Toast';
import { useDebounce } from '../../hooks/useDebounce';
import { api, errorMessage, query } from '../../lib/api';

const GAMES_KEY = ['admin', 'games'] as const;
const DETECTED_KEY = ['admin', 'games', 'detected'] as const;

const SLOT_LABEL: Record<GameArtworkSlot, string> = { icon: 'Square icon', cover: 'Vertical cover (2:3)', banner: 'Wide artwork (16:9)' };
const SLOT_HINT: Record<GameArtworkSlot, string> = {
  icon: 'At least 128 × 128. Used in lists and activity lines.',
  cover: 'At least 200 × 300. Used for libraries and recently played.',
  banner: 'At least 480 × 270. Optional, for large "now playing" cards.',
};

function useGames(q: string, missingArtwork: boolean) {
  return useQuery({
    queryKey: [...GAMES_KEY, q, missingArtwork],
    queryFn: () => api.get<{ games: GameDto[] }>(`/admin/games${query({ q, missingArtwork: missingArtwork ? 'true' : undefined })}`),
    select: (data) => data.games,
  });
}

function useDetected() {
  return useQuery({
    queryKey: DETECTED_KEY,
    queryFn: () => api.get<{ titles: DetectedTitleDto[] }>('/admin/games/detected-titles'),
    select: (data) => data.titles,
  });
}

function useInvalidateGames() {
  const queryClient = useQueryClient();
  return () => {
    void queryClient.invalidateQueries({ queryKey: GAMES_KEY });
    void queryClient.invalidateQueries({ queryKey: ['activity'] });
    void queryClient.invalidateQueries({ queryKey: ['friends'] });
  };
}

function ArtworkSlot({ game, slot, onChanged }: { game: GameDto; slot: GameArtworkSlot; onChanged: (game: GameDto) => void }) {
  const toast = useToast();
  const [error, setError] = useState<string | null>(null);
  const upload = useMutation({
    mutationFn: (file: File) => api.upload<GameDto>(`/admin/games/${game.id}/artwork/${slot}`, file),
    onSuccess: (updated) => {
      setError(null);
      onChanged(updated);
      toast.success(`${SLOT_LABEL[slot]} saved.`);
    },
    onError: (failure) => setError(errorMessage(failure)),
  });
  const remove = useMutation({
    mutationFn: () => api.delete<GameDto>(`/admin/games/${game.id}/artwork/${slot}`),
    onSuccess: onChanged,
    onError: (failure) => toast.error('Could not remove the artwork', errorMessage(failure)),
  });
  const present = Boolean(game.artwork[slot]);
  const preview = { ...game.artwork, icon: slot === 'icon' ? game.artwork.icon : null, cover: slot === 'cover' ? game.artwork.cover : null, banner: slot === 'banner' ? game.artwork.banner : null };
  return (
    <div className="flex flex-col gap-2 rounded-xl border border-line p-3">
      <p className="text-sm font-medium">{SLOT_LABEL[slot]}</p>
      <div className={cn('mx-auto', slot === 'icon' ? 'w-24' : slot === 'cover' ? 'w-24' : 'w-full')}>
        <GameArt art={present ? preview : null} name={game.name} variant={slot} sizes="200px" className="w-full" />
      </div>
      <p className="text-xs text-muted">{SLOT_HINT[slot]}</p>
      <div className="flex flex-wrap gap-2">
        <label className={cn('inline-flex h-8 cursor-pointer items-center gap-1.5 rounded-xl border border-line-strong bg-surface-2 px-3 text-sm font-medium hover:bg-surface-3', upload.isPending && 'pointer-events-none opacity-60')}>
          <ImagePlus aria-hidden className="size-4" />
          {upload.isPending ? 'Uploading…' : present ? 'Replace' : 'Upload'}
          <input
            type="file"
            accept="image/jpeg,image/png,image/webp,image/gif"
            className="sr-only"
            data-testid={`artwork-input-${slot}`}
            onChange={(event) => {
              const file = event.target.files?.[0];
              event.target.value = '';
              if (file) upload.mutate(file);
            }}
          />
        </label>
        {present ? (
          <Button size="sm" variant="ghost" icon={<Trash2 aria-hidden className="size-4" />} loading={remove.isPending} onClick={() => remove.mutate()}>
            Remove
          </Button>
        ) : null}
      </div>
      <InlineError message={error} />
    </div>
  );
}

function GameEditor({ game: initial, onClose }: { game: GameDto; onClose: () => void }) {
  const toast = useToast();
  const confirm = useConfirm();
  const invalidate = useInvalidateGames();
  const [game, setGame] = useState(initial);
  const [form, setForm] = useState({
    name: initial.name,
    aliases: initial.aliases.join(', '),
    artworkSource: initial.artworkSource ?? '',
    artworkAttribution: initial.artworkAttribution,
    artworkLicense: initial.artworkLicense,
  });
  const [titleDraft, setTitleDraft] = useState('');
  const changed = (updated: GameDto) => {
    setGame(updated);
    invalidate();
  };
  const save = useMutation({
    mutationFn: () =>
      api.patch<GameDto>(`/admin/games/${game.id}`, {
        name: form.name,
        aliases: form.aliases.split(',').map((alias) => alias.trim()).filter(Boolean),
        artworkSource: form.artworkSource || null,
        artworkAttribution: form.artworkAttribution,
        artworkLicense: form.artworkLicense,
      }),
    onSuccess: (updated) => {
      changed(updated);
      toast.success('Game saved.');
    },
    onError: (error) => toast.error('Could not save the game', errorMessage(error)),
  });
  const mapTitle = useMutation({
    mutationFn: (titleId: string) => api.put<GameDto>(`/admin/games/${game.id}/titles/${titleId}`, {}),
    onSuccess: (updated) => {
      changed(updated);
      setTitleDraft('');
    },
    onError: (error) => toast.error('Could not add the title ID', errorMessage(error)),
  });
  const unmapTitle = useMutation({
    mutationFn: (titleId: string) => api.delete(`/admin/games/${game.id}/titles/${titleId}`),
    onSuccess: (_result, titleId) => changed({ ...game, titleIds: game.titleIds.filter((entry) => entry.titleId !== titleId) }),
    onError: (error) => toast.error('Could not remove the title ID', errorMessage(error)),
  });
  const remove = useMutation({
    mutationFn: () => api.delete(`/admin/games/${game.id}`),
    onSuccess: () => {
      invalidate();
      toast.success('Game deleted.');
      onClose();
    },
    onError: (error) => toast.error('Could not delete the game', errorMessage(error)),
  });
  const titleValid = GAME_ACTIVITY.titleIdPattern.test(titleDraft.trim().toUpperCase());

  return (
    <Modal
      open
      onClose={onClose}
      size="lg"
      title={game.name}
      description="Artwork appears automatically wherever a mapped title ID is detected, and for favorite games or sessions with exactly this name."
      footer={
        <>
          <Button
            variant="ghost"
            className="mr-auto text-danger hover:text-danger"
            icon={<Trash2 aria-hidden className="size-4" />}
            onClick={async () => {
              if (await confirm({ title: `Delete ${game.name}?`, message: 'Its title IDs become unverified again and its artwork is deleted.', confirmLabel: 'Delete game' })) remove.mutate();
            }}
          >
            Delete
          </Button>
          <Button variant="ghost" onClick={onClose}>
            Close
          </Button>
          <Button variant="primary" loading={save.isPending} onClick={() => save.mutate()}>
            Save details
          </Button>
        </>
      }
    >
      <div className="flex flex-col gap-5">
        <div className="grid gap-4 sm:grid-cols-2">
          <TextField label="Name" value={form.name} maxLength={GAME_CATALOG_LIMITS.nameMax} onChange={(event) => setForm({ ...form, name: event.target.value })} />
          <TextField
            label="Alternative names"
            value={form.aliases}
            onChange={(event) => setForm({ ...form, aliases: event.target.value })}
            hint="Comma separated. Used for exact matching of favorite games and session names."
          />
        </div>
        <div>
          <p className="mb-1.5 text-sm font-medium">PlayStation title IDs</p>
          <div className="flex flex-wrap gap-2">
            {game.titleIds.map((title) => (
              <span key={title.titleId} className="inline-flex items-center gap-1.5 rounded-lg border border-line bg-surface-2 py-1 pl-2.5 pr-1 font-mono text-xs">
                {title.titleId}
                <span className="font-sans text-subtle">{title.platform ?? ''}</span>
                <button type="button" aria-label={`Remove ${title.titleId}`} className="rounded p-0.5 text-subtle hover:text-ink" onClick={() => unmapTitle.mutate(title.titleId)}>
                  <X aria-hidden className="size-3.5" />
                </button>
              </span>
            ))}
            {game.titleIds.length === 0 ? <span className="text-sm text-muted">No title IDs yet.</span> : null}
          </div>
          <form
            className="mt-2 flex gap-2"
            onSubmit={(event) => {
              event.preventDefault();
              if (titleValid) mapTitle.mutate(titleDraft.trim().toUpperCase());
            }}
          >
            <label className="sr-only" htmlFor="title-id-draft">
              Add a title ID
            </label>
            <input
              id="title-id-draft"
              value={titleDraft}
              maxLength={9}
              onChange={(event) => setTitleDraft(event.target.value.toUpperCase())}
              placeholder="PPSA01234 or CUSA01234"
              className="h-10 w-56 rounded-xl border border-line-strong bg-surface-2 px-3 font-mono text-sm focus:border-accent focus:outline-none"
            />
            <Button type="submit" size="md" disabled={!titleValid} loading={mapTitle.isPending} icon={<Link2 aria-hidden className="size-4" />}>
              Map title
            </Button>
          </form>
          <p className="mt-1.5 text-xs text-muted">Only map a title ID after checking it really is this game (and platform). Unknown titles stay without artwork.</p>
        </div>
        <div className="grid gap-3 sm:grid-cols-3">
          {GAME_ARTWORK_SLOTS.map((slot) => (
            <ArtworkSlot key={slot} game={game} slot={slot} onChanged={changed} />
          ))}
        </div>
        <fieldset className="grid gap-4 rounded-xl border border-line p-4 sm:grid-cols-2">
          <legend className="px-1 text-sm font-medium">Artwork source and rights</legend>
          <SelectField
            label="Source"
            value={form.artworkSource}
            onChange={(event) => setForm({ ...form, artworkSource: event.target.value as GameArtworkSource | '' })}
            options={[{ value: '', label: 'Not recorded' }, ...GAME_ARTWORK_SOURCES.map((source) => ({ value: source, label: GAME_ARTWORK_SOURCE_LABELS[source] }))]}
          />
          <TextField
            label="Attribution"
            value={form.artworkAttribution}
            maxLength={GAME_CATALOG_LIMITS.attributionMax}
            onChange={(event) => setForm({ ...form, artworkAttribution: event.target.value })}
            placeholder="© Publisher, from the official press kit"
          />
          <div className="sm:col-span-2">
            <TextField
              label="Licence / usage notes"
              value={form.artworkLicense}
              maxLength={GAME_CATALOG_LIMITS.licenseMax}
              onChange={(event) => setForm({ ...form, artworkLicense: event.target.value })}
              hint="Record where the artwork came from and under which terms you may show it on this server."
            />
          </div>
        </fieldset>
      </div>
    </Modal>
  );
}

function CreateGameDialog({ initial, onClose, onCreated }: { initial: { name: string; titleId: string | null }; onClose: () => void; onCreated: (game: GameDto) => void }) {
  const toast = useToast();
  const [name, setName] = useState(initial.name);
  const create = useMutation({
    mutationFn: () => api.post<GameDto>('/admin/games', { name, titleIds: initial.titleId ? [initial.titleId] : [] }),
    onSuccess: (game) => {
      toast.success('Game created. Add its artwork next.');
      onCreated(game);
    },
    onError: (error) => toast.error('Could not create the game', errorMessage(error)),
  });
  return (
    <Modal
      open
      onClose={onClose}
      size="sm"
      title="New game"
      description={initial.titleId ? `Creates a verified game for ${initial.titleId}.` : 'Create a game, then map title IDs and add artwork.'}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" disabled={!name.trim()} loading={create.isPending} onClick={() => create.mutate()}>
            Create game
          </Button>
        </>
      }
    >
      <TextField label="Official name" value={name} maxLength={GAME_CATALOG_LIMITS.nameMax} onChange={(event) => setName(event.target.value)} hint="Check the name: the console-reported name may be abbreviated or localized." />
    </Modal>
  );
}

function DetectedTitles({ onEdit, onCreate }: { onEdit: (gameId: string) => void; onCreate: (initial: { name: string; titleId: string }) => void }) {
  const detected = useDetected();
  if (detected.isPending) return <Skeleton className="m-5 h-24" />;
  if (detected.isError) return <ErrorState error={detected.error} onRetry={() => void detected.refetch()} />;
  if (detected.data.length === 0) {
    return <EmptyState icon={<Gamepad2 className="size-6" />} title="No games detected yet" description="Titles appear here once a paired console reports them." />;
  }
  return (
    <ul className="divide-y divide-line" data-testid="detected-titles">
      {detected.data.map((title) => {
        const missing = title.game ? !title.game.hasIcon || !title.game.hasCover : true;
        return (
          <li key={title.titleId} className="flex flex-col gap-2 px-5 py-3 sm:flex-row sm:items-center">
            <div className="min-w-0 flex-1">
              <p className="flex flex-wrap items-center gap-2 font-medium">
                <code className="font-mono text-sm">{title.titleId}</code>
                {title.platform ? <Badge>{title.platform}</Badge> : null}
                {title.game ? <Badge tone="success">Mapped</Badge> : <Badge tone="warning">Unverified</Badge>}
                {title.game && missing ? <Badge tone="neutral">Artwork missing</Badge> : null}
              </p>
              <p className="truncate text-xs text-muted">
                {title.game ? `Game: ${title.game.name}` : title.reportedName ? `Console reports: ${title.reportedName}` : 'No name reported'} · {title.players} player
                {title.players === 1 ? '' : 's'} · {title.sessions} session{title.sessions === 1 ? '' : 's'} · last <RelativeTime iso={title.lastPlayedAt} />
              </p>
            </div>
            {title.game ? (
              <Button size="sm" variant={missing ? 'subtle' : 'ghost'} onClick={() => onEdit(title.game!.id)}>
                {missing ? 'Add artwork' : 'Edit game'}
              </Button>
            ) : (
              <Button size="sm" variant="subtle" icon={<Plus aria-hidden className="size-4" />} onClick={() => onCreate({ name: title.reportedName ?? '', titleId: title.titleId })}>
                Create game
              </Button>
            )}
          </li>
        );
      })}
    </ul>
  );
}

function Catalogue({ onEdit }: { onEdit: (gameId: string) => void }) {
  const [q, setQ] = useState('');
  const [missing, setMissing] = useState(false);
  const debounced = useDebounce(q, 300);
  const games = useGames(debounced.trim(), missing);
  return (
    <>
      <div className="flex flex-col gap-3 border-b border-line p-4 sm:flex-row sm:items-center">
        <label className="relative block flex-1">
          <span className="sr-only">Search games</span>
          <Search aria-hidden className="absolute left-3.5 top-1/2 size-4 -translate-y-1/2 text-subtle" />
          <input
            value={q}
            onChange={(event) => setQ(event.target.value)}
            placeholder="Search by name or title ID"
            className="h-10 w-full rounded-xl border border-line-strong bg-surface-2 pl-10 pr-3 text-sm focus:border-accent focus:outline-none"
          />
        </label>
        <div className="sm:w-64">
          <Switch label="Missing artwork only" checked={missing} onChange={setMissing} />
        </div>
      </div>
      {games.isPending ? (
        <Skeleton className="m-5 h-24" />
      ) : games.isError ? (
        <ErrorState error={games.error} onRetry={() => void games.refetch()} />
      ) : games.data.length === 0 ? (
        <EmptyState title="No games found" description="Create a game from a detected title or with “New game”." />
      ) : (
        <ul className="divide-y divide-line" data-testid="game-catalogue">
          {games.data.map((game) => (
            <li key={game.id}>
              <button type="button" onClick={() => onEdit(game.id)} className="flex w-full items-center gap-3 px-5 py-3 text-left hover:bg-surface-2">
                <GameArt art={game.artwork} name={game.name} className="size-11" sizes="44px" decorative />
                <span className="min-w-0 flex-1">
                  <span className="block truncate font-medium text-ink">{game.name}</span>
                  <span className="block truncate font-mono text-xs text-muted">{game.titleIds.map((title) => title.titleId).join(' · ') || 'No title IDs'}</span>
                </span>
                <span className="hidden items-center gap-1.5 sm:flex">
                  {GAME_ARTWORK_SLOTS.map((slot) => (
                    <Badge key={slot} tone={game.artwork[slot] ? 'success' : 'neutral'} icon={game.artwork[slot] ? <Check aria-hidden className="size-3" /> : undefined}>
                      {slot}
                    </Badge>
                  ))}
                </span>
              </button>
            </li>
          ))}
        </ul>
      )}
    </>
  );
}

export function GamesPanel() {
  const toast = useToast();
  const invalidate = useInvalidateGames();
  const queryClient = useQueryClient();
  const [view, setView] = useState<'detected' | 'catalogue'>('detected');
  const [editing, setEditing] = useState<GameDto | null>(null);
  const [creating, setCreating] = useState<{ name: string; titleId: string | null } | null>(null);
  const openEditor = async (gameId: string) => {
    try {
      setEditing(await api.get<GameDto>(`/admin/games/${gameId}`));
    } catch (error) {
      toast.error('Could not open the game', errorMessage(error));
    }
  };
  const reload = useMutation({
    mutationFn: () => api.post<{ games: number; titles: number }>('/admin/games/reload'),
    onSuccess: (result) => {
      invalidate();
      toast.success(`Catalogue reloaded: ${result.games} games, ${result.titles} title IDs.`);
    },
    onError: (error) => toast.error('Could not reload the catalogue', errorMessage(error)),
  });

  return (
    <div className="flex flex-col gap-4">
      <Notice icon={<Gamepad2 className="size-4" />}>
        Only add artwork you are allowed to show (for example from a publisher's press kit or your own captures) and record its source. AKENO never
        downloads artwork from PlayStation Network or other websites by itself.
      </Notice>
      <Card>
        <CardHeader
          title="Games and artwork"
          icon={<Gamepad2 className="size-5" />}
          description="Verified games, their PlayStation title IDs and artwork."
          action={
            <div className="flex gap-2">
              <Button size="sm" variant="ghost" icon={<RefreshCw aria-hidden className="size-4" />} loading={reload.isPending} onClick={() => reload.mutate()}>
                Reload
              </Button>
              <Button size="sm" variant="primary" icon={<Plus aria-hidden className="size-4" />} onClick={() => setCreating({ name: '', titleId: null })}>
                New game
              </Button>
            </div>
          }
        />
        <div className="border-b border-line px-4 py-3">
          <Tabs
            label="Game views"
            value={view}
            onChange={setView}
            className="w-fit"
            items={[
              { value: 'detected', label: 'Detected titles' },
              { value: 'catalogue', label: 'Catalogue' },
            ]}
          />
        </div>
        {view === 'detected' ? <DetectedTitles onEdit={(id) => void openEditor(id)} onCreate={setCreating} /> : <Catalogue onEdit={(id) => void openEditor(id)} />}
      </Card>
      {creating ? (
        <CreateGameDialog
          initial={creating}
          onClose={() => setCreating(null)}
          onCreated={(game) => {
            setCreating(null);
            invalidate();
            void queryClient.invalidateQueries({ queryKey: DETECTED_KEY });
            setEditing(game);
          }}
        />
      ) : null}
      {editing ? (
        <GameEditor
          key={editing.id}
          game={editing}
          onClose={() => {
            setEditing(null);
            void queryClient.invalidateQueries({ queryKey: DETECTED_KEY });
          }}
        />
      ) : null}
    </div>
  );
}
