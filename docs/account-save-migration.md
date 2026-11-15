# Account save migration

**Stop all game servers before migrating.** Back up the primary and all
relevant replicas, and verify restoration before changing data. Deploying this
version without the required unique indexes intentionally prevents startup.
Do not deploy mixed old and new game-server versions: old writers and session
handover do not observe the new save guarantees.

The old schema allows multiple inventory and profession snapshots for the
same account. They are not additive. Never sum values or automatically keep
only the greatest ID/value: concurrent saves, legitimate consumption and
replica lag make those choices unsafe. Preserve every row externally and
reconcile affected accounts against verified backups, binary logs and player
reports. If the correct snapshot is unknown, obtain an operator decision.

Run the following on the authoritative shared primary to find duplicates:

```sql
SELECT UserID, ItemID, COUNT(*) AS snapshots
FROM tw_accounts_items GROUP BY UserID, ItemID HAVING COUNT(*) > 1;
SELECT UserID, ProfessionID, COUNT(*) AS snapshots
FROM tw_accounts_professions GROUP BY UserID, ProfessionID HAVING COUNT(*) > 1;
```

After operator-reviewed reconciliation leaves at most one row per key, add
any missing unique indexes (do not add a redundant equivalent index):

```sql
ALTER TABLE tw_accounts_items ADD UNIQUE KEY uq_account_item (UserID, ItemID);
ALTER TABLE tw_accounts_professions ADD UNIQUE KEY uq_account_profession (UserID, ProfessionID);
```

The ALTER statements fail if duplicates remain; they do not resolve them.
Verify indexes on the primary and wait for replicas to apply the DDL before
restarting servers. Account inventory and profession reads require an updated
bridge that understands `/*mrpg:primary*/`, or a direct primary connection.
Backups are still required: no code change recovers an already overwritten row.

## Session availability

Configure a distinct, nonzero `sv_sync_server_id` for every server and enable
the relay before allowing cross-server logins. A takeover waits for the old
server to disconnect the account and finish the tracked inventory and
profession saves. If the relay cannot confirm this, a save fails, or an old
reservation has no server ID, login fails closed instead of risking rollback.
Reservations are not stolen merely because their heartbeat is old. This means
a crashed server or failed save may require operator investigation and manual
session-row recovery **after** confirming the old process is stopped and its
outstanding data has been recovered. Do not blindly clear session rows.

Existing data in the primary may already have been overwritten; retrying
failed SQL or clearing a session does not recover it. A sudden process or host
crash can still lose writes that were not durably committed. Consult the failed
query log and verified backups; it can contain sensitive account information.

### Reload and restart recovery

During an orderly world reload, pending reservations are released after local
tracked writes settle; if a save fails, the reservation remains for operator
recovery. If the database is unavailable during teardown, release itself can
fail and still require manual recovery. Callbacks associated with the old world
are invalidated. In-flight SQL work is not made durable by a world reload.

The server keeps process-local proof of the session tokens it activated. It
refuses a local takeover or remote acknowledgement for an unknown owner after a
process restart, even if its server ID matches. This is intentional: a restart
forgets pending and failed writes. Investigate the old process and recover the
account's data before manually clearing or repairing its session row. Configure
distinct server IDs and a single relay for all servers; this process-local check
is not a substitute for a database fencing token and cannot guarantee safety
under misconfiguration, split-brain deployments, or stale writers outside this
protocol. A hard crash can still lose writes not committed to the database.
