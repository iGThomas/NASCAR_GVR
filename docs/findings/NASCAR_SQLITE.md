# NASCAR on SQLite — replacing MSDE / SQL Server 2000

**Status: built and functionally verified (2026-08-12).** The cabinet stack runs against
an embedded SQLite file; no SQL Server, MSDE, or database service is installed or started.

This is the NASCAR port of the parent project's NFSU SQLite work. The *technique* is the
same; every artifact had to be re-derived because NASCAR ships a different 2008 build.

---

## How it works

`PLUSDE.dll` — the GvrPlus data engine the Anark shell talks to — is a **mixed-mode C++/CLI
assembly**. Its database access is managed code using `System.Data.SqlClient`:

```
gcroot<System::Data::SqlClient::SqlConnection __gc *>
gcroot<System::Data::SqlClient::SqlCommand    __gc *>
gcroot<System::Data::SqlClient::SqlDataAdapter __gc *>
```

That means the SQL Server dependency lives entirely in **metadata type references**, not in
native code. So we do not rewrite a single instruction:

1. **`GvrSqlite.dll`** — a small provider that mirrors exactly the SqlClient member surface
   PLUSDE calls (`GvrConnection`, `GvrCommand`, `GvrDataAdapter`, `GvrParameter`,
   `GvrParameterCollection`, `GvrError`, `GvrErrorCollection`, `GvrException`) and P/Invokes
   `sqlite3.dll`. It is *not* a real ADO.NET provider — it is a flat mirror of the 37 members
   this game actually uses.
2. **A dnlib typeref swap** rewrites PLUSDE's 10 `System.Data.SqlClient.*` /
   `System.Data.Common.*` type references to point at `GvrSqlite` instead. The image is
   written back with dnlib's **native** writer so the C++ half survives intact (verified:
   native imports `KERNEL32/mscoree/WINMM` unchanged, +4 KB of metadata).
3. **`game.db`** is generated from the OEM schema — the same 106 tables, seeded with the
   same content the cabinet's `Nascar_db.exe` would have loaded.

`System.Data.DataSet` / `DataTable` / `DataRow` are **not** swapped: those are ordinary
framework types and the game passes them around normally. Only the SQL-Server-specific types
are redirected.

## Where the schema came from

