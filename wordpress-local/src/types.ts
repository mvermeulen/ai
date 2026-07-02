export type ContentType = "post" | "page";

export interface LocalFrontmatter {
  title: string;
  slug?: string;
  status?: "draft" | "publish" | "private";
  date?: string;
  excerpt?: string;
  tags?: string[];
  categories?: string[];
  strava_routes?: string[];
}

export interface LocalDocument {
  type: ContentType;
  filePath: string;
  relativePath: string;
  slug: string;
  frontmatter: LocalFrontmatter;
  markdownBody: string;
  htmlBody: string;
  localHash: string;
  mediaRefs: string[];
}

export interface SyncManifestItem {
  key: string;
  type: ContentType;
  slug: string;
  wpId: number;
  wpModifiedGmt: string;
  lastSyncedLocalHash: string;
  lastSyncedAt: string;
}

export interface MediaManifestItem {
  relPath: string;
  hash: string;
  wpId: number;
  sourceUrl: string;
  uploadedAt: string;
}

export interface SyncManifest {
  version: 1;
  items: Record<string, SyncManifestItem>;
  media: Record<string, MediaManifestItem>;
}

export interface WpPostLike {
  id: number;
  slug: string;
  title?: { rendered: string };
  modified_gmt: string;
  status: string;
  link?: string;
  type?: string;
}

export interface SyncOperation {
  kind: "create" | "update" | "skip" | "conflict" | "error";
  type: ContentType;
  slug: string;
  reason: string;
  filePath: string;
  wpId?: number;
}

export interface SyncOptions {
  apply: boolean;
  allowCreate: boolean;
  allowUpdate: boolean;
  allowConflictOverride: boolean;
  maxApplyOperations: number;
  approveLargeSync: boolean;
  expectedHost?: string;
}
