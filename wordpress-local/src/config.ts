import fs from "node:fs";
import path from "node:path";

export const APP_PASSWORD_ENV_VAR = "WPSYNC2_APP_PASSWORD";
const CONFIG_DIR = ".wpsync2";
const CONFIG_FILE = "config.json";

export class ConfigError extends Error {}

export interface Config {
  baseUrl: string;
  username: string;
  appPassword?: string;
  dbPath: string;
  mediaDir: string;
  root: string;
}

export function loadConfig(root = "."): Config {
  const configPath = path.join(root, CONFIG_DIR, CONFIG_FILE);
  if (!fs.existsSync(configPath)) {
    throw new ConfigError(`No config found at ${configPath}. Run \`wpsync2 init\` first.`);
  }
  const data = JSON.parse(fs.readFileSync(configPath, "utf-8"));
  if (!data.baseUrl || !data.username) {
    throw new ConfigError(`${configPath} must set both baseUrl and username`);
  }
  return {
    baseUrl: data.baseUrl,
    username: data.username,
    appPassword: process.env[APP_PASSWORD_ENV_VAR] ?? data.appPassword,
    dbPath: path.join(root, CONFIG_DIR, "site.db"),
    mediaDir: path.join(root, data.mediaDir ?? "media"),
    root,
  };
}

export function requireCredentials(config: Config): string {
  if (!config.appPassword) {
    throw new ConfigError(
      `No application password available. Set the ${APP_PASSWORD_ENV_VAR} environment variable ` +
        `(recommended), or add "appPassword" to .wpsync2/config.json. Create one under WordPress admin ` +
        `-> Users -> Profile -> Application Passwords.`
    );
  }
  return config.appPassword;
}

export function writeConfigTemplate(root: string, baseUrl: string, username: string): string {
  const dir = path.join(root, CONFIG_DIR);
  fs.mkdirSync(dir, { recursive: true });
  const configPath = path.join(dir, CONFIG_FILE);
  const template = {
    baseUrl,
    username,
    mediaDir: "media",
    _note: `Prefer setting ${APP_PASSWORD_ENV_VAR} in your shell instead of an appPassword field here. This file is gitignored either way.`,
  };
  fs.writeFileSync(configPath, JSON.stringify(template, null, 2) + "\n", "utf-8");
  return configPath;
}
