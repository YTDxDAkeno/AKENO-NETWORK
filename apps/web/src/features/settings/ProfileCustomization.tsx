import { useMutation, useQueryClient } from '@tanstack/react-query';
import { Check, ImagePlus, RotateCcw, Save, Trash2 } from 'lucide-react';
import { useState } from 'react';
import {
  AVATAR_PRESETS,
  BADGES,
  FEATURED_BADGES_MAX,
  HEX_COLOR_PATTERN,
  PROFILE_THEMES,
  badgeDefinition,
  themeAccent,
  type BadgeId,
  type CurrentUser,
  type ProfileAppearance,
  type ProfileThemeId,
} from '@akeno/shared';
import { Avatar } from '../../components/Avatar';
import { BadgeIcon } from '../../components/BadgeIcon';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { cn } from '../../components/cn';
import { useConfirm } from '../../components/ConfirmDialog';
import { Switch } from '../../components/Field';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';
import { themeStyle, useColorMode } from '../../lib/theme';
import { useAuth, useCurrentUser } from '../auth/AuthProvider';
import { ImageCropDialog, type CropKind } from '../profile/ImageCropDialog';
import { ProfileBanner, ProfileHeader } from '../profile/ProfileHeader';

function useProfileSaved() {
  const { setUser } = useAuth();
  const queryClient = useQueryClient();
  return (updated: CurrentUser) => {
    setUser(updated);
    void queryClient.invalidateQueries({ queryKey: queryKeys.profiles });
    void queryClient.invalidateQueries({ queryKey: queryKeys.friends });
  };
}

/** Profile picture, banner and preset avatars. Changes apply immediately. */
export function ProfileMediaCard() {
  const user = useCurrentUser();
  const toast = useToast();
  const confirm = useConfirm();
  const saved = useProfileSaved();
  const [cropping, setCropping] = useState<CropKind | null>(null);
  const [uploadError, setUploadError] = useState<string | null>(null);

  const upload = useMutation({
    mutationFn: ({ kind, file }: { kind: CropKind; file: Blob }) => api.upload<CurrentUser>(`/profile/${kind}`, file),
    onSuccess: (updated, { kind }) => {
      saved(updated);
      setCropping(null);
      toast.success(kind === 'avatar' ? 'Profile picture updated.' : 'Banner updated.');
    },
    onError: (error) => setUploadError(errorMessage(error)),
  });
  const remove = useMutation({
    mutationFn: (kind: CropKind) => api.delete<CurrentUser>(`/profile/${kind}`),
    onSuccess: (updated, kind) => {
      saved(updated);
      toast.success(kind === 'avatar' ? 'Profile picture removed. Your preset avatar is shown again.' : 'Banner removed.');
    },
    onError: (error) => toast.error('Could not remove the image', errorMessage(error)),
  });
  const preset = useMutation({
    mutationFn: async (avatar: string) => {
      const updated = await api.patch<CurrentUser>('/profile', { avatar });
      return updated.profile.avatarImage ? api.delete<CurrentUser>('/profile/avatar') : updated;
    },
    onSuccess: (updated) => {
      saved(updated);
      toast.success('Preset avatar selected.');
    },
    onError: (error) => toast.error('Could not change the avatar', errorMessage(error)),
  });

  const open = (kind: CropKind) => {
    setUploadError(null);
    setCropping(kind);
  };

  return (
    <Card>
      <CardHeader title="Picture and banner" description="Upload your own images from your computer or phone, or use one of the preset avatars." />
      <div className="grid gap-6 p-5 md:grid-cols-2">
        <div className="flex items-center gap-4">
          <Avatar avatar={user.profile.avatar} image={user.profile.avatarImage} name={user.profile.displayName} size="xl" />
          <div className="flex flex-col gap-2">
            <p className="text-sm font-medium">Profile picture</p>
            <div className="flex flex-wrap gap-2">
              <Button size="sm" variant="primary" icon={<ImagePlus aria-hidden className="size-4" />} onClick={() => open('avatar')}>
                {user.profile.avatarImage ? 'Replace' : 'Upload'}
              </Button>
              {user.profile.avatarImage ? (
                <Button
                  size="sm"
                  variant="ghost"
                  icon={<Trash2 aria-hidden className="size-4" />}
                  loading={remove.isPending && remove.variables === 'avatar'}
                  onClick={async () => {
                    if (await confirm({ title: 'Remove your profile picture?', message: 'Your preset avatar is shown instead.', confirmLabel: 'Remove' })) {
                      remove.mutate('avatar');
                    }
                  }}
                >
                  Remove
                </Button>
              ) : null}
            </div>
          </div>
        </div>
        <div className="flex flex-col gap-2">
          <p className="text-sm font-medium">Banner</p>
          <div
            className="aspect-[3/1] w-full overflow-hidden rounded-xl border border-line bg-surface-2"
            style={themeStyle(user.profile.appearance, 'dark')}
          >
            <ProfileBanner appearance={user.profile.appearance} className="h-full" />
          </div>
          <div className="flex flex-wrap gap-2">
            <Button size="sm" variant="secondary" icon={<ImagePlus aria-hidden className="size-4" />} onClick={() => open('banner')}>
              {user.profile.appearance.bannerImage ? 'Replace banner' : 'Upload banner'}
            </Button>
            {user.profile.appearance.bannerImage ? (
              <Button size="sm" variant="ghost" icon={<Trash2 aria-hidden className="size-4" />} loading={remove.isPending && remove.variables === 'banner'} onClick={() => remove.mutate('banner')}>
                Remove
              </Button>
            ) : null}
          </div>
        </div>
      </div>
      <fieldset className="border-t border-line p-5">
        <legend className="mb-1 text-sm font-medium">Preset avatars</legend>
        <p className="mb-3 text-xs text-muted">
          {user.profile.avatarImage
            ? 'Choosing a preset removes your uploaded picture.'
            : 'Shown wherever your picture appears. You can upload your own picture at any time.'}
        </p>
        <div className="grid grid-cols-4 gap-3 sm:grid-cols-8" role="radiogroup" aria-label="Preset avatar">
          {AVATAR_PRESETS.map((option) => {
            const selected = !user.profile.avatarImage && option.id === user.profile.avatar;
            return (
              <button
                key={option.id}
                type="button"
                role="radio"
                aria-checked={selected}
                aria-label={option.label}
                disabled={preset.isPending}
                onClick={() => preset.mutate(option.id)}
                className={cn(
                  'flex flex-col items-center gap-1.5 rounded-xl border p-2 text-xs transition-colors',
                  selected ? 'border-accent bg-accent-soft text-ink' : 'border-transparent text-muted hover:bg-surface-2',
                )}
              >
                <Avatar avatar={option.id} name={option.label} size="md" />
                {option.label}
              </button>
            );
          })}
        </div>
      </fieldset>
      {cropping ? (
        <ImageCropDialog
          kind={cropping}
          open
          onClose={() => setCropping(null)}
          saving={upload.isPending}
          error={uploadError}
          onSave={(file) => {
            setUploadError(null);
            upload.mutate({ kind: cropping, file });
          }}
        />
      ) : null}
    </Card>
  );
}

