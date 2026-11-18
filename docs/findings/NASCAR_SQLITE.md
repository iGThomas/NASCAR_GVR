# NASCAR on SQLite — replacing MSDE / SQL Server 2000

**Status: built and functionally verified (2026-08-12).** The cabinet stack runs against an embedded SQLite file. No SQL Server, MSDE or database service is installed or started.

The technique swaps the database layer underneath the game's own code. Every artifact is derived from NASCAR's own 2008 build: the provider surface, the patch and the database.

---

## How it works

`PLUSDE.dll` is the GvrPlus data engine the Anark shell talks to. It is a **mixed-mode C++/CLI assembly**. Its database access is managed code using `System.Data.SqlClient`:

```
gcroot<System::Data::SqlClient::SqlConnection __gc *>
gcroot<System::Data::SqlClient::SqlCommand    __gc *>
gcroot<System::Data::SqlClient::SqlDataAdapter __gc *>
```

So the SQL Server dependency lives entirely in metadata type references, not in native code. Not a single instruction is rewritten. The replacement has three parts:

1. `GvrSqlite.dll` is a small provider that P/Invokes `sqlite3.dll`. It mirrors exactly the SqlClient member surface PLUSDE calls: `GvrConnection`, `GvrCommand`, `GvrDataAdapter`, `GvrParameter`, `GvrParameterCollection`, `GvrError`, `GvrErrorCollection`, `GvrException`. It is *not* a real ADO.NET provider. It is a flat mirror of the 37 members this game actually uses.
2. A dnlib typeref swap points PLUSDE's 10 `System.Data.SqlClient.*` / `System.Data.Common.*` type references at `GvrSqlite` instead. The image is written back with dnlib's native writer, so the C++ half survives intact. Verified: the native imports `KERNEL32/mscoree/WINMM` are unchanged, and metadata grows by +4 KB.
3. `game.db` is generated from the OEM schema. It has the same 106 tables, seeded with the same content the cabinet's `Nascar_db.exe` would have loaded.

`System.Data.DataSet` / `DataTable` / `DataRow` are not swapped. They are ordinary framework types, and the game passes them around normally. Only the SQL-Server-specific types are redirected.

## Where the schema came from

The OEM ships the schema encrypted in `NASCARcabinet.enc`, `NASCARcabinet_Content.enc` and `NASCARcabinetXml.enc`. The format is Base64 over AES-128-CBC, PKCS#7.

GlobalVR reused the same key/IV across titles, and it works unchanged on NASCAR's 2008 media. `Tools\Decrypt-GvrEnc.ps1` decrypts the files with nothing but .NET's built-in AES:

| File | Decrypted | Contents |
|---|---|---|
| `NASCARcabinet.enc` | 1,065,970 B | 106 `CREATE TABLE` + 716 stored procs + `GlobalVariable` seeds |
| `NASCARcabinet_Content.enc` | 64,218 B | 366 seed rows: tracks, drivers, lookups, cabinet config |
| `NASCARcabinetXml.enc` | 457,403 B | object/XML mapping + the SQL Server connection string |

The connection string never has to be re-encrypted. The provider ignores it and finds the database path itself.

## The stored-proc surface

All 716 procs come from 8 templates. `Insert`, `Get`, `GetSet`, `Update`, `Delete` and `InsertOrUpdate` give 106 each. `InsertDepth` / `UpdateDepth` give 40 each. The translator in `GvrSqlite.cs` maps them to SQLite:

| Proc | SQLite |
|---|---|
| `SP_InsertOrUpdate_<T>` / `SP_Insert_<T>` | `INSERT OR REPLACE INTO [T] (...) VALUES (...)` |
| `SP_Update_<T>` | `UPDATE [T] SET ... WHERE pk=?` (values reordered) |
| `SP_Delete_<T>` | `DELETE FROM [T] WHERE pk=?` |
| `SP_Get_<T>` | `SELECT * FROM [T] WHERE pk=?` |
| `SP_GetSet_<T>` | `SELECT * FROM [T]` |
| `SP_InsertDepth_<T>` / `SP_UpdateDepth_<T>` | primary row upserted. Nested rows logged and skipped. |

**Verified:** every proc declares its parameters in the same order as its table's DDL columns. That is what makes positional mapping safe.

`InsertDepth`/`UpdateDepth` are multi-table cascades. Their parameters are named `<Table>_<Col>` for the primary row and `<OtherTable>_<FkCol>_<Col>` for joined rows.

Those nested writes belong to the propagation engine, which syncs from the cabinet to the GlobalVR server. A standalone cabinet never runs it. So the provider writes the primary row and logs what it skipped. It does not blindly upsert prefixed column names that are not columns.

## Provisioning (what differs from a stock cabinet)

`Tools\Build-NascarSqliteDb.py` builds and provisions the database:

| Step | Why |
|---|---|
| 106 tables + `_gvrmeta` type map | `_gvrmeta` records the original SQL Server column types. The provider uses them to type `DataColumn`s correctly: `bigint` to Int64, `datetime` to DateTime, `bit` to bool. |
| 366 seed rows from `_Content` | tracks, drivers, event messages, lookups, the cabinet config row |
| 32 `GlobalVariable` rows | These live in the schema file, not the content file. Without them the shell has no row to read *or update*. The ADO.NET pattern of Fill, modify, then Update needs an existing DataRow. |
| unique index on `GlobalVariableId` | The OEM DDL gives this table no primary key. The upsert lived inside the stored proc. Without the index, `INSERT OR REPLACE` appends duplicates and the shell reads a stale first row. |
| operator pricing from 9 `a+*.tbl` files | Without it the Plus pricing lookup finds nothing. The frontend then spins in its pricing-init retry loop. |
| 360 leaderboard rows + collections from 7 `Nascar*.tbl` | This is what `Nascar_db.exe`/`NascarLeaderboard.sql` bulk-load. Without it the attract leaderboards are blank. |
| synthesized `GvrCabinet[0]` + `CabinetBilling[0]` | The cabinet then resolves as registered, with a valid pricing model. |
| `CabinetConfiguration_NAS1.FreePlay = 1` | The single deliberate change from OEM data. With no coin mech, the shell would ask for credits it can never receive. It stays a normal operator setting: press **O** in the shell to turn it back off. |

