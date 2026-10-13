import { Compass } from 'lucide-react';
import { ButtonLink } from '../components/Button';
import { Card } from '../components/Card';
import { EmptyState } from '../components/States';
import { useDocumentTitle } from '../hooks/useDocumentTitle';

export function NotFoundPage() {
  useDocumentTitle('Page not found');
  return (
    <Card>
      <EmptyState
        icon={<Compass className="size-7" />}
        title="This page does not exist"
        description="The link may be broken or the page may have moved."
        action={<ButtonLink to="/" variant="primary">Go home</ButtonLink>}
      />
    </Card>
  );
}
