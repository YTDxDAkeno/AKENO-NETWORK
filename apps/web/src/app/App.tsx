import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { RouterProvider } from 'react-router/dom';
import { ConfirmProvider } from '../components/ConfirmDialog';
import { ToastProvider } from '../components/Toast';
import { AuthProvider } from '../features/auth/AuthProvider';
import { ApiError } from '../lib/api';
import { router } from './router';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 15_000,
      refetchOnWindowFocus: true,
      // Do not retry client errors such as 401/403/404; retry network and server errors twice.
      retry: (failureCount, error) => {
        if (error instanceof ApiError && error.status >= 400 && error.status < 500) return false;
        return failureCount < 2;
      },
    },
    mutations: { retry: false },
  },
});

export function App() {
  return (
    <QueryClientProvider client={queryClient}>
      <ToastProvider>
        <ConfirmProvider>
          <AuthProvider>
            <RouterProvider router={router} />
          </AuthProvider>
        </ConfirmProvider>
      </ToastProvider>
    </QueryClientProvider>
  );
}
