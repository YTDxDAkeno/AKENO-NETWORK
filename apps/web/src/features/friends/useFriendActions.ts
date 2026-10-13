import { useMutation, useQueryClient } from '@tanstack/react-query';
import type { SendFriendRequestResponse } from '@akeno/shared';
import { useConfirm } from '../../components/ConfirmDialog';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';

/** Friend-related mutations with consistent feedback and cache invalidation. */
export function useFriendActions() {
  const queryClient = useQueryClient();
  const toast = useToast();
  const confirm = useConfirm();

  const refresh = () => {
    for (const key of [queryKeys.friends, queryKeys.friendRequests, queryKeys.blocks, queryKeys.profiles, ['search'], queryKeys.conversations]) {
      void queryClient.invalidateQueries({ queryKey: key });
    }
  };
  const onError = (title: string) => (error: unknown) => toast.error(title, errorMessage(error));

  const sendRequest = useMutation({
    mutationFn: (userId: string) => api.post<SendFriendRequestResponse>('/friends/requests', { userId }),
    onSuccess: (result) => {
      toast.success(result.status === 'accepted' ? 'You are now friends.' : 'Friend request sent.');
      refresh();
    },
    onError: onError('Could not send the friend request'),
  });

  const accept = useMutation({
    mutationFn: (requestId: string) => api.post(`/friends/requests/${requestId}/accept`),
    onSuccess: () => {
      toast.success('Friend request accepted.');
      refresh();
    },
    onError: onError('Could not accept the request'),
  });

  const decline = useMutation({
    mutationFn: (requestId: string) => api.post(`/friends/requests/${requestId}/decline`),
    onSuccess: refresh,
    onError: onError('Could not decline the request'),
  });

  const cancel = useMutation({
    mutationFn: (requestId: string) => api.post(`/friends/requests/${requestId}/cancel`),
    onSuccess: () => {
      toast.info('Friend request cancelled.');
      refresh();
    },
    onError: onError('Could not cancel the request'),
  });

  const remove = useMutation({
    mutationFn: (userId: string) => api.delete(`/friends/${userId}`),
    onSuccess: () => {
      toast.info('Friend removed.');
      refresh();
    },
    onError: onError('Could not remove the friend'),
  });

  const block = useMutation({
    mutationFn: (userId: string) => api.post('/blocks', { userId }),
    onSuccess: () => {
      toast.info('User blocked.');
      refresh();
    },
    onError: onError('Could not block the user'),
  });

  const unblock = useMutation({
    mutationFn: (userId: string) => api.delete(`/blocks/${userId}`),
    onSuccess: () => {
      toast.info('User unblocked.');
      refresh();
    },
    onError: onError('Could not unblock the user'),
  });

  return {
    sendRequest,
    accept,
    decline,
    cancel,
    unblock,
    confirmRemove: async (userId: string, name: string) => {
      const ok = await confirm({
        title: `Remove ${name}?`,
        message: 'You will no longer see each other in your friends lists. You can send a new request later.',
        confirmLabel: 'Remove friend',
      });
      if (ok) remove.mutate(userId);
    },
    confirmBlock: async (userId: string, name: string) => {
      const ok = await confirm({
        title: `Block ${name}?`,
        message:
          'Blocking removes the friendship, cancels pending requests and invitations, and prevents messages, friend requests and invitations between you. They are not notified.',
        confirmLabel: 'Block user',
      });
      if (ok) block.mutate(userId);
    },
  };
}
