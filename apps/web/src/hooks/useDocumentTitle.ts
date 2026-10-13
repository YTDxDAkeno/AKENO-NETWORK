import { useEffect } from 'react';
import { PRODUCT_NAME } from '@akeno/shared';

export function useDocumentTitle(title: string): void {
  useEffect(() => {
    document.title = title ? `${title} · ${PRODUCT_NAME}` : PRODUCT_NAME;
  }, [title]);
}
