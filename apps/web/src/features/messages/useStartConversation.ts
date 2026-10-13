import { useMutation, useQueryClient } from '@tanstack/react-query';
import { useNavigate } from 'react-router';
import type { DirectConversationDto } from '@akeno/shared';
import { useToast } from '../../components/Toast';
import { api, errorMessage } from '../../lib/api';
import { queryKeys } from '../../lib/query-keys';

/** Finds or creates the direct conversation with a user and opens it. */
export function useStartConversation() {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const toast = useToast();
  return useMutation({
    mutationFn: (userId: string) => api.post<DirectConversationDto>('/conversations', { userId }),
    onSuccess: (conversation) => {
      void queryClient.invalidateQueries({ queryKey: queryKeys.conversations });
      void navigate(`/messages/${conversation.id}`);
    },
    onError: (error) => toast.error('Could not open the conversation', errorMessage(error)),
  });
}
