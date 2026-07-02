import "dotenv/config";
import { z } from "zod";

const envSchema = z.object({
  WP_SITE_URL: z.string().url(),
  WP_USERNAME: z.string().min(1).optional(),
  WP_APPLICATION_PASSWORD: z.string().min(1).optional(),
  WP_ALLOWED_HOSTS: z.string().optional(),
  WP_MAX_APPLY_OPERATIONS: z.string().optional()
});

export interface AppConfig {
  siteUrl: URL;
  username?: string;
  applicationPassword?: string;
  allowedHosts: string[];
  defaultMaxApplyOperations: number;
}

export function loadConfig(): AppConfig {
  const parsed = envSchema.safeParse(process.env);

  if (!parsed.success) {
    const missing = parsed.error.issues.map((x) => x.path.join(".")).join(", ");
    throw new Error(`Missing or invalid env configuration: ${missing}. See .env.example.`);
  }

  const data = parsed.data;
  const siteUrl = new URL(data.WP_SITE_URL);
  const allowedHosts = (data.WP_ALLOWED_HOSTS ?? siteUrl.hostname)
    .split(",")
    .map((h) => h.trim())
    .filter(Boolean);

  const maxOps = Number(data.WP_MAX_APPLY_OPERATIONS ?? "25");

  return {
    siteUrl,
    username: data.WP_USERNAME,
    applicationPassword: data.WP_APPLICATION_PASSWORD,
    allowedHosts,
    defaultMaxApplyOperations: Number.isFinite(maxOps) && maxOps > 0 ? maxOps : 25
  };
}

export function assertCanApply(host: string, allowedHosts: string[], expectedHost?: string): void {
  if (!allowedHosts.includes(host)) {
    throw new Error(
      `Refusing apply: host '${host}' is not in WP_ALLOWED_HOSTS. Allowed: ${allowedHosts.join(", ")}`
    );
  }

  if (expectedHost && expectedHost !== host) {
    throw new Error(
      `Refusing apply: expected host '${expectedHost}' does not match configured host '${host}'.`
    );
  }
}
