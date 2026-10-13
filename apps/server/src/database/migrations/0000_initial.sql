CREATE TYPE "public"."account_status" AS ENUM('active', 'suspended', 'deleted');--> statement-breakpoint
CREATE TYPE "public"."conversation_kind" AS ENUM('direct', 'group', 'party');--> statement-breakpoint
CREATE TYPE "public"."friend_request_privacy" AS ENUM('everyone', 'nobody');--> statement-breakpoint
CREATE TYPE "public"."friend_request_status" AS ENUM('pending', 'accepted', 'declined', 'cancelled');--> statement-breakpoint
CREATE TYPE "public"."game_session_join_mode" AS ENUM('open', 'approval');--> statement-breakpoint
CREATE TYPE "public"."game_session_role" AS ENUM('host', 'player');--> statement-breakpoint
CREATE TYPE "public"."game_session_status" AS ENUM('waiting', 'active', 'finished');--> statement-breakpoint
CREATE TYPE "public"."game_session_visibility" AS ENUM('public', 'friends', 'private');--> statement-breakpoint
CREATE TYPE "public"."group_join_policy" AS ENUM('invite_only', 'open');--> statement-breakpoint
CREATE TYPE "public"."group_role" AS ENUM('owner', 'moderator', 'member');--> statement-breakpoint
CREATE TYPE "public"."invitation_kind" AS ENUM('group', 'party', 'game_session');--> statement-breakpoint
CREATE TYPE "public"."invitation_status" AS ENUM('pending', 'accepted', 'declined', 'cancelled', 'expired');--> statement-breakpoint
CREATE TYPE "public"."join_request_status" AS ENUM('pending', 'accepted', 'rejected', 'cancelled');--> statement-breakpoint
CREATE TYPE "public"."message_privacy" AS ENUM('everyone', 'friends');--> statement-breakpoint
CREATE TYPE "public"."party_privacy" AS ENUM('invite_only', 'friends');--> statement-breakpoint
CREATE TYPE "public"."presence_preference" AS ENUM('online', 'away', 'dnd', 'invisible');--> statement-breakpoint
CREATE TYPE "public"."presence_visibility" AS ENUM('everyone', 'friends', 'nobody');--> statement-breakpoint
CREATE TYPE "public"."profile_visibility" AS ENUM('everyone', 'friends');--> statement-breakpoint
CREATE TYPE "public"."user_role" AS ENUM('user', 'admin');--> statement-breakpoint
CREATE TABLE "profiles" (
	"user_id" uuid PRIMARY KEY NOT NULL,
	"display_name" varchar(64) NOT NULL,
	"avatar" varchar(32) DEFAULT 'nova' NOT NULL,
	"bio" varchar(500) DEFAULT '' NOT NULL,
	"status_message" varchar(80) DEFAULT '' NOT NULL,
	"favorite_games" jsonb DEFAULT '[]'::jsonb NOT NULL,
	"activity" varchar(64),
	"activity_updated_at" timestamp with time zone,
	"presence_preference" "presence_preference" DEFAULT 'online' NOT NULL,
	"profile_visibility" "profile_visibility" DEFAULT 'everyone' NOT NULL,
	"presence_visibility" "presence_visibility" DEFAULT 'friends' NOT NULL,
	"last_seen_visibility" "presence_visibility" DEFAULT 'friends' NOT NULL,
	"message_privacy" "message_privacy" DEFAULT 'friends' NOT NULL,
	"friend_request_privacy" "friend_request_privacy" DEFAULT 'everyone' NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "registration_invitations" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"code_hash" varchar(64) NOT NULL,
	"code_hint" varchar(8) NOT NULL,
	"note" varchar(100) DEFAULT '' NOT NULL,
	"max_uses" integer DEFAULT 1 NOT NULL,
	"use_count" integer DEFAULT 0 NOT NULL,
	"expires_at" timestamp with time zone,
	"revoked_at" timestamp with time zone,
	"created_by_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "registration_invitations_max_uses_check" CHECK ("registration_invitations"."max_uses" BETWEEN 1 AND 100),
	CONSTRAINT "registration_invitations_use_count_check" CHECK ("registration_invitations"."use_count" >= 0 AND "registration_invitations"."use_count" <= "registration_invitations"."max_uses")
);
--> statement-breakpoint
CREATE TABLE "users" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"online_id" varchar(64) NOT NULL,
	"online_id_normalized" varchar(64) NOT NULL,
	"password_hash" text,
	"role" "user_role" DEFAULT 'user' NOT NULL,
	"status" "account_status" DEFAULT 'active' NOT NULL,
	"invited_by_invitation_id" uuid,
	"last_seen_at" timestamp with time zone,
	"password_changed_at" timestamp with time zone,
	"deleted_at" timestamp with time zone,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "auth_sessions" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"user_id" uuid NOT NULL,
	"token_hash" varchar(64) NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"last_used_at" timestamp with time zone DEFAULT now() NOT NULL,
	"expires_at" timestamp with time zone NOT NULL,
	"user_agent" varchar(256),
	"ip_address" varchar(64)
);
--> statement-breakpoint
CREATE TABLE "friend_requests" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"sender_id" uuid NOT NULL,
	"recipient_id" uuid NOT NULL,
	"status" "friend_request_status" DEFAULT 'pending' NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"responded_at" timestamp with time zone,
	CONSTRAINT "friend_requests_not_self_check" CHECK ("friend_requests"."sender_id" <> "friend_requests"."recipient_id")
);
--> statement-breakpoint
CREATE TABLE "friendships" (
	"user_low_id" uuid NOT NULL,
	"user_high_id" uuid NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "friendships_pkey" PRIMARY KEY("user_low_id","user_high_id"),
	CONSTRAINT "friendships_ordered_pair_check" CHECK ("friendships"."user_low_id" < "friendships"."user_high_id")
);
--> statement-breakpoint
CREATE TABLE "user_blocks" (
	"blocker_id" uuid NOT NULL,
	"blocked_id" uuid NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "user_blocks_pkey" PRIMARY KEY("blocker_id","blocked_id"),
	CONSTRAINT "user_blocks_not_self_check" CHECK ("user_blocks"."blocker_id" <> "user_blocks"."blocked_id")
);
--> statement-breakpoint
CREATE TABLE "conversation_read_states" (
	"conversation_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"last_read_message_id" bigint DEFAULT 0 NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "conversation_read_states_pkey" PRIMARY KEY("conversation_id","user_id")
);
--> statement-breakpoint
CREATE TABLE "conversations" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"kind" "conversation_kind" NOT NULL,
	"direct_user_low_id" uuid,
	"direct_user_high_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"last_message_at" timestamp with time zone,
	CONSTRAINT "conversations_direct_pair_check" CHECK (("conversations"."kind" = 'direct' AND "conversations"."direct_user_low_id" IS NOT NULL AND "conversations"."direct_user_high_id" IS NOT NULL AND "conversations"."direct_user_low_id" < "conversations"."direct_user_high_id")
        OR ("conversations"."kind" <> 'direct' AND "conversations"."direct_user_low_id" IS NULL AND "conversations"."direct_user_high_id" IS NULL))
);
--> statement-breakpoint
CREATE TABLE "messages" (
	"id" bigint PRIMARY KEY GENERATED ALWAYS AS IDENTITY (sequence name "messages_id_seq" INCREMENT BY 1 MINVALUE 1 MAXVALUE 9223372036854775807 START WITH 1 CACHE 1),
	"conversation_id" uuid NOT NULL,
	"sender_id" uuid NOT NULL,
	"body" text NOT NULL,
	"client_message_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "messages_body_length_check" CHECK (char_length("messages"."body") BETWEEN 1 AND 4000)
);
--> statement-breakpoint
CREATE TABLE "group_members" (
	"group_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"role" "group_role" DEFAULT 'member' NOT NULL,
	"joined_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "group_members_pkey" PRIMARY KEY("group_id","user_id")
);
--> statement-breakpoint
CREATE TABLE "groups" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"name" varchar(48) NOT NULL,
	"description" varchar(500) DEFAULT '' NOT NULL,
	"join_policy" "group_join_policy" DEFAULT 'invite_only' NOT NULL,
	"conversation_id" uuid NOT NULL,
	"created_by_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "parties" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"name" varchar(48) NOT NULL,
	"leader_id" uuid NOT NULL,
	"privacy" "party_privacy" DEFAULT 'friends' NOT NULL,
	"max_members" integer DEFAULT 8 NOT NULL,
	"member_count" integer DEFAULT 0 NOT NULL,
	"conversation_id" uuid NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "parties_max_members_check" CHECK ("parties"."max_members" BETWEEN 2 AND 16),
	CONSTRAINT "parties_member_count_check" CHECK ("parties"."member_count" >= 0 AND "parties"."member_count" <= "parties"."max_members")
);
--> statement-breakpoint
CREATE TABLE "party_members" (
	"party_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"joined_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "party_members_pkey" PRIMARY KEY("party_id","user_id")
);
--> statement-breakpoint
CREATE TABLE "game_session_join_requests" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"session_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"status" "join_request_status" DEFAULT 'pending' NOT NULL,
	"message" varchar(200) DEFAULT '' NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"responded_at" timestamp with time zone,
	"responded_by_id" uuid
);
--> statement-breakpoint
CREATE TABLE "game_session_members" (
	"session_id" uuid NOT NULL,
	"user_id" uuid NOT NULL,
	"role" "game_session_role" DEFAULT 'player' NOT NULL,
	"joined_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "game_session_members_pkey" PRIMARY KEY("session_id","user_id")
);
--> statement-breakpoint
CREATE TABLE "game_sessions" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"game_name" varchar(64) NOT NULL,
	"game_identifier" varchar(64),
	"description" varchar(500) DEFAULT '' NOT NULL,
	"host_id" uuid NOT NULL,
	"visibility" "game_session_visibility" DEFAULT 'friends' NOT NULL,
	"join_mode" "game_session_join_mode" DEFAULT 'open' NOT NULL,
	"max_participants" integer DEFAULT 4 NOT NULL,
	"participant_count" integer DEFAULT 0 NOT NULL,
	"status" "game_session_status" DEFAULT 'waiting' NOT NULL,
	"region" varchar(32),
	"platform" varchar(32),
	"connection_info" varchar(300),
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	"started_at" timestamp with time zone,
	"ended_at" timestamp with time zone,
	CONSTRAINT "game_sessions_max_participants_check" CHECK ("game_sessions"."max_participants" BETWEEN 2 AND 64),
	CONSTRAINT "game_sessions_participant_count_check" CHECK ("game_sessions"."participant_count" >= 0 AND "game_sessions"."participant_count" <= "game_sessions"."max_participants")
);
--> statement-breakpoint
CREATE TABLE "invitations" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"kind" "invitation_kind" NOT NULL,
	"group_id" uuid,
	"party_id" uuid,
	"game_session_id" uuid,
	"inviter_id" uuid NOT NULL,
	"invitee_id" uuid NOT NULL,
	"status" "invitation_status" DEFAULT 'pending' NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"responded_at" timestamp with time zone,
	"expires_at" timestamp with time zone NOT NULL,
	CONSTRAINT "invitations_target_check" CHECK (("invitations"."kind" = 'group' AND "invitations"."group_id" IS NOT NULL AND "invitations"."party_id" IS NULL AND "invitations"."game_session_id" IS NULL)
        OR ("invitations"."kind" = 'party' AND "invitations"."party_id" IS NOT NULL AND "invitations"."group_id" IS NULL AND "invitations"."game_session_id" IS NULL)
        OR ("invitations"."kind" = 'game_session' AND "invitations"."game_session_id" IS NOT NULL AND "invitations"."group_id" IS NULL AND "invitations"."party_id" IS NULL)),
	CONSTRAINT "invitations_not_self_check" CHECK ("invitations"."inviter_id" <> "invitations"."invitee_id")
);
--> statement-breakpoint
CREATE TABLE "audit_events" (
	"id" bigint PRIMARY KEY GENERATED ALWAYS AS IDENTITY (sequence name "audit_events_id_seq" INCREMENT BY 1 MINVALUE 1 MAXVALUE 9223372036854775807 START WITH 1 CACHE 1),
	"actor_id" uuid,
	"action" varchar(64) NOT NULL,
	"target_type" varchar(32),
	"target_id" varchar(64),
	"metadata" jsonb DEFAULT '{}'::jsonb NOT NULL,
	"ip_address" varchar(64),
	"created_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "notifications" (
	"id" bigint PRIMARY KEY GENERATED ALWAYS AS IDENTITY (sequence name "notifications_id_seq" INCREMENT BY 1 MINVALUE 1 MAXVALUE 9223372036854775807 START WITH 1 CACHE 1),
	"user_id" uuid NOT NULL,
	"type" varchar(64) NOT NULL,
	"data" jsonb NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"read_at" timestamp with time zone
);
--> statement-breakpoint
ALTER TABLE "profiles" ADD CONSTRAINT "profiles_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "registration_invitations" ADD CONSTRAINT "registration_invitations_created_by_id_users_id_fk" FOREIGN KEY ("created_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "users" ADD CONSTRAINT "users_invited_by_invitation_id_registration_invitations_id_fk" FOREIGN KEY ("invited_by_invitation_id") REFERENCES "public"."registration_invitations"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "auth_sessions" ADD CONSTRAINT "auth_sessions_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "friend_requests" ADD CONSTRAINT "friend_requests_sender_id_users_id_fk" FOREIGN KEY ("sender_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "friend_requests" ADD CONSTRAINT "friend_requests_recipient_id_users_id_fk" FOREIGN KEY ("recipient_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "friendships" ADD CONSTRAINT "friendships_user_low_id_users_id_fk" FOREIGN KEY ("user_low_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "friendships" ADD CONSTRAINT "friendships_user_high_id_users_id_fk" FOREIGN KEY ("user_high_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "user_blocks" ADD CONSTRAINT "user_blocks_blocker_id_users_id_fk" FOREIGN KEY ("blocker_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "user_blocks" ADD CONSTRAINT "user_blocks_blocked_id_users_id_fk" FOREIGN KEY ("blocked_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "conversation_read_states" ADD CONSTRAINT "conversation_read_states_conversation_id_conversations_id_fk" FOREIGN KEY ("conversation_id") REFERENCES "public"."conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "conversation_read_states" ADD CONSTRAINT "conversation_read_states_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "conversations" ADD CONSTRAINT "conversations_direct_user_low_id_users_id_fk" FOREIGN KEY ("direct_user_low_id") REFERENCES "public"."users"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "conversations" ADD CONSTRAINT "conversations_direct_user_high_id_users_id_fk" FOREIGN KEY ("direct_user_high_id") REFERENCES "public"."users"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "messages" ADD CONSTRAINT "messages_conversation_id_conversations_id_fk" FOREIGN KEY ("conversation_id") REFERENCES "public"."conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "messages" ADD CONSTRAINT "messages_sender_id_users_id_fk" FOREIGN KEY ("sender_id") REFERENCES "public"."users"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "group_members" ADD CONSTRAINT "group_members_group_id_groups_id_fk" FOREIGN KEY ("group_id") REFERENCES "public"."groups"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "group_members" ADD CONSTRAINT "group_members_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "groups" ADD CONSTRAINT "groups_conversation_id_conversations_id_fk" FOREIGN KEY ("conversation_id") REFERENCES "public"."conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "groups" ADD CONSTRAINT "groups_created_by_id_users_id_fk" FOREIGN KEY ("created_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "parties" ADD CONSTRAINT "parties_leader_id_users_id_fk" FOREIGN KEY ("leader_id") REFERENCES "public"."users"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "parties" ADD CONSTRAINT "parties_conversation_id_conversations_id_fk" FOREIGN KEY ("conversation_id") REFERENCES "public"."conversations"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "party_members" ADD CONSTRAINT "party_members_party_id_parties_id_fk" FOREIGN KEY ("party_id") REFERENCES "public"."parties"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "party_members" ADD CONSTRAINT "party_members_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_session_join_requests" ADD CONSTRAINT "game_session_join_requests_session_id_game_sessions_id_fk" FOREIGN KEY ("session_id") REFERENCES "public"."game_sessions"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_session_join_requests" ADD CONSTRAINT "game_session_join_requests_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_session_join_requests" ADD CONSTRAINT "game_session_join_requests_responded_by_id_users_id_fk" FOREIGN KEY ("responded_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_session_members" ADD CONSTRAINT "game_session_members_session_id_game_sessions_id_fk" FOREIGN KEY ("session_id") REFERENCES "public"."game_sessions"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_session_members" ADD CONSTRAINT "game_session_members_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_sessions" ADD CONSTRAINT "game_sessions_host_id_users_id_fk" FOREIGN KEY ("host_id") REFERENCES "public"."users"("id") ON DELETE restrict ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "invitations" ADD CONSTRAINT "invitations_group_id_groups_id_fk" FOREIGN KEY ("group_id") REFERENCES "public"."groups"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "invitations" ADD CONSTRAINT "invitations_party_id_parties_id_fk" FOREIGN KEY ("party_id") REFERENCES "public"."parties"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "invitations" ADD CONSTRAINT "invitations_game_session_id_game_sessions_id_fk" FOREIGN KEY ("game_session_id") REFERENCES "public"."game_sessions"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "invitations" ADD CONSTRAINT "invitations_inviter_id_users_id_fk" FOREIGN KEY ("inviter_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "invitations" ADD CONSTRAINT "invitations_invitee_id_users_id_fk" FOREIGN KEY ("invitee_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "audit_events" ADD CONSTRAINT "audit_events_actor_id_users_id_fk" FOREIGN KEY ("actor_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "notifications" ADD CONSTRAINT "notifications_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "profiles_display_name_idx" ON "profiles" USING btree (lower("display_name"));--> statement-breakpoint
CREATE UNIQUE INDEX "registration_invitations_code_hash_key" ON "registration_invitations" USING btree ("code_hash");--> statement-breakpoint
CREATE UNIQUE INDEX "users_online_id_normalized_key" ON "users" USING btree ("online_id_normalized");--> statement-breakpoint
CREATE INDEX "users_status_idx" ON "users" USING btree ("status");--> statement-breakpoint
CREATE UNIQUE INDEX "auth_sessions_token_hash_key" ON "auth_sessions" USING btree ("token_hash");--> statement-breakpoint
CREATE INDEX "auth_sessions_user_id_idx" ON "auth_sessions" USING btree ("user_id");--> statement-breakpoint
CREATE INDEX "auth_sessions_expires_at_idx" ON "auth_sessions" USING btree ("expires_at");--> statement-breakpoint
CREATE UNIQUE INDEX "friend_requests_pending_pair_key" ON "friend_requests" USING btree (least("sender_id", "recipient_id"),greatest("sender_id", "recipient_id")) WHERE "friend_requests"."status" = 'pending';--> statement-breakpoint
CREATE INDEX "friend_requests_recipient_status_idx" ON "friend_requests" USING btree ("recipient_id","status");--> statement-breakpoint
CREATE INDEX "friend_requests_sender_status_idx" ON "friend_requests" USING btree ("sender_id","status");--> statement-breakpoint
CREATE INDEX "friendships_user_high_id_idx" ON "friendships" USING btree ("user_high_id");--> statement-breakpoint
CREATE INDEX "user_blocks_blocked_id_idx" ON "user_blocks" USING btree ("blocked_id");--> statement-breakpoint
CREATE INDEX "conversation_read_states_user_id_idx" ON "conversation_read_states" USING btree ("user_id");--> statement-breakpoint
CREATE UNIQUE INDEX "conversations_direct_pair_key" ON "conversations" USING btree ("direct_user_low_id","direct_user_high_id") WHERE "conversations"."kind" = 'direct';--> statement-breakpoint
CREATE INDEX "conversations_direct_high_idx" ON "conversations" USING btree ("direct_user_high_id");--> statement-breakpoint
CREATE INDEX "messages_conversation_id_id_idx" ON "messages" USING btree ("conversation_id","id");--> statement-breakpoint
CREATE UNIQUE INDEX "messages_sender_client_message_id_key" ON "messages" USING btree ("sender_id","client_message_id");--> statement-breakpoint
CREATE UNIQUE INDEX "group_members_single_owner_key" ON "group_members" USING btree ("group_id") WHERE "group_members"."role" = 'owner';--> statement-breakpoint
CREATE INDEX "group_members_user_id_idx" ON "group_members" USING btree ("user_id");--> statement-breakpoint
CREATE UNIQUE INDEX "groups_conversation_id_key" ON "groups" USING btree ("conversation_id");--> statement-breakpoint
CREATE INDEX "groups_join_policy_idx" ON "groups" USING btree ("join_policy");--> statement-breakpoint
CREATE UNIQUE INDEX "parties_conversation_id_key" ON "parties" USING btree ("conversation_id");--> statement-breakpoint
CREATE INDEX "parties_leader_id_idx" ON "parties" USING btree ("leader_id");--> statement-breakpoint
CREATE UNIQUE INDEX "party_members_user_id_key" ON "party_members" USING btree ("user_id");--> statement-breakpoint
CREATE UNIQUE INDEX "game_session_join_requests_pending_key" ON "game_session_join_requests" USING btree ("session_id","user_id") WHERE "game_session_join_requests"."status" = 'pending';--> statement-breakpoint
CREATE INDEX "game_session_join_requests_user_id_idx" ON "game_session_join_requests" USING btree ("user_id");--> statement-breakpoint
CREATE INDEX "game_session_members_user_id_idx" ON "game_session_members" USING btree ("user_id");--> statement-breakpoint
CREATE INDEX "game_sessions_status_visibility_idx" ON "game_sessions" USING btree ("status","visibility","created_at");--> statement-breakpoint
CREATE INDEX "game_sessions_host_id_idx" ON "game_sessions" USING btree ("host_id");--> statement-breakpoint
CREATE UNIQUE INDEX "invitations_pending_group_key" ON "invitations" USING btree ("group_id","invitee_id") WHERE "invitations"."status" = 'pending' AND "invitations"."kind" = 'group';--> statement-breakpoint
CREATE UNIQUE INDEX "invitations_pending_party_key" ON "invitations" USING btree ("party_id","invitee_id") WHERE "invitations"."status" = 'pending' AND "invitations"."kind" = 'party';--> statement-breakpoint
CREATE UNIQUE INDEX "invitations_pending_game_session_key" ON "invitations" USING btree ("game_session_id","invitee_id") WHERE "invitations"."status" = 'pending' AND "invitations"."kind" = 'game_session';--> statement-breakpoint
CREATE INDEX "invitations_invitee_status_idx" ON "invitations" USING btree ("invitee_id","status");--> statement-breakpoint
CREATE INDEX "audit_events_created_at_idx" ON "audit_events" USING btree ("created_at");--> statement-breakpoint
CREATE INDEX "audit_events_actor_id_idx" ON "audit_events" USING btree ("actor_id");--> statement-breakpoint
CREATE INDEX "notifications_user_id_id_idx" ON "notifications" USING btree ("user_id","id");