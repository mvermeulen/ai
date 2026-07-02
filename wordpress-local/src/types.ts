export type Kind = "post" | "page";
export type Status = "draft" | "publish" | "pending" | "private";

export interface ContentRow {
  local_id: string;
  wp_id: number | null;
  kind: Kind;
  slug: string;
  title: string;
  status: Status;
  date: string | null;
  excerpt: string;
  categories: string[];
  tags: string[];
  local_content: string;
  base_content: string | null;
  base_modified_gmt: string | null;
  created_at: string;
  updated_at: string;
}

export interface MediaRow {
  local_path: string;
  file_hash: string;
  wp_id: number | null;
  source_url: string | null;
  uploaded_at: string | null;
}

export type OutboxOp = "create" | "update" | "delete";
export type OutboxResult = "applied" | "conflict" | "error";

export interface OutboxEntry {
  id: number;
  local_id: string;
  slug: string;
  op: OutboxOp;
  created_at: string;
  result: OutboxResult;
  detail: string;
  remote_backup: string | null;
}

export type SyncAction =
  | "new"
  | "unchanged"
  | "would-create"
  | "would-update"
  | "would-merge"
  | "create"
  | "update"
  | "merge"
  | "conflict"
  | "delete"
  | "error";

export interface SyncOutcome {
  slug: string;
  action: SyncAction;
  detail?: string;
  diff?: string;
  mergedPreview?: string;
}

export interface PullOutcome {
  slug: string;
  action: "new" | "fast-forwarded" | "skipped-local-changes" | "unchanged";
}
