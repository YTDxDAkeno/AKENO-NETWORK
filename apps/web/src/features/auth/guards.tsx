import type { ReactNode } from 'react';
import { Navigate, useLocation } from 'react-router';
import { PageLoader } from '../../components/Spinner';
import { useAuth } from './AuthProvider';

export function RequireAuth({ children }: { children: ReactNode }) {
  const { state } = useAuth();
  const location = useLocation();
  if (state.status === 'loading') return <PageLoader label="Connecting to AKENO NETWORK…" />;
  if (state.status === 'anonymous') {
    return <Navigate to="/login" replace state={{ from: location.pathname + location.search, reason: state.reason }} />;
  }
  return <>{children}</>;
}

export function RequireAdmin({ children }: { children: ReactNode }) {
  const { user } = useAuth();
  if (user?.role !== 'admin') return <Navigate to="/" replace />;
  return <>{children}</>;
}

export function RedirectIfAuthenticated({ children }: { children: ReactNode }) {
  const { state } = useAuth();
  const location = useLocation();
  if (state.status === 'loading') return <PageLoader />;
  if (state.status === 'authenticated') {
    const from = (location.state as { from?: string } | null)?.from;
    return <Navigate to={from && from !== '/login' ? from : '/'} replace />;
  }
  return <>{children}</>;
}
