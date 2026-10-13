import { useMutation, useQueryClient } from '@tanstack/react-query';
import { useState, type FormEvent } from 'react';
import { useNavigate } from 'react-router';
import {
  GAME_PLATFORMS,
  LIMITS,
  SUGGESTED_REGIONS,
  type GameSessionDetailDto,
  type GameSessionJoinMode,
  type GameSessionVisibility,
} from '@akeno/shared';
import { Button } from '../../components/Button';
import { SelectField, TextArea, TextField } from '../../components/Field';
import { Modal } from '../../components/Modal';
import { InlineError, Notice } from '../../components/States';
import { useToast } from '../../components/Toast';
import { ApiError, api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';

export function SessionFormDialog({ open, onClose, session }: { open: boolean; onClose: () => void; session?: GameSessionDetailDto }) {
  const queryClient = useQueryClient();
  const navigate = useNavigate();
  const toast = useToast();
  const [form, setForm] = useState({
    gameName: session?.gameName ?? '',
    gameIdentifier: session?.gameIdentifier ?? '',
    description: session?.description ?? '',
    visibility: (session?.visibility ?? 'friends') as GameSessionVisibility,
    joinMode: (session?.joinMode ?? 'open') as GameSessionJoinMode,
    maxParticipants: String(session?.maxParticipants ?? 4),
    region: session?.region ?? '',
    platform: session?.platform ?? '',
    connectionInfo: session?.connectionInfo ?? '',
  });
  const [errors, setErrors] = useState<Record<string, string>>({});
  const set = (key: keyof typeof form) => (event: React.ChangeEvent<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>) =>
    setForm((current) => ({ ...current, [key]: event.target.value }));

  const mutation = useMutation({
    mutationFn: () => {
      const shared = {
        description: form.description,
        visibility: form.visibility,
        joinMode: form.joinMode,
        maxParticipants: Number(form.maxParticipants),
        region: form.region.trim() || null,
        platform: form.platform || null,
        connectionInfo: form.connectionInfo.trim() || null,
      };
      return session
        ? api.patch<GameSessionDetailDto>(`/game-sessions/${session.id}`, shared)
        : api.post<GameSessionDetailDto>('/game-sessions', {
            ...shared,
            gameName: form.gameName,
            gameIdentifier: form.gameIdentifier.trim() || null,
          });
    },
    onSuccess: (result) => {
      queryClient.setQueryData(queryKeys.gameSession(result.id), result);
      void queryClient.invalidateQueries({ queryKey: queryKeys.allGameSessions });
      toast.success(session ? 'Session updated.' : 'Session created.');
      onClose();
      if (!session) void navigate(`/sessions/${result.id}`);
    },
    onError: (error) => {
      if (error instanceof ApiError) setErrors(error.fields);
    },
  });

  const onSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (!session && !form.gameName.trim()) return setErrors({ gameName: 'Enter the game you want to play.' });
    mutation.mutate();
  };

  return (
    <Modal
      open={open}
      onClose={onClose}
      size="lg"
      title={session ? 'Edit session' : 'Host a game session'}
      description="Create a lobby so friends can find you and coordinate a match."
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button type="submit" form="session-form" variant="primary" loading={mutation.isPending}>
            {session ? 'Save changes' : 'Create session'}
          </Button>
        </>
      }
    >
      <form id="session-form" className="grid gap-4 sm:grid-cols-2" onSubmit={onSubmit} noValidate>
        <TextField
          label="Game"
          name="gameName"
          value={form.gameName}
          onChange={set('gameName')}
          maxLength={LIMITS.gameNameMax}
          error={errors.gameName}
          disabled={Boolean(session)}
          required
          autoFocus={!session}
          containerClassName="sm:col-span-2"
        />
        <SelectField
          label="Platform (label only)"
          value={form.platform}
          onChange={set('platform')}
          options={[{ value: '', label: 'Not specified' }, ...GAME_PLATFORMS.map((platform) => ({ value: platform, label: platform }))]}
        />
        <div>
          <TextField label="Region" name="region" list="region-suggestions" value={form.region} onChange={set('region')} maxLength={LIMITS.gameSessionRegionMax} placeholder="Optional" />
          <datalist id="region-suggestions">
            {SUGGESTED_REGIONS.map((region) => (
              <option key={region} value={region} />
            ))}
          </datalist>
        </div>
        <SelectField
          label="Who can see it"
          value={form.visibility}
          onChange={set('visibility')}
          options={[
            { value: 'public', label: 'Everyone on the network' },
            { value: 'friends', label: 'My friends' },
            { value: 'private', label: 'Invited people only' },
          ]}
        />
        <SelectField
          label="How people join"
          value={form.joinMode}
          onChange={set('joinMode')}
          options={[
            { value: 'open', label: 'Join instantly' },
            { value: 'approval', label: 'Ask the host first' },
          ]}
        />
        <SelectField
          label="Maximum players"
          value={form.maxParticipants}
          onChange={set('maxParticipants')}
          options={[2, 3, 4, 5, 6, 8, 10, 12, 16, 24, 32, 64].map((value) => ({ value: String(value), label: String(value) }))}
        />
        {!session ? (
          <TextField
            label="Game identifier"
            name="gameIdentifier"
            value={form.gameIdentifier}
            onChange={set('gameIdentifier')}
            maxLength={LIMITS.gameIdentifierMax}
            error={errors.gameIdentifier}
            hint="Optional, for future game integrations."
          />
        ) : (
          <div />
        )}
        <TextArea label="Description" value={form.description} onChange={set('description')} maxLength={LIMITS.gameSessionDescriptionMax} containerClassName="sm:col-span-2" placeholder="Skill level, mode, voice tools you use…" />
        <TextField
          label="Connection details"
          name="connectionInfo"
          value={form.connectionInfo}
          onChange={set('connectionInfo')}
          maxLength={LIMITS.gameSessionConnectionInfoMax}
          hint="Optional, e.g. an in-game lobby code. Only participants can see it."
          containerClassName="sm:col-span-2"
        />
        <div className="sm:col-span-2">
          <Notice>This is a coordination lobby. AKENO NETWORK does not start games or connect consoles; use the connection details to meet in-game.</Notice>
        </div>
        <div className="sm:col-span-2">
          <InlineError message={mutation.isError ? errorMessage(mutation.error) : null} />
        </div>
      </form>
    </Modal>
  );
}
