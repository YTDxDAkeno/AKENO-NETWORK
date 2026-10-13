import { useQueryClient } from '@tanstack/react-query';
import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import type { AuthSessionResponse, CurrentUser, LoginInput, RegisterInput } from '@akeno/shared';
import { api, onUnauthorized, setCsrfRefresher, setCsrfToken } from '../../lib/api';

type AuthState =
  | { status: 'loading' }
  | { status: 'anonymous'; reason: string | null }
  | { status: 'authenticated'; user: CurrentUser; sessionId: string };

interface AuthContextValue {
  state: AuthState;
  user: CurrentUser | null;
  login: (input: LoginInput) => Promise<CurrentUser>;
  register: (input: RegisterInput) => Promise<CurrentUser>;
  logout: () => Promise<void>;
  /** Called when the server reports that the session ended (revoked, expired, deleted). */
  signedOut: (reason: string) => void;
  setUser: (user: CurrentUser) => void;
  patchUser: (patch: Partial<CurrentUser>) => void;
  refreshUser: () => Promise<void>;
}

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient();
  const [state, setState] = useState<AuthState>({ status: 'loading' });

  const adopt = useCallback(
    (response: AuthSessionResponse) => {
      setCsrfToken(response.csrfToken);
      setState({ status: 'authenticated', user: response.user, sessionId: response.session.id });
      return response.user;
    },
    [],
  );

  const signedOut = useCallback(
    (reason: string) => {
      setCsrfToken(null);
      queryClient.clear();
      setState((current) => (current.status === 'anonymous' ? current : { status: 'anonymous', reason }));
    },
    [queryClient],
  );

  useEffect(() => {
    let cancelled = false;
    api
      .get<AuthSessionResponse>('/auth/session', { allowUnauthenticated: true })
      .then((response) => {
        if (!cancelled) adopt(response);
      })
      .catch(() => {
        if (!cancelled) setState({ status: 'anonymous', reason: null });
      });
    return () => {
      cancelled = true;
    };
  }, [adopt]);

  useEffect(() => {
    onUnauthorized(() => signedOut('Your session has ended. Please sign in again.'));
    setCsrfRefresher(async () => {
      try {
        const response = await api.get<AuthSessionResponse>('/auth/session', { allowUnauthenticated: true });
        return response.csrfToken;
      } catch {
        return null;
      }
    });
    return () => {
      onUnauthorized(null);
      setCsrfRefresher(null);
    };
  }, [signedOut]);

  const value = useMemo<AuthContextValue>(
    () => ({
      state,
      user: state.status === 'authenticated' ? state.user : null,
      login: async (input) => {
        queryClient.clear();
        return adopt(await api.post<AuthSessionResponse>('/auth/login', input, { allowUnauthenticated: true }));
      },
      register: async (input) => {
        queryClient.clear();
        return adopt(await api.post<AuthSessionResponse>('/auth/register', input, { allowUnauthenticated: true }));
      },
      logout: async () => {
        try {
          await api.post('/auth/logout');
        } finally {
          setCsrfToken(null);
          queryClient.clear();
          setState({ status: 'anonymous', reason: null });
        }
      },
      signedOut,
      setUser: (user) =>
        setState((current) => (current.status === 'authenticated' ? { ...current, user } : current)),
      patchUser: (patch) =>
        setState((current) =>
          current.status === 'authenticated' ? { ...current, user: { ...current.user, ...patch } } : current,
        ),
      refreshUser: async () => {
        const user = await api.get<CurrentUser>('/users/me');
        setState((current) => (current.status === 'authenticated' ? { ...current, user } : current));
      },
    }),
    [state, adopt, signedOut, queryClient],
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthContextValue {
  const context = useContext(AuthContext);
  if (!context) throw new Error('useAuth must be used inside AuthProvider');
  return context;
}

/** The signed-in user; only use below RequireAuth. */
export function useCurrentUser(): CurrentUser {
  const { user } = useAuth();
  if (!user) throw new Error('useCurrentUser requires an authenticated session');
  return user;
}
