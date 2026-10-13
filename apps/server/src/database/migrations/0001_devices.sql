CREATE TABLE "device_credentials" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"user_id" uuid NOT NULL,
	"token_hash" varchar(64) NOT NULL,
	"name" varchar(48) NOT NULL,
	"platform" varchar(48),
	"client_version" varchar(32),
	"scopes" text[] NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"last_used_at" timestamp with time zone DEFAULT now() NOT NULL,
	"expires_at" timestamp with time zone NOT NULL,
	"ip_address" varchar(64)
);
--> statement-breakpoint
CREATE TABLE "device_pairings" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"device_code_hash" varchar(64) NOT NULL,
	"user_code_hash" varchar(64) NOT NULL,
	"device_name" varchar(48) NOT NULL,
	"platform" varchar(48),
	"client_version" varchar(32),
	"requested_scopes" text[] NOT NULL,
	"granted_scopes" text[],
	"status" varchar(16) DEFAULT 'pending' NOT NULL,
	"user_id" uuid,
	"device_id" uuid,
	"request_ip" varchar(64),
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"expires_at" timestamp with time zone NOT NULL,
	"decided_at" timestamp with time zone,
	"redeemed_at" timestamp with time zone,
	"last_polled_at" timestamp with time zone,
	CONSTRAINT "device_pairings_status_check" CHECK ("device_pairings"."status" IN ('pending', 'approved', 'denied', 'redeemed')),
	CONSTRAINT "device_pairings_decision_check" CHECK (("device_pairings"."status" = 'pending') = ("device_pairings"."user_id" IS NULL AND "device_pairings"."decided_at" IS NULL)),
	CONSTRAINT "device_pairings_redeemed_check" CHECK (("device_pairings"."status" = 'redeemed') = ("device_pairings"."redeemed_at" IS NOT NULL))
);
--> statement-breakpoint
ALTER TABLE "device_credentials" ADD CONSTRAINT "device_credentials_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "device_pairings" ADD CONSTRAINT "device_pairings_user_id_users_id_fk" FOREIGN KEY ("user_id") REFERENCES "public"."users"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
CREATE UNIQUE INDEX "device_credentials_token_hash_key" ON "device_credentials" USING btree ("token_hash");--> statement-breakpoint
CREATE INDEX "device_credentials_user_id_idx" ON "device_credentials" USING btree ("user_id");--> statement-breakpoint
CREATE INDEX "device_credentials_expires_at_idx" ON "device_credentials" USING btree ("expires_at");--> statement-breakpoint
CREATE UNIQUE INDEX "device_pairings_device_code_hash_key" ON "device_pairings" USING btree ("device_code_hash");--> statement-breakpoint
CREATE UNIQUE INDEX "device_pairings_user_code_hash_key" ON "device_pairings" USING btree ("user_code_hash");--> statement-breakpoint
CREATE INDEX "device_pairings_expires_at_idx" ON "device_pairings" USING btree ("expires_at");