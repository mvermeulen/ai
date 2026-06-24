# BillMinder High-Level Design

## Status

Draft 0.1

This document is intentionally requirements-first. It captures the problem, constraints, success criteria, and architectural direction at a high level. Detailed design, schema, protocols, and implementation plans should wait until the open questions at the end are answered.

## Current Working Assumptions

The following assumptions reflect the first round of stakeholder answers and should be treated as the baseline for the next revision:

1. The initial system is for a single user.
2. The MVP should offer both a CLI and a local web UI.
3. The first version should focus on recurring bills only.
4. Stored financial detail should be limited, but may include more than minimal metadata when needed for useful operation.
5. Initial deployment should be local-only rather than remotely exposed.
6. MCP should eventually support carefully scoped write actions, not only read-only queries.
7. Email reminders are the first notification channel worth designing for.
8. The likely implementation direction is containerized services with SQLite storage on a home server and enough logging to troubleshoot operations safely.

## Problem Statement

BillMinder is a personal system for tracking upcoming and projected bills, helping ensure obligations are paid on time, and giving forward visibility into expected cash flow. The project should also serve as a practical learning vehicle for:

- Model Context Protocol (MCP)
- Distributed systems across multiple home servers and remote clients
- Secure handling of highly sensitive personal financial metadata

The system must be designed so that source code can live in a public repository while operational data, credentials, account details, and bill records remain private and difficult to expose accidentally.

## Goals

1. Provide a dependable personal workflow for tracking recurring, one-time, and projected bills.
2. Highlight bills that are approaching due dates or are at risk of being missed.
3. Project future obligations over configurable time windows such as 7, 30, 60, and 90 days.
4. Support manual entry first, with room for future integrations.
5. Demonstrate MCP usage in a way that is meaningful rather than artificial.
6. Illustrate a distributed architecture with components that can run on different home servers and be accessed by clients from different locations.
7. Make privacy and operational security first-class design constraints.
8. Reduce the chance of accidental disclosure through repository layout, local storage boundaries, secret handling, and safe defaults.

## Non-Goals For The First Iteration

1. Direct bank integration or screen scraping.
2. Automatic bill payment.
3. Multi-user household accounting unless explicitly added later.
4. One-time bill tracking in the first implementation slice.
5. Full budgeting, investment tracking, or tax reporting.
6. Mobile-native apps.
7. Complex AI forecasting beyond simple scheduled projections.

## Primary Users

The initial target is a single primary user managing personal or household bills. Future remote or secondary access may exist, but the default trust model should remain personal, local-first, and least-privilege.

## High-Level Requirements

### Functional Requirements

1. The system must let the user create, edit, archive, and categorize bills.
2. The system must support recurring bills with schedules such as monthly, weekly, quarterly, annually, and custom intervals.
3. The first implementation may defer one-time obligations until recurring-bill workflows are proven useful.
4. The system must track at least these fields per bill:
   - name
   - amount or expected amount range
   - due date or recurrence rule
   - payee
   - status such as upcoming, paid, overdue, skipped, uncertain
   - optional notes
5. The system must allow projecting future bills that have not yet been issued but are expected based on prior patterns or user-defined recurrence rules.
6. The system must provide views of:
   - bills due soon
   - overdue bills
   - projected obligations over a future horizon
   - payment history
7. The system must allow marking a bill as paid and recording when it was paid.
8. The system should support reminders or alerts, with email as the first planned notification path.
9. The system should support importing and exporting data in a human-readable format.
10. The system should preserve an audit-friendly history of changes or at least payment events.

### Distributed System Requirements

1. The system should separate responsibilities into deployable components rather than a single monolith, but only where that separation adds clarity or learning value.
2. A trusted home server should be able to host always-on services such as storage, scheduling, and notification.
3. The first deployment should work fully in a local-only mode.
4. Any later remote-client support should be added without changing the local source-of-truth model.
5. The design should tolerate intermittent connectivity for future clients.
6. The design should favor simple synchronization and strong ownership boundaries over complex distributed consensus.

### MCP Learning Requirements

1. The project should expose at least one meaningful MCP server capability, such as bill lookup, due-soon summaries, projection generation, or controlled payment-status updates.
2. MCP usage should be additive to the core product, not the only interface.
3. The design should make clear which actions are safe for MCP clients and which require stronger trust, confirmation, or audit capture.
4. MCP-facing tools should avoid returning secrets or unnecessary personal financial detail by default.
5. Any MCP write action should be idempotent where possible and leave an auditable event trail.

### Security And Privacy Requirements

1. Sensitive operational data must be stored outside the public repository by default.
2. Secrets, tokens, keys, and local datasets must have an explicit private storage path and must be excluded from version control.
3. The project must use deny-by-default handling for sensitive files, with repository examples or templates instead of real data.
4. The system must minimize stored financial detail to only what is needed for bill tracking.
5. Encryption at rest should be considered for persisted bill data or backups.
6. Encryption in transit is required for any remote access.
7. Authentication is required for every non-local privileged operation.
8. Authorization should distinguish read-only access, bill updates, administrative configuration, and secret management.
9. Logging must be detailed enough for troubleshooting but must avoid leaking account numbers, credentials, or unnecessary bill detail.
10. Backup and recovery should be designed from the start, including safe restore procedures.
11. The design must include guardrails to reduce accidental git commits of private data.

