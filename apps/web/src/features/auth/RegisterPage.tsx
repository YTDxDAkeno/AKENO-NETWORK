import { useQuery } from '@tanstack/react-query';
import { UserPlus } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { Link } from 'react-router';
import { LIMITS, ONLINE_ID_PATTERN, checkPasswordQuality, type AuthConfigResponse } from '@akeno/shared';
import { AuthLayout } from '../../app/layouts/AuthLayout';
import { Button } from '../../components/Button';
import { TextField } from '../../components/Field';
import { InlineError } from '../../components/States';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError, api, errorMessage } from '../../lib/api';
import { useAuth } from './AuthProvider';

export function RegisterPage() {
  useDocumentTitle('Create account');
  const { register } = useAuth();
  const config = useQuery({
    queryKey: ['auth', 'config'],
    queryFn: () => api.get<AuthConfigResponse>('/auth/config', { allowUnauthenticated: true }),
    staleTime: Infinity,
  });
  const inviteOnly = config.data?.registrationMode !== 'open';
  const [form, setForm] = useState({ invitationCode: '', onlineId: '', displayName: '', password: '', confirm: '' });
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const update = (key: keyof typeof form) => (event: React.ChangeEvent<HTMLInputElement>) =>
    setForm((current) => ({ ...current, [key]: event.target.value }));

  const validate = (): Record<string, string> => {
    const next: Record<string, string> = {};
    if (inviteOnly && !form.invitationCode.trim()) next.invitationCode = 'Enter the invitation code you received.';
    if (!ONLINE_ID_PATTERN.test(form.onlineId.trim())) {
      next.onlineId = `Use ${LIMITS.onlineIdMin}-${LIMITS.onlineIdMax} letters, numbers, underscores or hyphens, starting with a letter.`;
    }
    if (form.password.length < LIMITS.passwordMin) next.password = `Use at least ${LIMITS.passwordMin} characters.`;
    else {
      const quality = checkPasswordQuality(form.password, form.onlineId.trim());
      if (quality) next.password = quality;
    }
    if (form.confirm !== form.password) next.confirm = 'The passwords do not match.';
    return next;
  };

  const onSubmit = async (event: FormEvent) => {
    event.preventDefault();
    const next = validate();
    setErrors(next);
    if (Object.keys(next).length > 0) return;
    setSubmitting(true);
    setError(null);
    try {
      await register({
        onlineId: form.onlineId.trim(),
        password: form.password,
        ...(form.displayName.trim() ? { displayName: form.displayName.trim() } : {}),
        ...(form.invitationCode.trim() ? { invitationCode: form.invitationCode.trim() } : {}),
      });
    } catch (caught) {
      setError(errorMessage(caught));
      if (caught instanceof ApiError) setErrors(caught.fields);
      setSubmitting(false);
    }
  };

  return (
    <AuthLayout>
      <div className="card p-6 sm:p-8">
        <h1 className="text-2xl font-semibold">Join the network</h1>
        <p className="mt-1 text-sm text-muted">
          {inviteOnly
            ? 'This is a private network. You need an invitation code from an administrator.'
            : 'Create your AKENO NETWORK account.'}
        </p>
        <form className="mt-6 flex flex-col gap-4" onSubmit={onSubmit} noValidate>
          {inviteOnly ? (
            <TextField
              label="Invitation code"
              name="invitationCode"
              placeholder="XXXX-XXXX-XXXX-XXXX"
              autoComplete="off"
              autoCapitalize="characters"
              spellCheck={false}
              value={form.invitationCode}
              onChange={update('invitationCode')}
              error={errors.invitationCode}
              required
            />
          ) : null}
          <TextField
            label="Online ID"
            name="onlineId"
            autoComplete="username"
            autoCapitalize="none"
            spellCheck={false}
            value={form.onlineId}
            onChange={update('onlineId')}
            error={errors.onlineId}
            hint="Your unique name on the network. Friends use it to find you."
            maxLength={LIMITS.onlineIdMax}
            required
          />
          <TextField
            label="Display name"
            name="displayName"
            autoComplete="nickname"
            value={form.displayName}
            onChange={update('displayName')}
            error={errors.displayName}
            hint="Optional. You can change it any time."
            maxLength={LIMITS.displayNameMax}
          />
          <TextField
            label="Password"
            name="password"
            type="password"
            autoComplete="new-password"
            value={form.password}
            onChange={update('password')}
            error={errors.password}
            hint={`At least ${LIMITS.passwordMin} characters. A passphrase of several words works well.`}
            required
          />
          <TextField
            label="Confirm password"
            name="confirm"
            type="password"
            autoComplete="new-password"
            value={form.confirm}
            onChange={update('confirm')}
            error={errors.confirm}
            required
          />
          <InlineError message={error} />
          <Button type="submit" variant="primary" size="lg" loading={submitting} icon={<UserPlus aria-hidden className="size-4" />}>
            Create account
          </Button>
        </form>
        <p className="mt-6 text-center text-sm text-muted">
          Already have an account?{' '}
          <Link to="/login" className="font-medium text-accent hover:underline">
            Sign in
          </Link>
        </p>
      </div>
    </AuthLayout>
  );
}
