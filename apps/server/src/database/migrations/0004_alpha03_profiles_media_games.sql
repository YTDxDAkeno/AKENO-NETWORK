CREATE TABLE "media_objects" (
	"id" varchar(32) PRIMARY KEY NOT NULL,
	"kind" varchar(16) NOT NULL,
	"owner_id" uuid,
	"width" integer NOT NULL,
	"height" integer NOT NULL,
	"variants" jsonb NOT NULL,
	"total_bytes" bigint NOT NULL,
	"source_sha256" varchar(64) NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"deleted_at" timestamp with time zone,
	CONSTRAINT "media_objects_id_check" CHECK ("media_objects"."id" ~ '^[0-9a-f]{32}$'),
	CONSTRAINT "media_objects_kind_check" CHECK ("media_objects"."kind" IN ('avatar', 'banner', 'game_icon', 'game_cover', 'game_banner'))
);
--> statement-breakpoint
CREATE TABLE "game_title_mappings" (
	"title_id" varchar(16) PRIMARY KEY NOT NULL,
	"game_id" uuid NOT NULL,
	"source" varchar(32) DEFAULT 'admin' NOT NULL,
	"updated_by_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "game_title_mappings_title_id_check" CHECK ("game_title_mappings"."title_id" ~ '^[A-Z]{4}[0-9]{5}$')
);
--> statement-breakpoint
CREATE TABLE "games" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"name" varchar(128) NOT NULL,
	"aliases" text[] DEFAULT '{}'::text[] NOT NULL,
	"icon_media_id" varchar(32),
	"cover_media_id" varchar(32),
	"banner_media_id" varchar(32),
	"artwork_source" varchar(32),
	"artwork_attribution" varchar(200) DEFAULT '' NOT NULL,
	"artwork_license" varchar(300) DEFAULT '' NOT NULL,
	"updated_by_id" uuid,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "user_badges" (
	"user_id" uuid NOT NULL,
	"badge_id" varchar(32) NOT NULL,
	"granted_by_id" uuid,
	"note" varchar(100) DEFAULT '' NOT NULL,
	"granted_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "user_badges_pkey" PRIMARY KEY("user_id","badge_id")
);
--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "avatar_media_id" varchar(32);--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "banner_media_id" varchar(32);--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "banner_overlay" boolean DEFAULT true NOT NULL;--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "theme" varchar(16) DEFAULT 'default' NOT NULL;--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "accent_color" varchar(7);--> statement-breakpoint
ALTER TABLE "profiles" ADD COLUMN "featured_badges" text[] DEFAULT '{}'::text[] NOT NULL;--> statement-breakpoint
ALTER TABLE "media_objects" ADD CONSTRAINT "media_objects_owner_id_users_id_fk" FOREIGN KEY ("owner_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_title_mappings" ADD CONSTRAINT "game_title_mappings_game_id_games_id_fk" FOREIGN KEY ("game_id") REFERENCES "public"."games"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "game_title_mappings" ADD CONSTRAINT "game_title_mappings_updated_by_id_users_id_fk" FOREIGN KEY ("updated_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "games" ADD CONSTRAINT "games_icon_media_id_media_objects_id_fk" FOREIGN KEY ("icon_media_id") REFERENCES "public"."media_objects"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "games" ADD CONSTRAINT "games_cover_media_id_media_objects_id_fk" FOREIGN KEY ("cover_media_id") REFERENCES "public"."media_objects"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "games" ADD CONSTRAINT "games_banner_media_id_media_objects_id_fk" FOREIGN KEY ("banner_media_id") REFERENCES "public"."media_objects"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "games" ADD CONSTRAINT "games_updated_by_id_users_id_fk" FOREIGN KEY ("updated_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "user_badges" ADD CONSTRAINT "user_badges_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "user_badges" ADD CONSTRAINT "user_badges_granted_by_id_users_id_fk" FOREIGN KEY ("granted_by_id") REFERENCES "public"."users"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "media_objects_owner_created_idx" ON "media_objects" USING btree ("owner_id","created_at");--> statement-breakpoint
CREATE INDEX "media_objects_deleted_idx" ON "media_objects" USING btree ("deleted_at") WHERE "media_objects"."deleted_at" IS NOT NULL;--> statement-breakpoint
CREATE INDEX "game_title_mappings_game_id_idx" ON "game_title_mappings" USING btree ("game_id");--> statement-breakpoint
CREATE INDEX "games_name_idx" ON "games" USING btree (lower("name"));--> statement-breakpoint
CREATE INDEX "user_badges_badge_id_idx" ON "user_badges" USING btree ("badge_id");--> statement-breakpoint
ALTER TABLE "profiles" ADD CONSTRAINT "profiles_avatar_media_id_media_objects_id_fk" FOREIGN KEY ("avatar_media_id") REFERENCES "public"."media_objects"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "profiles" ADD CONSTRAINT "profiles_banner_media_id_media_objects_id_fk" FOREIGN KEY ("banner_media_id") REFERENCES "public"."media_objects"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
-- Data migration: every administrator-curated name of Alpha 0.2 (game_titles) becomes a
-- canonical game with one verified title mapping. game_titles itself is kept unchanged so the
-- previous release still works after a rollback.
WITH source AS (
	SELECT gen_random_uuid() AS game_id, title_id, name, updated_by_id, created_at, updated_at
	FROM "game_titles"
	WHERE title_id ~ '^[A-Z]{4}[0-9]{5}$'
), inserted AS (
	INSERT INTO "games" ("id", "name", "updated_by_id", "created_at", "updated_at")
	SELECT game_id, name, updated_by_id, created_at, updated_at FROM source
	RETURNING "id"
)
INSERT INTO "game_title_mappings" ("title_id", "game_id", "source", "updated_by_id", "created_at", "updated_at")
SELECT s.title_id, s.game_id, 'migrated', s.updated_by_id, s.created_at, s.updated_at
FROM source s
WHERE s.game_id IN (SELECT "id" FROM inserted);