## Build it

```powershell
cd <the repo folder>
.\Tools\Build-SqliteBackend.ps1
```

The script runs the whole chain:

1. Decrypt the schema.
2. Build `game.db`.
3. Compile the provider.
4. Patch `PLUSDE.dll`.
5. Stage `Deploy\`.
6. Run the functional test.

The script prefers the **real .NET 1.1 compiler** (`C:\Windows\Microsoft.NET\Framework\v1.1.4322\csc.exe`). When it is present, the provider is compiled directly for the runtime the game uses. Without it, the script falls back to the 2.0 compiler plus a dnlib metadata retarget to `v1.1.4322`.

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

The installer does four things:

* It stages `C:\GvrPlus`.
* It puts the three DLLs beside both `NASCAR_GVR.exe` and `AMPlayer.exe`. PLUSDE is not strong-named and is found by plain DLL search order. The OEM copy is kept as `PLUSDE.dll.oem`.
* It installs `game.db` to `C:\GvrPlus\game.db`.
* It sets the machine environment variable `GVRSQLITE_DB_NAS1`.

An existing `game.db` is never overwritten without `-ForceOverwrite`. It holds the leaderboards and operator settings.

> **Log off and back on after installing.** Processes inherit environment variables from the
> Explorer session that started them. A shell started from an Explorer that was already running
> will not see `GVRSQLITE_DB_NAS1`. A working install then looks like a hang.

### Why a per-title environment variable

The provider looks for the database in this order:

1. `GVRSQLITE_DB_NAS1` (NASCAR-specific)
2. `GVRSQLITE_DB` (the generic variable, which another title's install may set)
3. `HKLM\SOFTWARE\Gvr\Plus\1.1\Cabinet\PlusSchemaPath`, giving `<GvrPlus>\game.db`
4. `C:\GvrPlus\game.db`, then `game.db` beside `GvrSqlite.dll`

Step 1 exists because `GVRSQLITE_DB` is machine-wide. Another GlobalVR title on the same PC may have set it to its own `game.db`. A NASCAR install without the per-title variable then silently opens that title's database. The symptom is `no such table: Tracks_NAS1`.

With the per-title variable, both titles coexist.

## Verification

`Tools\Test-GvrSqlite.ps1` compiles a test harness with the 1.1 compiler. It runs the harness against a copy of the built database, using the same call shapes PLUSDE uses. All 13 checks pass in the current result:

```
PASS  connection opens                                    PASS  GlobalVariable upsert round-trips
PASS  ExecuteScalar COUNT(*) Tracks_NAS1        [got 28]  PASS  GlobalVariable upsert did not duplicate the row
PASS  CommandTimeout accepted                             PASS  InsertDepth writes the primary row
PASS  SP_Get_CabinetConfiguration_NAS1 returns the row    PASS  GvrException.Message populated
PASS  FreePlay provisioned to 1                           PASS  GvrException.Errors[0].Message populated
PASS  SP_GetSet_Drivers_NAS1        [12 rows]             PASS  connection closes
PASS  SP_GetSet_CabinetLeaderboard_NAS1  [120 rows]
```

The patcher also runs its own **pre-flight** check. It lists every member PLUSDE references on the swapped types, 37 distinct ones. It refuses to patch unless the provider implements all of them.

Four of those members are specific to NASCAR's 2008 build, and all are implemented: `GvrCommand.ExecuteScalar`, `GvrCommand.CommandTimeout`, `GvrError.Message`, `GvrException.Message`.

## Known boundaries

* **`GvrPeEngine.dll` and `GvrPeClient.dll` still use SqlClient.** That includes `SqlDataReader`, which the provider does not implement. These two DLLs are the *propagation engine*. It syncs from the cabinet to the GlobalVR servers over dial-up/ISP, and those servers no longer exist. Nothing in a standalone install should reach them. If something does, it throws a normal .NET type/connection error and does not fail silently. Patching them would mean implementing `SqlDataReader` too.
* Untranslated T-SQL fails loudly, not silently. That is deliberate. A statement that silently returns zero rows shows up as an unrelated frontend symptom instead of a database error. An untranslated `SELECT TOP n` is one example. Set `GVRSQLITE_LOG=1` to log every statement the provider runs. That log is how to find the next dialect gap.
* This is the database layer only. It does not address the dongle check that currently stops `NASCAR_GVR.exe` at startup (`NASCAR_FINDINGS.md` §5).

## Files

| Path | What |
|---|---|
| `Tools\Build-SqliteBackend.ps1` | one run for the whole chain: decrypt, build db, compile, patch, stage, test |
| `Tools\Decrypt-GvrEnc.ps1` | AES-128-CBC decryptor for GvrPlus `.enc` files |
| `Tools\Build-NascarSqliteDb.py` | schema to SQLite, seeding and provisioning |
| `Tools\Patch-PlusdeToSqlite.ps1` | dnlib typeref swap + pre-flight + verify |
| `Tools\Test-GvrSqlite.ps1`, `TestGvrSqlite.cs` | functional test |
| `src\GvrSqlite\GvrSqlite.cs` | the provider source |
| `Deploy\` | the four files the installer uses |
