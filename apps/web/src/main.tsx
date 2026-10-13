import '@fontsource-variable/inter';
import '@fontsource-variable/space-grotesk';
import './styles/index.css';
import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './app/App';
import { applyTheme, getTheme } from './lib/preferences';

applyTheme(getTheme());

const root = document.getElementById('root');
if (!root) throw new Error('Root element #root is missing');

createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
