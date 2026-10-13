import { LogIn } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { Link, useLocation } from 'react-router';
import { AuthLayout } from '../../app/layouts/AuthLayout';
import { Button } from '../../components/Button';
import { TextField } from '../../components/Field';
import { InlineError, Notice } from '../../components/States';
import { useDocumentTitle } from '../../hooks/useDocumentTitle';
import { ApiError, errorMessage } from '../../lib/api';
import { useAuth } from './AuthProvider';

export function LoginPage() {
  useDocumentTitle('Sign in');
  const { login } = useAuth();
  const location = useLocation();
  const reason = (location.state as { reason?: string | null } | null)?.reason ?? null;
  const [onlineId, setOnlineId] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [fieldErrors, setFieldErrors] = useState<Record<string, string>>({});
  const [submitting, setSubmitting] = useState(false);

  const onSubmit = async (event: FormEvent) => {
    event.preventDefault();
    const errors: Record<string, string> = {};
    if (!onlineId.trim()) errors.onlineId = 'Enter your Online ID.';
    if (!password) errors.password = 'Enter your password.';
    setFieldErrors(errors);
    if (Object.keys(errors).length > 0) return;
    setSubmitting(true);
    setError(null);
    try {
      await login({ onlineId: onlineId.trim(), password });
    } catch (caught) {
      setError(errorMessage(caught));
      if (caught instanceof ApiError) setFieldErrors(caught.fields);
      setSubmitting(false);
    }
  };

  return (
    <AuthLayout>
      <div className="card p-6 sm:p-8">
        <h1 className="text-2xl font-semibold">Welcome back</h1>
        <p className="mt-1 text-sm text-muted">Sign in to AKENO NETWORK with your Online ID.</p>
        {reason ? (
          <div className="mt-5">
            <Notice>{reason}</Notice>
          </div>
        ) : null}
        <form className="mt-6 flex flex-col gap-4" onSubmit={onSubmit} noValidate>
          <TextField
            label="Online ID"
            name="onlineId"
            autoComplete="username"
            autoCapitalize="none"
            spellCheck={false}
            value={onlineId}
            onChange={(event) => setOnlineId(event.target.value)}
            error={fieldErrors.onlineId}
            autoFocus
          />
          <TextField
            label="Password"
            name="password"
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            error={fieldErrors.password}
          />
          <InlineError message={error} />
          <Button type="submit" variant="primary" size="lg" loading={submitting} icon={<LogIn aria-hidden className="size-4" />}>
            Sign in
          </Button>
        </form>
        <p className="mt-6 text-center text-sm text-muted">
          New here?{' '}
          <Link to="/register" className="font-medium text-accent hover:underline">
            Create an account with an invitation
          </Link>
        </p>
      </div>
    </AuthLayout>
  );
}
