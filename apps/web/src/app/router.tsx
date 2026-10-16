import { createBrowserRouter, Outlet } from 'react-router';
import { AdminPage } from '../features/admin/AdminPage';
import { RedirectIfAuthenticated, RequireAdmin, RequireAuth } from '../features/auth/guards';
import { LoginPage } from '../features/auth/LoginPage';
import { RegisterPage } from '../features/auth/RegisterPage';
import { HomePage } from '../features/dashboard/HomePage';
import { PairDevicePage } from '../features/devices/PairDevicePage';
import { DiscoverPage } from '../features/discover/DiscoverPage';
import { FriendsPage } from '../features/friends/FriendsPage';
import { GroupDetailPage } from '../features/groups/GroupDetailPage';
import { GroupsPage } from '../features/groups/GroupsPage';
import { MessagesPage } from '../features/messages/MessagesPage';
import { PartiesPage } from '../features/parties/PartiesPage';
import { ProfilePage } from '../features/profile/ProfilePage';
import { SessionDetailPage } from '../features/sessions/SessionDetailPage';
import { SessionsPage } from '../features/sessions/SessionsPage';
import { SettingsPage } from '../features/settings/SettingsPage';
import { SharePlayProvider } from '../features/shareplay/SharePlayProvider';
import { VoiceProvider } from '../features/voice/VoiceProvider';
import { RouteErrorBoundary } from './ErrorBoundary';
import { AppLayout } from './layouts/AppLayout';
import { NotFoundPage } from './NotFoundPage';
import { RealtimeProvider } from './providers/RealtimeProvider';

function AuthenticatedShell() {
  return (
    <RequireAuth>
      <RealtimeProvider>
        <VoiceProvider>
          <SharePlayProvider>
            <AppLayout>
              <Outlet />
            </AppLayout>
          </SharePlayProvider>
        </VoiceProvider>
      </RealtimeProvider>
    </RequireAuth>
  );
}

export const router = createBrowserRouter([
  {
    errorElement: <RouteErrorBoundary />,
    children: [
      { path: '/login', element: <RedirectIfAuthenticated><LoginPage /></RedirectIfAuthenticated> },
      { path: '/register', element: <RedirectIfAuthenticated><RegisterPage /></RedirectIfAuthenticated> },
      {
        element: <AuthenticatedShell />,
        children: [
          { index: true, element: <HomePage /> },
          { path: 'friends', element: <FriendsPage /> },
          { path: 'messages', element: <MessagesPage /> },
          { path: 'messages/:conversationId', element: <MessagesPage /> },
          { path: 'groups', element: <GroupsPage /> },
          { path: 'groups/:groupId', element: <GroupDetailPage /> },
          { path: 'parties', element: <PartiesPage /> },
          { path: 'sessions', element: <SessionsPage /> },
          { path: 'sessions/:sessionId', element: <SessionDetailPage /> },
          { path: 'discover', element: <DiscoverPage /> },
          { path: 'u/:handle', element: <ProfilePage /> },
          { path: 'settings', element: <SettingsPage /> },
          { path: 'pair', element: <PairDevicePage /> },
          { path: 'admin', element: <RequireAdmin><AdminPage /></RequireAdmin> },
          { path: '*', element: <NotFoundPage /> },
        ],
      },
    ],
  },
]);
