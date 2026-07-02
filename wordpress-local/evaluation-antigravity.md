# Evaluation of WordPress Local Sync Implementations

## Approaches Taken: A Comparison

The six branches explore different architectures and synchronization models for maintaining an offline markdown-based authoring environment that syncs with a WordPress site. They primarily diverge along three axes: **State Management**, **Execution Model**, and **Conflict Resolution**.

**1. State Management (Database vs. File System vs. Manifest):**
- **antigravity-v1 & claude-v2:** Use a local **SQLite database** to store metadata, sync status, and base content. This provides robust querying and atomic updates but introduces a binary database into the workspace.
- **antigravity-v2:** Uses a pure **File System (Frontmatter)** approach. Sync metadata (like `wp_id` and `status`) is injected directly back into the markdown frontmatter.
- **claude-v1 & copilot-v1:** Rely on a **JSON Manifest / State Store** (`.wpsync/state.json` or `sync-manifest.json`). This separates metadata from content while keeping it text-based and easy to version control.

**2. Execution Model (Direct Execution vs. Planner Pattern):**
- Most versions (`antigravity-v1`, `claude-v1`, `claude-v2`, `copilot-v1`) use a standard **dry-run flag** architecture.
- **copilot-v2** adopts a strict **Planner Pattern (Terraform-style)**. It forces the generation of an immutable, checksum-verified `plan.json` file. Applying changes requires explicitly referencing this plan and providing a generated `requires_ack` phrase. This provides high safety against accidental destructive syncs.

**3. Conflict Resolution:**
- **antigravity-v1 & v2:** Basic conflict detection (e.g. `sync_status = 'local_modified'`) but lacking deep verification.
- **claude-v1 & copilot-v1:** Use a two-signal approach: tracking WordPress's `modified_gmt` timestamp and a hash of the local rendered HTML (`content_hash`). If the remote timestamp moved since the last sync, it throws a conflict and halts.
- **claude-v2:** Introduces **3-Way Merging** (`node-diff3`). It stores a snapshot of the `base_content` at the last sync. If a conflict is detected, it attempts to merge `base_content`, `local_content`, and `remote_content` automatically, only failing if the edits overlap.

## Identified Best Practices

*   **Decoupled State via JSON Manifest (claude-v1 / copilot-v1):** Keeping sync state (hashes, timestamps, WP IDs) in a centralized state file rather than muddying the source markdown frontmatter. It's cleaner than a SQLite DB for a git-backed repo.
*   **3-Way Merging (claude-v2):** A phenomenal addition that prevents the frustrating "remote changed" block when changes don't actually overlap (e.g., someone fixed a typo on the live site while you wrote a new paragraph offline).
*   **The Planner/Apply Pattern (copilot-v2):** Generating a static plan that must be acknowledged (e.g. `APPROVE-plan-12345`) provides maximum safety, ensuring the user knows exactly what will happen before touching the live database.
*   **Two-Signal Conflict Detection (claude-v1):** Using the remote `modified_gmt` to detect live-site edits, and the local `content_hash` to detect offline edits, is the most robust way to determine state drift without hitting the WP API unnecessarily.
*   **Offline Media Resolving (claude-v1):** Using an `offline_resolver` to map local image paths to uploaded WP media URLs dynamically during HTML render, ensuring the local markdown works cleanly in both environments.

## Key Questions to Determine the Choice

1. **How technical are the users?**
   - If users are developers, the Terraform-style **Planner** (`copilot-v2`) is familiar and highly appreciated for its strictness. If they are non-technical writers, a simple dry-run flag or a GUI (like `antigravity-v1`) might be better.
2. **How frequently do concurrent edits happen?**
   - If multiple people edit the live WordPress dashboard while others write offline, **3-Way Merging** (`claude-v2`) is absolutely essential to prevent constant sync blocks. If it's a single-author blog, simpler conflict halting (`claude-v1`) is sufficient.
3. **Is the repository tracked in Git?**
   - If the markdown files are tracked in Git, storing state in a JSON manifest (`claude-v1`, `copilot-v1`) is vastly superior to SQLite, as it allows users to see exactly how sync state changes in their commits without dealing with binary blobs.

## Recommended Approach

We recommend starting with the architecture of **claude-v1** (Python) or **copilot-v1** (TypeScript) due to their clean JSON manifest state management and separation of concerns.

However, the final tool should **adopt the following ideas from the other branches**:
1.  **Adopt the 3-Way Merge from `claude-v2`**: Store the `base_content` in the manifest or alongside the state, and use a diff3 library to automatically resolve non-overlapping remote edits.
2.  **Adopt the Planner Pattern from `copilot-v2`**: Instead of a simple `--dry-run`, the tool should generate a `plan-<timestamp>.json` with a checksum and require `apply --plan <file> --ack <phrase>` to execute.
3.  **Implement Offline Media Resolution**: Ensure images are managed seamlessly as demonstrated in `claude-v1` so that offline previews aren't broken.

This hybrid approach will yield a Git-friendly, merge-capable, and highly safe synchronization engine.