/** Theme, accent colour, banner overlay and featured badges, with a live preview. */
export function AppearanceCard() {
  const user = useCurrentUser();
  const toast = useToast();
  const saved = useProfileSaved();
  const mode = useColorMode();
  const initial = user.profile.appearance;
  const [theme, setTheme] = useState<ProfileThemeId>(initial.theme);
  const [customAccent, setCustomAccent] = useState(initial.accentColor ?? themeAccent(initial.theme, null));
  const [overlay, setOverlay] = useState(initial.bannerOverlay);
  const [featured, setFeatured] = useState<BadgeId[]>(user.profile.featuredBadges);
  const accentValid = HEX_COLOR_PATTERN.test(customAccent.toLowerCase());

  const draft: ProfileAppearance = {
    theme,
    accentColor: theme === 'custom' && accentValid ? customAccent.toLowerCase() : null,
    bannerImage: initial.bannerImage,
    bannerOverlay: overlay,
  };
  const earned = BADGES.filter((badge) => user.badges.some((owned) => owned.id === badge.id));

  const save = useMutation({
    mutationFn: () =>
      api.patch<CurrentUser>('/profile/appearance', {
        theme,
        ...(theme === 'custom' ? { accentColor: customAccent.toLowerCase() } : {}),
        bannerOverlay: overlay,
        featuredBadges: featured,
      }),
    onSuccess: (updated) => {
      saved(updated);
      toast.success('Appearance saved.');
    },
    onError: (error) => toast.error('Could not save your appearance', errorMessage(error)),
  });

  const reset = () => {
    setTheme('default');
    setCustomAccent(themeAccent('default', null));
    setOverlay(true);
  };

  const toggleBadge = (id: BadgeId) =>
    setFeatured((current) => (current.includes(id) ? current.filter((entry) => entry !== id) : current.length >= FEATURED_BADGES_MAX ? current : [...current, id]));

  return (
    <Card>
      <CardHeader
        title="Appearance"
        description="Your theme colours your profile for everyone who visits it, and your own AKENO app."
        action={
          <Button size="sm" variant="ghost" icon={<RotateCcw aria-hidden className="size-4" />} onClick={reset}>
            Reset to default
          </Button>
        }
      />
      <div className="grid gap-6 p-5 lg:grid-cols-[minmax(0,1fr)_minmax(0,1fr)]">
        <div className="flex flex-col gap-5">
          <fieldset>
            <legend className="mb-2 text-sm font-medium">Theme</legend>
            <div className="grid grid-cols-2 gap-2 sm:grid-cols-4 lg:grid-cols-2" role="radiogroup" aria-label="Theme">
              {PROFILE_THEMES.map((option) => {
                const selected = option.id === theme;
                const swatch = option.id === 'custom' ? (accentValid ? customAccent : option.accent) : option.accent;
                return (
                  <button
                    key={option.id}
                    type="button"
                    role="radio"
                    aria-checked={selected}
                    onClick={() => setTheme(option.id)}
                    data-testid={`theme-${option.id}`}
                    className={cn(
                      'flex items-center gap-2 rounded-xl border px-2.5 py-2 text-left text-xs font-medium transition-colors',
                      selected ? 'border-accent bg-accent-soft text-ink' : 'border-line text-muted hover:bg-surface-2',
                    )}
                  >
                    <span className="relative grid size-6 shrink-0 place-items-center rounded-full" style={{ backgroundColor: option.tint, boxShadow: `inset 0 0 0 6px ${swatch}` }}>
                      {selected ? <Check aria-hidden className="size-3 text-white" /> : null}
                    </span>
                    <span className="truncate">{option.label}</span>
                  </button>
                );
              })}
            </div>
          </fieldset>
          {theme === 'custom' ? (
            <div className="flex items-end gap-3">
              <label className="flex flex-col gap-1.5 text-sm font-medium">
                Accent colour
                <input
                  type="color"
                  value={accentValid ? customAccent : '#4f7cff'}
                  onChange={(event) => setCustomAccent(event.target.value)}
                  className="h-11 w-16 cursor-pointer rounded-xl border border-line-strong bg-surface-2 p-1"
                />
              </label>
              <label className="flex flex-1 flex-col gap-1.5 text-sm font-medium">
                Hex value
                <input
                  value={customAccent}
                  maxLength={7}
                  onChange={(event) => setCustomAccent(event.target.value.trim())}
                  aria-invalid={!accentValid}
                  className="h-11 rounded-xl border border-line-strong bg-surface-2 px-3.5 font-mono text-sm focus:border-accent focus:outline-none"
                />
              </label>
            </div>
          ) : null}
          {theme === 'custom' && !accentValid ? <p className="text-xs text-danger">Use a colour such as #4f7cff.</p> : null}
          {theme === 'custom' ? (
            <p className="text-xs text-muted">Very dark or very light colours are adjusted automatically so text and buttons stay readable.</p>
          ) : null}
          <Switch
            label="Darken banner"
            description="A soft dark overlay on your banner. Your name and buttons are never placed on the banner itself."
            checked={overlay}
            onChange={setOverlay}
          />
          <fieldset>
            <legend className="mb-1 text-sm font-medium">Featured badges</legend>
            <p className="mb-2 text-xs text-muted">
              Choose up to {FEATURED_BADGES_MAX} badges to show by name on your profile. Badges are granted by the administrators of this server.
            </p>
            {earned.length === 0 ? (
              <p className="rounded-xl bg-surface-2 px-3 py-2.5 text-sm text-muted">You have not earned any badges yet.</p>
            ) : (
              <ul className="flex flex-col gap-1.5">
                {earned.map((badge) => {
                  const checked = featured.includes(badge.id);
                  return (
                    <li key={badge.id}>
                      <label className={cn('flex cursor-pointer items-center gap-3 rounded-xl border px-3 py-2', checked ? 'border-accent bg-accent-soft' : 'border-line hover:bg-surface-2')}>
                        <input
                          type="checkbox"
                          className="size-4 accent-[var(--akeno-accent)]"
                          checked={checked}
                          disabled={!checked && featured.length >= FEATURED_BADGES_MAX}
                          onChange={() => toggleBadge(badge.id)}
                        />
                        <BadgeIcon id={badge.id} size="md" />
                        <span className="min-w-0">
                          <span className="block text-sm font-medium text-ink">{badge.name}</span>
                          <span className="block truncate text-xs text-muted">{badgeDefinition(badge.id)?.description}</span>
                        </span>
                      </label>
                    </li>
                  );
                })}
              </ul>
            )}
          </fieldset>
        </div>
        <div>
          <p className="mb-2 text-sm font-medium">Preview</p>
          <div className="card-tinted overflow-hidden" style={themeStyle(draft, mode)} data-testid="appearance-preview">
            <ProfileHeader
              compact
              identity={{ ...user.profile, onlineId: user.onlineId }}
              appearance={draft}
              status={user.presence === 'offline' ? 'online' : user.presence}
              featuredBadges={featured}
              actions={
                <Button size="sm" variant="primary" tabIndex={-1}>
                  Message
                </Button>
              }
              meta={
                <div className="flex items-center gap-2 text-sm">
                  <span className="text-accent">Accent</span>
                  <span className="text-muted">· links, highlights and buttons</span>
                </div>
              }
            />
          </div>
        </div>
      </div>
      <div className="flex justify-end border-t border-line px-5 py-3.5">
        <Button variant="primary" loading={save.isPending} disabled={theme === 'custom' && !accentValid} icon={<Save aria-hidden className="size-4" />} onClick={() => save.mutate()}>
          Save appearance
        </Button>
      </div>
    </Card>
  );
}
