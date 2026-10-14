ALTER TABLE "profiles" ADD COLUMN "game_playtime_visible" boolean DEFAULT true NOT NULL;--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "game_excluded_titles" text[] DEFAULT '{}'::text[] NOT NULL;--> statement-breakpoint
ALTER TABLE "game_activity_sessions" ADD COLUMN "private" boolean DEFAULT false NOT NULL;