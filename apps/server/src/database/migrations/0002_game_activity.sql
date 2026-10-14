CREATE TABLE "game_activity_sessions" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"user_id" uuid NOT NULL,
	"title_id" varchar(16) NOT NULL,
	"title_name" varchar(128),
	"platform" varchar(16) NOT NULL,
	"started_at" timestamp with time zone DEFAULT now() NOT NULL,
	"last_heartbeat_at" timestamp with time zone DEFAULT now() NOT NULL,
	"ended_at" timestamp with time zone,
	"end_reason" varchar(24),
	"started_by_device_id" uuid,
	"last_device_id" uuid,
	"heartbeat_count" integer DEFAULT 1 NOT NULL,
	CONSTRAINT "game_activity_sessions_heartbeat_check" CHECK ("game_activity_sessions"."last_heartbeat_at" >= "game_activity_sessions"."started_at"),
	CONSTRAINT "game_activity_sessions_end_check" CHECK ("game_activity_sessions"."ended_at" IS NULL OR "game_activity_sessions"."ended_at" >= "game_activity_sessions"."started_at"),
	CONSTRAINT "game_activity_sessions_reason_check" CHECK (("game_activity_sessions"."ended_at" IS NULL) = ("game_activity_sessions"."end_reason" IS NULL))
);
--> statement-breakpoint
CREATE TABLE "game_titles" (
	"title_id" varchar(16) PRIMARY KEY NOT NULL,
	"name" varchar(128) NOT NULL,
	"updated_by_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "game_activity_tracking" boolean DEFAULT true NOT NULL;--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "game_activity_visibility" "presence_visibility" DEFAULT 'friends' NOT NULL;--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "game_history_visibility" "presence_visibility" DEFAULT 'friends' NOT NULL;--> statement-breakpoint
ALTER TABLE "game_activity_sessions" ADD CONSTRAINT "game_activity_sessions_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_activity_sessions" ADD CONSTRAINT "game_activity_sessions_started_by_device_id_device_credentials_id_fk" FOREIGN KEY ("started_by_device_id") REFERENCES "public"."device_credentials"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_activity_sessions" ADD CONSTRAINT "game_activity_sessions_last_device_id_device_credentials_id_fk" FOREIGN KEY ("last_device_id") REFERENCES "public"."device_credentials"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_titles" ADD CONSTRAINT "game_titles_updated_by_id_users_id_fk" FOREIGN KEY ("updated_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
CREATE UNIQUE INDEX "game_activity_sessions_active_key" ON "game_activity_sessions" USING btree ("user_id","title_id") WHERE "game_activity_sessions"."ended_at" IS NULL;--> statement-breakpoint
CREATE INDEX "game_activity_sessions_user_started_idx" ON "game_activity_sessions" USING btree ("user_id","started_at");--> statement-breakpoint
CREATE INDEX "game_activity_sessions_active_heartbeat_idx" ON "game_activity_sessions" USING btree ("last_heartbeat_at") WHERE "game_activity_sessions"."ended_at" IS NULL;