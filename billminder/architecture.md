# BillMinder Architecture

## Overview Diagram of Components

```mermaid
graph TD
    UI[Web UI (Vanilla JS/HTML/CSS)] -->|REST API| Server[Core Service (C++)]
    CLI[CLI Client (C++)] -->|REST API| Server
    MCP[MCP Server Layer] -->|Constrained API| Server
    
    subgraph Backend
        Server --> DB[(SQLite Database)]
        Server --> Projection[Projection / Rollover Engine]
        Server --> Notifier[Notification Engine]
    end
```

## Core Components

1. **Core Service (C++)**: Owns the source-of-truth bill store. Exposes a shared REST/HTTP API.
2. **Scheduler / Projection Service**: Computes upcoming obligations, parses recurrence rules, and handles automatic background rollovers for past-due bills.
3. **MCP Server Layer**: Exposes safe query and action tools to authorized clients.
4. **Clients**: Two first-class user clients: a CLI tool (C++) and a local-admin Web UI (Vanilla JS/HTML/CSS).
5. **Notification Adapter**: Background thread handling scheduled alerts and email reminders.

## Responsibility Boundaries

- **Storage**: Persists bills, schedules, payment events, and configuration. Applies XOR encryption to sensitive template metadata (e.g., passwords).
- **Projection**: Expands recurrence rules into future expected obligations. Handles automatic instance spawning immediately when bills are marked as paid or past-due.
- **MCP**: Offers constrained tools for querying summaries and making safe state changes. Enforces authentication, authorization, and output filtering.
- **Client**: Presents the user workflow using a Single Page App (SPA) design and avoids direct access to raw storage.

## Database Schema

```sql
CREATE TABLE bills (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    default_amount REAL NOT NULL,
    recurrence_rule TEXT NOT NULL,   -- e.g., monthly, quarterly, none
    payee TEXT,
    url TEXT,
    account TEXT,
    password TEXT,                   -- Stored using XOR encryption
    notes TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE bill_instances (
    id TEXT PRIMARY KEY,
    bill_id TEXT NOT NULL,           -- Foreign key to bills.id
    due_date TEXT NOT NULL,          -- Format: YYYY-MM-DD
    amount_expected REAL NOT NULL,
    status TEXT NOT NULL,            -- upcoming, paid, overdue, skipped
    notes TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    FOREIGN KEY(bill_id) REFERENCES bills(id)
);

CREATE TABLE payment_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bill_instance_id TEXT NOT NULL,
    amount_paid REAL NOT NULL,
    payment_date TEXT NOT NULL,
    notes TEXT,
    FOREIGN KEY(bill_instance_id) REFERENCES bill_instances(id)
);
```

## Interface Specifications (REST API)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET`  | `/api/bills` | Fetches recurring bill templates. |
| `POST` | `/api/bills` | Creates a new recurring bill template and its first instance. |
| `GET`  | `/api/bills/:id` | Fetches a single bill template and its metadata. |
| `PUT`  | `/api/bills/:id` | Updates a bill template (e.g., metadata url/account/password). |
| `DELETE` | `/api/bills/:id` | Cascading delete of a bill template and all its instances/history. |
| `GET`  | `/api/bills/:id/instances` | Fetches all instances belonging to a specific bill template. |
| `GET`  | `/api/instances` | Fetches active bill instances. Triggers background rollover logic. |
| `GET`  | `/api/instances/:id` | Fetches a single bill instance by its unique ID. |
| `PUT`  | `/api/instances/:id` | Updates a specific bill instance (e.g., amount_expected, due_date). |
| `DELETE` | `/api/instances/:id` | Deletes a bill instance. |
| `POST` | `/api/instances/:id/pay` | Records a payment in the history table, updates the instance status to `paid`, and spawns the next instance. |

## Views

1. **Active Bills Dashboard**: Default SPA view displaying bill templates grouped by their active instances, sorted sequentially by due date. Provides top-level actions like Mark Paid and Delete Bill.
2. **Bill Details View**: Granular view for a specific bill. Displays and allows editing of sensitive metadata (URL, Account, Password) and provides a list of all historical and upcoming instances for that bill.
3. **Projected Obligations**: Visualizes expected bills over configurable future time horizons (e.g. 7, 30, 90 days).
4. **Payment History**: Detailed audit trail of past payments for reconciliation.

## Deployment Direction

1. Start with a single compose-style deployment on one home server or personal machine.
2. Package the core service, database mount, and web UI to run cleanly in containers.
3. Keep the first deployment local-only.
4. Add any remote client path only after the trust, authentication, and backup models are clear.