## Design Drivers

The following drivers should shape the architecture before detailed design begins:

1. Local-first ownership: the user owns the source of truth.
2. Public-code, private-data separation: the repo can be public, the data cannot.
3. Incremental delivery: manual workflows should work before automation or integrations.
4. Simplicity over novelty: distributed boundaries should be educational but justified.
5. Safe defaults: the easiest path should also be the safest operational path.
6. Operational transparency: logs and artifacts should help explain system behavior without exposing sensitive data.

## Candidate High-Level Architecture Direction

This is not yet a final design. It is a working direction to validate against the requirements.

### Core Components

1. A local or home-server data service that owns the source-of-truth bill store, likely backed by SQLite.
2. A scheduler or projection service that computes upcoming obligations and reminders.
3. An MCP server layer that exposes safe query and action tools to authorized clients.
4. Two first-class user clients: a CLI and a local web UI.
5. A notification adapter layer, with email as the first concrete channel.

### Responsibility Boundaries

1. Storage component:
   - Persists bills, schedules, payment events, and configuration.
   - Applies encryption and backup policies.
2. Projection component:
   - Expands recurrence rules into future expected obligations.
   - Flags due-soon and overdue items.
3. MCP component:
   - Offers constrained tools for querying summaries and optionally making state changes.
   - Enforces authentication, authorization, and output filtering.
4. Client component:
   - Presents the user workflow.
   - Avoids direct access to raw storage when possible.

### Deployment Direction

1. Start with a single trusted deployment on one home server or personal machine.
2. Package the system so it can run cleanly in containers.
3. Keep the first deployment local-only.
4. Add any remote client path only after the trust, authentication, and backup models are clear.
5. If remote access is later introduced, prefer secure tunnels, VPN, or mutually authenticated private network access over public exposure.

## Data Sensitivity Model

Potentially sensitive data includes:

1. Bill names when they reveal providers or account relationships.
2. Payees, due dates, amounts, and payment history.
3. Account identifiers, login credentials, tokens, and message delivery credentials.
4. Notification destinations if they reveal personal contact information.

Recommended high-level handling:

1. Keep real data under a private directory outside the repository root.
2. Store only templates, sample configs, and schema definitions in the repository.
3. Use environment-specific configuration files that are ignored by git.
4. Add repository guardrails such as .gitignore rules, pre-commit checks, and startup warnings when sensitive paths are misconfigured.
5. Treat SQLite database files, backups, exported reports, and mail-delivery credentials as private operational assets.

## Incremental Delivery Plan

### Phase 0: Requirements And Threat Model

1. Finalize scope, trust boundaries, and the minimum bill-tracking workflow.
2. Define what data will and will not be stored.
3. Define the private-data directory and repository safety rules.
4. Define what troubleshooting logs are required and what redaction rules apply.

### Phase 1: Local-First MVP

1. Manual bill entry and recurring schedule support.
2. Due-soon and projected-bills views.
3. Mark-as-paid workflow.
4. Local-only private storage.
5. CLI and local web UI.
6. Exportable reports.
7. Email reminder design, with implementation optional if it would delay the core workflow.

### Phase 2: Secure Service Boundary

1. Move source-of-truth storage behind a service boundary if Phase 1 proves the workflow.
2. Add authentication and encrypted transport.
3. Introduce one non-local client path only if it adds real value.

### Phase 3: MCP Integration

1. Expose read-focused MCP tools first if that simplifies rollout.
2. Add carefully scoped mutation tools, including payment-status updates, only with trust checks and audit behavior.
3. Evaluate whether MCP is best used for user assistance, automation, or both.

### Phase 4: Distributed Operation

1. Run supporting services on different home servers only if there is a clear operational reason.
2. Add synchronization, failover, or backup automation as justified by actual usage.

## Key Risks

1. Over-designing the distributed architecture before the local workflow is proven useful.
2. Treating MCP as the product instead of as one interface into the product.
3. Storing more personal financial detail than necessary.
4. Creating convenience features that weaken privacy or secret isolation.
5. Exposing remote access without a simple, testable trust model.
6. Allowing verbose logs or reports to become a secondary data leak path.

## Decisions To Make Before Detailed Design

1. What exact bill fields are allowed beyond minimal metadata, and which are explicitly forbidden?
2. Should projections be deterministic recurrence-based only, or include estimated variable amounts?
3. What private storage location and backup approach should be standard?
4. Which MCP write actions are acceptable in the first release beyond marking a bill as paid?
5. How should email delivery credentials be stored, rotated, and tested safely?

## Follow-Up Questions

1. Which additional stored fields are acceptable beyond bill name, recurrence, amount, due date, and paid status? For example: payment method nickname, autopay flag, statement close date, grace period, provider contact info.
2. Should projected amounts stay fixed per recurrence, or should the design allow estimated ranges for bills that vary month to month?
3. Do you want the local web UI to be strictly local-admin, or should the design anticipate a read-only dashboard mode later?
4. Is email reminder delivery expected to use an external SMTP provider, a local mail relay, or is that still open?
5. For containerization, do you want a single compose-style deployment first, or should the architecture target a more distributed multi-host setup from the beginning?

## Next Step

After the clarifying questions are answered, this document should be revised into:

1. A confirmed high-level requirements document.
2. A threat model and privacy model.
3. A more concrete architecture decision record for the first implementation slice.
