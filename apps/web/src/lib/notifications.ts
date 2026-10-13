import type { NotificationData } from '@akeno/shared';

const KIND_LABEL = { group: 'group', party: 'party', game_session: 'game session' } as const;

/** English text and in-app link for a notification. */
export function describeNotification(data: NotificationData): { text: string; href: string } {
  const actor = data.actor.displayName;
  switch (data.type) {
    case 'friend_request.received':
      return { text: `${actor} sent you a friend request.`, href: '/friends?tab=requests' };
    case 'friend_request.accepted':
      return { text: `${actor} accepted your friend request.`, href: `/u/${data.actor.onlineId}` };
    case 'invitation.received': {
      const href =
        data.kind === 'group' ? `/groups/${data.targetId}` : data.kind === 'party' ? '/parties' : `/sessions/${data.targetId}`;
      return { text: `${actor} invited you to the ${KIND_LABEL[data.kind]} "${data.targetName}".`, href };
    }
    case 'game_session.join_requested':
      return { text: `${actor} asked to join your ${data.gameName} session.`, href: `/sessions/${data.sessionId}` };
    case 'game_session.request_accepted':
      return { text: `${actor} accepted your request to join ${data.gameName}.`, href: `/sessions/${data.sessionId}` };
    case 'game_session.request_rejected':
      return { text: `${actor} declined your request to join ${data.gameName}.`, href: `/sessions/${data.sessionId}` };
    case 'group.removed':
      return { text: `${actor} removed you from the group "${data.groupName}".`, href: '/groups' };
    case 'group.role_changed':
      return {
        text: `${actor} made you ${data.role === 'moderator' ? 'a moderator' : 'a member'} of "${data.groupName}".`,
        href: `/groups/${data.groupId}`,
      };
  }
}