The OEM ships the schema encrypted (`NASCARcabinet.enc`, `NASCARcabinet_Content.enc`,
`NASCARcabinetXml.enc` — Base64 over AES-128-CBC, PKCS#7). **GlobalVR reused the same
key/IV across titles**, so the NFSU derivation works unchanged — verified on NASCAR's 2008
media. `Tools\Decrypt-GvrEnc.ps1` does it with nothing but .NET's built-in AES:

| File | Decrypted | Contents |
|---|---|---|
| `NASCARcabinet.enc` | 1,065,970 B | 106 `CREATE TABLE` + 716 stored procs + `GlobalVariable` seeds |
| `NASCARcabinet_Content.enc` | 64,218 B | 366 seed rows (tracks, drivers, lookups, cabinet config) |
| `NASCARcabinetXml.enc` | 457,403 B | object/XML mapping + the SQL Server connection string |

Note the connection string never has to be re-encrypted (unlike the NFSU SQL-Express
repoint route): the provider ignores it and resolves the database path itself.

## The stored-proc surface

All 716 procs are generated from 8 templates — `Insert`, `Get`, `GetSet`, `Update`,
`Delete`, `InsertOrUpdate` (106 each) plus `InsertDepth` / `UpdateDepth` (40 each). The
translator in `GvrSqlite.cs` maps them to SQLite:

| Proc | SQLite |
|---|---|
| `SP_InsertOrUpdate_<T>` / `SP_Insert_<T>` | `INSERT OR REPLACE INTO [T] (...) VALUES (...)` |
| `SP_Update_<T>` | `UPDATE [T] SET ... WHERE pk=?` (values reordered) |
| `SP_Delete_<T>` | `DELETE FROM [T] WHERE pk=?` |
| `SP_Get_<T>` | `SELECT * FROM [T] WHERE pk=?` |
| `SP_GetSet_<T>` | `SELECT * FROM [T]` |
| `SP_InsertDepth_<T>` / `SP_UpdateDepth_<T>` | primary row upserted; nested rows logged and skipped |

**Verified** that every proc declares its parameters in the same order as its table's DDL
columns, which is what makes positional mapping safe.

`InsertDepth`/`UpdateDepth` are multi-table cascades whose parameters are named
`<Table>_<Col>` for the primary row and `<OtherTable>_<FkCol>_<Col>` for joined rows. Those
nested writes belong to the **propagation engine** (cabinet → GlobalVR server sync), which a
standalone cabinet never runs, so the provider writes the primary row and logs what it
skipped rather than blindly upserting prefixed column names that are not columns.

## Provisioning (what differs from a stock cabinet)

`Tools\Build-NascarSqliteDb.py` builds and provisions the database:

| Step | Why |
|---|---|
| 106 tables + `_gvrmeta` type map | `_gvrmeta` records the original SQL Server column types so the provider can type `DataColumn`s correctly (`bigint`→Int64, `datetime`→DateTime, `bit`→bool) |
| 366 seed rows from `_Content` | tracks, drivers, event messages, lookups, the cabinet config row |
| 32 `GlobalVariable` rows | these live in the **schema** file, not the content file — miss them and the shell has no row to read *or update* (the ADO.NET Fill→modify→Update pattern needs an existing DataRow) |
| unique index on `GlobalVariableId` | the OEM DDL gives this table no primary key (the upsert lived inside the stored proc); without the index `INSERT OR REPLACE` appends duplicates and the shell reads a stale first row |
| operator pricing from 9 `a+*.tbl` files | the Plus pricing lookup finds nothing otherwise and the frontend spins in its pricing-init retry loop |
| 360 leaderboard rows + collections from 7 `Nascar*.tbl` | what `Nascar_db.exe`/`NascarLeaderboard.sql` bulk-load; otherwise the attract leaderboards are blank |
| synthesized `GvrCabinet[0]` + `CabinetBilling[0]` | so the cabinet resolves as registered with a valid pricing model |
| **`CabinetConfiguration_NAS1.FreePlay = 1`** | the single deliberate deviation from OEM data: with no coin mech the shell would ask for credits it can never receive. It stays a normal operator setting — press **O** in the shell to turn it back off |

## Build it

```powershell
cd D:\NFSU_GVR\NASCAR
.\Tools\Build-SqliteBackend.ps1
```

Which runs the whole chain: decrypt schema → build `game.db` → compile the provider →
patch `PLUSDE.dll` → stage `Deploy\` → run the functional test.

This machine has the **real .NET 1.1 compiler** (`C:\Windows\Microsoft.NET\Framework\v1.1.4322\csc.exe`),
so the provider is compiled directly for the runtime the game uses. On a box without it the
script falls back to the 2.0 compiler plus a dnlib metadata retarget to `v1.1.4322` — the
route the NFSU project had to take.

Output in `NASCAR\Deploy\`:

| File | Size |
|---|---|
| `PLUSDE.dll` (patched) | 1,814,528 B |
| `GvrSqlite.dll` (.NET 1.1) | 36,864 B |
| `sqlite3.dll` (x86) | 2,577,408 B |
| `game.db` | 610,304 B |

## Install it

```powershell
.\Install-NASCAR-GVR-Portable.ps1
```

That stages `C:\GvrPlus`, drops the three DLLs beside both `NASCAR_GVR.exe` and
`AMPlayer.exe` (PLUSDE is not strong-named and is found by plain DLL search order — the OEM
copy is preserved as `PLUSDE.dll.oem`), installs `game.db` to `C:\GvrPlus\game.db`, and sets
the machine environment variable **`GVRSQLITE_DB_NAS1`**.

An existing `game.db` is never overwritten without `-ForceOverwrite` — it holds the
leaderboards and operator settings.

> **Log off and back on after installing.** Processes inherit environment variables from the
> Explorer session that launched them; a shell started from a pre-existing Explorer will not
> see `GVRSQLITE_DB_NAS1`. This exact trap cost the NFSU project a debugging session where a
> working install looked like a hang.

### Why a per-title environment variable

The provider resolves the database in this order:

1. `GVRSQLITE_DB_NAS1` (NASCAR-specific)
2. `GVRSQLITE_DB` (what the NFSU install sets)
3. `HKLM\SOFTWARE\Gvr\Plus\1.1\Cabinet\PlusSchemaPath` → `<GvrPlus>\game.db`
4. `C:\GvrPlus\game.db`, then `game.db` beside `GvrSqlite.dll`

Step 1 exists because `GVRSQLITE_DB` is machine-wide: on this development box it already
points at `D:\Games\NFSU\Underground\GVR\GvrPlus\game.db`, and without the per-title
variable a NASCAR install would silently open NFSU's database (it did, during testing —
`no such table: Tracks_NAS1`). With it, both titles coexist.

## Verification

`Tools\Test-GvrSqlite.ps1` compiles a harness with the 1.1 compiler and runs it against a
copy of the built database, exercising the same call shapes PLUSDE uses. Current result —
all 13 checks pass:

```
PASS  connection opens                                    PASS  GlobalVariable upsert round-trips
PASS  ExecuteScalar COUNT(*) Tracks_NAS1        [got 28]  PASS  GlobalVariable upsert did not duplicate the row
PASS  CommandTimeout accepted                             PASS  InsertDepth writes the primary row
PASS  SP_Get_CabinetConfiguration_NAS1 returns the row    PASS  GvrException.Message populated
PASS  FreePlay provisioned to 1                           PASS  GvrException.Errors[0].Message populated
PASS  SP_GetSet_Drivers_NAS1        [12 rows]             PASS  connection closes
PASS  SP_GetSet_CabinetLeaderboard_NAS1  [120 rows]
```

The patcher also runs its own **pre-flight**: it enumerates every member PLUSDE references
on the swapped types (37 distinct) and refuses to patch unless the provider implements all
of them. NASCAR's 2008 build needed four members NFSU's never used —
`GvrCommand.ExecuteScalar`, `GvrCommand.CommandTimeout`, `GvrError.Message`,
`GvrException.Message` — all added.

## Known boundaries

* **`GvrPeEngine.dll` and `GvrPeClient.dll` still use SqlClient** (including
  `SqlDataReader`, which the provider does not implement). These are the *propagation
  engine* — cabinet-to-GlobalVR-server sync over dial-up/ISP, against servers that no longer
  exist. Nothing in a standalone install should reach them. If something does, it will throw
  a normal .NET type/connection error rather than fail silently; patching them would mean
  implementing `SqlDataReader` too.
* **Untranslated T-SQL fails loudly, not silently.** That is deliberate: the NFSU project
  lost time to a `SELECT TOP n` that silently returned zero rows and made every frontend car
  render white. Set `GVRSQLITE_LOG=1` to log every statement the provider executes — that is
  how you find the next dialect gap.
* This is the database layer only. It does **not** address the dongle check that currently
  stops `NASCAR_GVR.exe` at startup (`NASCAR_FINDINGS.md` §5).

## Files

| Path | What |
|---|---|
| `Tools\Build-SqliteBackend.ps1` | one-shot: decrypt → build db → compile → patch → stage → test |
| `Tools\Decrypt-GvrEnc.ps1` | AES-128-CBC decryptor for GvrPlus `.enc` files |
| `Tools\Build-NascarSqliteDb.py` | schema → SQLite, seeding and provisioning |
| `Tools\Patch-PlusdeToSqlite.ps1` | dnlib typeref swap + pre-flight + verify |
| `Tools\Test-GvrSqlite.ps1`, `TestGvrSqlite.cs` | functional test |
| `..\GvrSqlite\GvrSqlite.cs` | the provider source, shared with the NFSU project |
| `Deploy\` | the four files the installer consumes |
