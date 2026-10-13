import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { MemoryRouter } from 'react-router';
import { describe, expect, it, vi } from 'vitest';
import { ConfirmProvider } from '../../src/components/ConfirmDialog';
import { ToastProvider } from '../../src/components/Toast';
import { AuthProvider } from '../../src/features/auth/AuthProvider';
import { LoginPage } from '../../src/features/auth/LoginPage';

function renderLogin() {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  return render(
    <QueryClientProvider client={client}>
      <ToastProvider>
        <ConfirmProvider>
          <AuthProvider>
            <MemoryRouter initialEntries={['/login']}>
              <LoginPage />
            </MemoryRouter>
          </AuthProvider>
        </ConfirmProvider>
      </ToastProvider>
    </QueryClientProvider>,
  );
}

function json(status: number, body: unknown) {
  return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
}

describe('LoginPage', () => {
  it('validates empty fields before calling the API', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue(json(401, { error: { code: 'UNAUTHENTICATED', message: 'Sign in.' } }));
    renderLogin();
    await userEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(await screen.findByText('Enter your Online ID.')).toBeInTheDocument();
    expect(screen.getByText('Enter your password.')).toBeInTheDocument();
    // Only the initial session probe was sent.
    expect(fetchMock.mock.calls.every(([url]) => String(url).endsWith('/auth/session'))).toBe(true);
  });

  it('shows the server error for wrong credentials', async () => {
    vi.spyOn(globalThis, 'fetch').mockImplementation(async (input) => {
      if (String(input).endsWith('/auth/session')) return json(401, { error: { code: 'UNAUTHENTICATED', message: 'Sign in.' } });
      return json(401, { error: { code: 'INVALID_CREDENTIALS', message: 'The Online ID or password is incorrect.' } });
    });
    renderLogin();
    await userEvent.type(screen.getByLabelText('Online ID'), 'alice');
    await userEvent.type(screen.getByLabelText('Password'), 'wrong-password-123');
    await userEvent.click(screen.getByRole('button', { name: 'Sign in' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('The Online ID or password is incorrect.');
  });
});
