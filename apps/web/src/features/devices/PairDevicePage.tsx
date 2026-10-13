import { useMutation, useQueryClient } from '@tanstack/react-query';
import { CheckCircle2, Gamepad2, Link2, ShieldAlert, XCircle } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { Link, useSearchParams } from 'react-router';
import { DEVICE_SCOPE_LABELS, type DevicePairingPreviewDto, type DeviceScope } from '@akeno/shared';
import { Button } from '../../components/Button';
import { Card, CardHeader } from '../../components/Card';
import { TextField } from '../../components/Field';
import { PageHeader } from '../../components/PageHeader';
import { RelativeTime } from '../../components/RelativeTime';
import { Notice } from '../../components/States';
import { ApiError, api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';

type Step =
  | { kind: 'enter' }
  | { kind: 'review'; code: string; pairing: DevicePairingPreviewDto; scopes: Set<DeviceScope> }
  | { kind: 'approved'; deviceName: string }
  | { kind: 'denied' };

/** Formats free typing as XXXX-XXXX while ignoring separators and case. */
function formatCode(value: string): string {
  const compact = value.replace(/[^A-Za-z0-9]/g, '').toUpperCase().slice(0, 8);
  return compact.length > 4 ? `${compact.slice(0, 4)}-${compact.slice(4)}` : compact;
}

/**
 * Approves a device pairing request (for example from the AKENO console client). The device
 * shows a short code; the signed-in user enters it here and chooses which permissions to grant.
 */
export function PairDevicePage() {
  const [params] = useSearchParams();
  const queryClient = useQueryClient();
  const [code, setCode] = useState(() => formatCode(params.get('code') ?? ''));
  const [step, setStep] = useState<Step>({ kind: 'enter' });
  const [error, setError] = useState<string | null>(null);

  const lookup = useMutation({
    mutationFn: (userCode: string) => api.post<{ pairing: DevicePairingPreviewDto }>('/devices/pairings/lookup', { userCode }),
    onSuccess: (result, userCode) => {
      setError(null);
      setStep({ kind: 'review', code: userCode, pairing: result.pairing, scopes: new Set(result.pairing.requestedScopes) });
    },
    onError: (failure) => setError(failure instanceof ApiError && failure.fields.userCode ? failure.fields.userCode : errorMessage(failure)),
  });

  const approve = useMutation({
    mutationFn: (review: Extract<Step, { kind: 'review' }>) =>
      api.post('/devices/pairings/approve', { userCode: review.code, scopes: [...review.scopes] }),
    onSuccess: (_result, review) => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.devices });
      setStep({ kind: 'approved', deviceName: review.pairing.deviceName });
    },
    onError: (failure) => setError(errorMessage(failure)),
  });

  const deny = useMutation({
    mutationFn: (userCode: string) => api.post('/devices/pairings/deny', { userCode }),
    onSuccess: () => setStep({ kind: 'denied' }),
    onError: (failure) => setError(errorMessage(failure)),
  });

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (code.replace('-', '').length !== 8) {
      setError('Enter the 8-character code shown on your device.');
      return;
    }
    lookup.mutate(code);
  };

  const toggleScope = (scope: DeviceScope) => {
    if (step.kind !== 'review') return;
    const scopes = new Set(step.scopes);
    if (scopes.has(scope)) scopes.delete(scope);
    else scopes.add(scope);
    setStep({ ...step, scopes });
  };

  return (
    <div className="mx-auto max-w-2xl">
      <PageHeader
        title="Pair a device"
        icon={<Link2 aria-hidden />}
        description="Sign in a console or companion app with the code it shows, without typing your password on it."
      />

      {step.kind === 'enter' ? (
        <Card>
          <form className="flex flex-col gap-4 p-5" onSubmit={submit} noValidate>
            <Notice icon={<ShieldAlert aria-hidden className="size-4" />} tone="warning">
              Only enter a code that is shown on a device you own and are using right now. Never enter a code that someone
              sent you: it would give them access to your account.
            </Notice>
            <TextField
              label="Pairing code"
              value={code}
              onChange={(event) => setCode(formatCode(event.target.value))}
              placeholder="XXXX-XXXX"
              autoComplete="off"
              autoCapitalize="characters"
              spellCheck={false}
              inputMode="text"
              className="font-mono text-lg tracking-widest"
              error={error ?? undefined}
              hint="The code expires 10 minutes after the device shows it."
            />
            <div className="flex justify-end">
              <Button type="submit" variant="primary" loading={lookup.isPending}>
                Continue
              </Button>
            </div>
          </form>
        </Card>
      ) : null}

      {step.kind === 'review' ? (
        <Card>
          <CardHeader
            title={step.pairing.deviceName}
            icon={<Gamepad2 aria-hidden className="size-5" />}
            description={[step.pairing.platform, step.pairing.clientVersion ? `version ${step.pairing.clientVersion}` : null]
              .filter(Boolean)
              .join(' · ')}
          />
          <div className="flex flex-col gap-4 p-5">
            <p className="text-sm text-muted">
              Requested <RelativeTime iso={step.pairing.createdAt} />
              {step.pairing.requestIp ? ` from ${step.pairing.requestIp}` : ''}. Choose what this device may do:
            </p>
            <fieldset className="flex flex-col gap-2">
              <legend className="sr-only">Permissions</legend>
              {step.pairing.requestedScopes.map((scope) => (
                <label key={scope} className="flex items-center gap-3 rounded-xl border border-line px-3 py-2 text-sm">
                  <input
                    type="checkbox"
                    className="size-4 accent-accent"
                    checked={step.scopes.has(scope)}
                    onChange={() => toggleScope(scope)}
                  />
                  <span>{DEVICE_SCOPE_LABELS[scope]}</span>
                </label>
              ))}
            </fieldset>
            <p className="text-xs text-muted">
              Paired devices can never change your password, privacy settings or blocked users, delete your account, or use
              administration tools. You can unpair the device at any time in Settings → Account &amp; security.
            </p>
            {error ? (
              <p className="text-sm text-danger" role="alert">
                {error}
              </p>
            ) : null}
            <div className="flex flex-wrap justify-end gap-2">
              <Button variant="ghost" loading={deny.isPending} onClick={() => deny.mutate(step.code)}>
                Deny
              </Button>
              <Button
                variant="primary"
                loading={approve.isPending}
                disabled={step.scopes.size === 0}
                onClick={() => approve.mutate(step)}
              >
                Approve device
              </Button>
            </div>
          </div>
        </Card>
      ) : null}

      {step.kind === 'approved' ? (
        <Card>
          <div className="flex flex-col items-center gap-3 p-8 text-center">
            <CheckCircle2 aria-hidden className="size-10 text-success" />
            <h2 className="text-lg font-semibold">{step.deviceName} is paired</h2>
            <p className="text-sm text-muted">Return to the device. It finishes signing in within a few seconds.</p>
            <Link to="/settings?tab=security" className="text-sm text-accent underline-offset-4 hover:underline">
              Manage paired devices
            </Link>
          </div>
        </Card>
      ) : null}

      {step.kind === 'denied' ? (
        <Card>
          <div className="flex flex-col items-center gap-3 p-8 text-center">
            <XCircle aria-hidden className="size-10 text-danger" />
            <h2 className="text-lg font-semibold">Pairing denied</h2>
            <p className="text-sm text-muted">The device was not given access to your account.</p>
          </div>
        </Card>
      ) : null}
    </div>
  );
}
