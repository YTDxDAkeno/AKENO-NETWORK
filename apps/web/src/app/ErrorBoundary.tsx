import { CircleAlert } from 'lucide-react';
import { useRouteError } from 'react-router';
import { Button } from '../components/Button';

/** Last-resort error screen for unexpected rendering errors. */
export function RouteErrorBoundary() {
  const error = useRouteError();
  if (import.meta.env.DEV) console.error(error);
  return (
    <div className="flex min-h-dvh items-center justify-center p-6">
      <div className="card max-w-md p-8 text-center" role="alert">
        <CircleAlert aria-hidden className="mx-auto mb-4 size-10 text-danger" />
        <h1 className="text-xl font-semibold">Something went wrong</h1>
        <p className="mt-2 text-sm text-muted">An unexpected error occurred while showing this page. Reloading usually fixes it.</p>
        <Button className="mt-6" variant="primary" onClick={() => window.location.reload()}>
          Reload AKENO NETWORK
        </Button>
      </div>
    </div>
  );
}
