#!/usr/bin/env python3
"""
Build-NascarSqliteDb.py — build the NASCAR cabinet database as SQLite.

Translates the decrypted GlobalVR cabinet schema (NASCARcabinet.txt, SQL
Server 2000 DDL) into an equivalent SQLite database, then seeds and
provisions it so the Anark shell / GvrPlus stack can run with no SQL Server.

This is the NASCAR sibling of the parent project's Tools/Build-GvrSqliteDb.py.
Kept standalone on purpose so the NASCAR folder is self-contained. The
schema-parsing and seeding logic is the same; the provisioning differs
(NASCAR uses _NAS1 tables and ships far more seed content than NFSU did).

Inputs (produced by Tools\\Decrypt-GvrEnc.ps1):
    NASCARcabinet.txt          schema DDL + stored procs + GlobalVariable seeds
    NASCARcabinet_Content.txt  seed rows (tracks, drivers, lookups, cabinet config)
    <GVRPLUS>\\4\\schema\\game  operator pricing .tbl files

Usage:
    python Build-NascarSqliteDb.py <NASCARcabinet.txt> <out.db> [content.txt] [tbl_dir]
"""
import sys, re, os, sqlite3

# NASCAR's entry in GvrGame (from NASCARcabinet_Content.txt).
NASCAR_GAME_CODE = 'NASCAR06'
NASCAR_GAME_ID = 3960


# ---------------------------------------------------------------- schema

def normalize_type(sqltype):
    """Strip SQL Server's optional brackets around the type name.

    NASCARcabinet.txt mixes both styles: most tables declare `[Col] bigint`,
    but GlobalVariable declares `[GlobalVariableId] [bigint] NOT NULL`. Without
    stripping, the type reads as '[bigint]', no mapping matches, and the column
    silently becomes TEXT - which handed PLUSDE a String where it expects an
    Int64 and crashed the shell with an access violation inside its mixed-mode
    code the moment it read GlobalVariable. Only that one table is affected,
    and it is the first thing the shell queries.
    """
    return re.sub(r'[\[\]]', '', sqltype).strip()


def sqlite_type(sqltype):
    """SQL Server -> SQLite type affinity."""
    t = normalize_type(sqltype).lower()
    base = re.split(r'[\s(]', t, 1)[0]
    if base in ('bigint', 'int', 'integer', 'smallint', 'tinyint', 'bit'):
        return 'INTEGER'
    if base in ('float', 'real', 'decimal', 'numeric', 'money', 'smallmoney'):
        return 'REAL'
    if base in ('binary', 'varbinary', 'image', 'timestamp', 'rowversion'):
        return 'BLOB'
    return 'TEXT'


def parse_tables(sql_text):
    """Yield (table_name, [(col, type, is_pk)]) for each CREATE TABLE block."""
    lines = sql_text.splitlines()
    i, n = 0, len(lines)
    ct_re = re.compile(r'^\s*CREATE\s+TABLE\s+\[?(?:dbo\]?\.\[?)?([A-Za-z0-9_]+)\]?', re.I)
    while i < n:
        m = ct_re.match(lines[i])
        if not m:
            i += 1
            continue
        table = m.group(1)
        cols = []
        i += 1
        while i < n and lines[i].strip() in ('', '('):
            i += 1
        while i < n:
            raw = lines[i].strip()
            if raw.startswith(')'):
                i += 1
                break
            if raw.startswith('('):
                raw = raw[1:].strip()
            cm = re.match(r'\[?([A-Za-z0-9_]+)\]?\s+(.+?)\s*,?\s*$', raw)
            if cm:
                rest = cm.group(2)
                cols.append((cm.group(1), rest, bool(re.search(r'PRIMARY\s+KEY', rest, re.I))))
            i += 1
        if cols:
            yield table, cols


def parse_tsql_args(s):
    """Split a T-SQL EXEC argument list into python values."""
    vals, cur, inq, i = [], '', False, 0
    while i < len(s):
        ch = s[i]
        if inq:
            if ch == "'":
                if i + 1 < len(s) and s[i + 1] == "'":
                    cur += "'"
                    i += 2
                    continue
                inq = False
            else:
                cur += ch
        elif ch == "'":
            inq = True
        elif ch == ',':
            vals.append(cur.strip())
            cur = ''
        else:
            cur += ch
        i += 1
    if cur.strip() != '' or vals:
        vals.append(cur.strip())

    out = []
    for tok in vals:
        t = tok.strip()
        if t.upper() == 'NULL' or t == '':
            out.append(None)
            continue
        if re.match(r'^-?\d+$', t):
            out.append(int(t))
            continue
        if re.match(r'^-?\d*\.\d+([eE]-?\d+)?$', t):
            out.append(float(t))
            continue
        out.append(t)
    return out


# ---------------------------------------------------------------- seeding

def seed_content(cur, tbl_cols, content_path):
    """Apply 'Exec SP_(Insert|InsertOrUpdate)_<Table> args' lines as upserts.

    Verified for NASCAR: every SP_InsertOrUpdate_<T> declares its parameters in
    the same order as the table's DDL columns, so positional mapping is correct.
    """
    rx = re.compile(r'^\s*Exec\s+SP_(?:InsertOrUpdate|Insert)_([A-Za-z0-9_]+)\s+(.*\S)\s*$', re.I)
    applied, skipped, per_table = 0, 0, {}
    for line in open(content_path, encoding='utf-8', errors='replace'):
        m = rx.match(line)
        if not m:
            continue
        table = m.group(1)
        cols = tbl_cols.get(table.lower())
        if not cols:
            skipped += 1
            continue
        vals = parse_tsql_args(m.group(2))
        # proc bodies pass @params, not data
        if vals and isinstance(vals[0], str) and vals[0].startswith('@'):
            continue
        use = cols[:len(vals)]
        vals = vals[:len(use)]
        try:
            cur.execute('INSERT OR REPLACE INTO "%s" (%s) VALUES (%s)'
                        % (table, ','.join('"%s"' % c for c in use), ','.join('?' * len(use))), vals)
            applied += 1
            per_table[table] = per_table.get(table, 0) + 1
        except sqlite3.Error as e:
            skipped += 1
            if skipped <= 5:
                print('  seed !! %s: %s' % (table, e))
    return applied, skipped, per_table


def seed_global_variables(cur, schema_path):
    """GlobalVariable's seed rows live in the SCHEMA file, not the content file.

    Same trap as NFSU: seed_content() never sees them, so the table would ship
    empty and the shell's post-race logic would have no row to read or update.
    GlobalVariable also has no PRIMARY KEY in the OEM DDL (the upsert lives in
    SP_InsertOrUpdate_GlobalVariable), so we add the unique index that restores
    the stored proc's semantics -- otherwise INSERT OR REPLACE appends
    duplicates and the shell reads a stale first row.
    """
    rx = re.compile(r'^\s*Exec\s+SP_(?:InsertOrUpdate|Insert)_GlobalVariable\s+(.*\S)\s*$', re.I)
    rows = []
    for line in open(schema_path, encoding='utf-8', errors='replace'):
        m = rx.match(line)
        if not m:
            continue
        vals = parse_tsql_args(m.group(1))
        if len(vals) >= 4 and isinstance(vals[0], int):
            # keep native types: the id and ModifiedDate are bigint columns and the
            # game reads them as Int64, so do not stringify them here
            rows.append([v if v is not None else '' for v in vals[:4]])
    if not rows:
        print('  !! no GlobalVariable seed rows found')
        return 0
    cur.execute('CREATE UNIQUE INDEX IF NOT EXISTS ux_GlobalVariable_Id '
                'ON GlobalVariable(GlobalVariableId)')
    for r in rows:
        cur.execute('INSERT OR REPLACE INTO "GlobalVariable" '
                    '(GlobalVariableId,VariableName,VariableValue,ModifiedDate) VALUES (?,?,?,?)', r)
    print('  seed: GlobalVariable = %d rows + unique id index' % len(rows))
    return len(rows)


# ---------------------------------------------------------------- provisioning

_PLUS_TBL = [
    ('ProductDefinition', 'a+productdefinition.tbl'),
    ('PricingModel', 'a+pricingmodel.tbl'),
    ('ProductPricing', 'a+productpricing.tbl'),
    ('UpchargePricing', 'a+upchargepricing.tbl'),
    ('CollectionCategory', 'a+collectioncategory.tbl'),
    ('LedgerCategory', 'a+ledgercategory.tbl'),
    ('PricingConfiguration', 'a+pricingconfiguration.tbl'),
    ('RegionalPricingAvailability', 'a+regionalpricingavailability.tbl'),
    ('OperatorSplit', 'a+operatorsplit.tbl'),
]


# NASCAR-specific bulk loads, mirroring DbSetup\Nascar_db.exe (NascarLeaderboard.sql).
# These are the default attract-mode leaderboard entries and the last-collection
# accounting rows; without them the shell's leaderboard screens are blank.
_NASCAR_TBL = [
    ('CabinetLeaderboard_NAS1', 'nascarcabinetleaderboard.tbl'),
    ('DailyLeaderboard_NAS1', 'nascardailyleaderboard.tbl'),
    ('DefaultDailyLeaderboard_NAS1', 'nascardailyleaderboard.tbl'),
    ('Collection', 'nascarlastcollection.tbl'),
    ('CollectionEntry', 'nascarlastcollectionentry.tbl'),
    ('ServiceCollection', 'nascarlastservicecollection.tbl'),
    ('ServiceCollectionEntry', 'nascarlastservicecollectionentry.tbl'),
]


def _parse_tbl(path):
    """Pipe-delimited operator .tbl file (the BULK INSERT source of adminplus.sql)."""
    raw = open(path, 'rb').read().decode('latin-1')
    rows = []
    for line in raw.replace('\r\n', '\n').split('\n'):
        line = line.strip()
        if not line:
            continue
        if line.endswith('|'):
            line = line[:-1]
        rows.append(line.split('|'))
    return rows


def provision_plus(cur, tbl_cols, tbl_dir, free_play=True):
    """Make the DB usable by a standalone (non-networked, coin-less) cabinet."""

    # 1) operator pricing tables. Without these the Plus pricing lookup finds
    #    nothing and the frontend spins in its pricing-init retry loop.
    if tbl_dir and os.path.isdir(tbl_dir):
        disk = {f.lower(): f for f in os.listdir(tbl_dir)}
        for table, fname in _PLUS_TBL + _NASCAR_TBL:
            real = disk.get(fname.lower())
            cols = tbl_cols.get(table.lower())
            if not real or not cols:
                continue
            rows = _parse_tbl(os.path.join(tbl_dir, real))
            cur.execute('DELETE FROM "%s"' % table)
            n = 0
            for r in rows:
                if len(r) == len(cols) + 1 and r[-1] == '':
                    r = r[:-1]
                if len(r) != len(cols):
                    continue
                cur.execute('INSERT INTO "%s" (%s) VALUES (%s)'
                            % (table, ','.join('"%s"' % c for c in cols), ','.join('?' * len(cols))),
                            [None if v == '' else v for v in r])
                n += 1
            print('  provision %-28s %d rows' % (table, n))
    else:
        print('  provision: pricing .tbl dir not found - PricingModel left EMPTY (frontend will hang)')

    # 2) a registered free-play cabinet so GvrCabinet[0]/CabinetBilling[0] resolve.
    pm = 12000
    try:
        row = cur.execute("SELECT PricingModelId FROM PricingModel "
                          "WHERE RegionId=1234 AND DefaultForRegion='1' LIMIT 1").fetchone()
        if row:
            pm = row[0]
    except sqlite3.Error:
        pass

    def synth(table, colvals):
        cols = tbl_cols.get(table.lower())
        if not cols:
            print('  provision: %s not in schema, skipped' % table)
            return
        data = {k: v for k, v in colvals.items() if k in cols}
        cur.execute('DELETE FROM "%s" WHERE "%s"=?' % (table, cols[0]), (colvals[cols[0]],))
        cur.execute('INSERT INTO "%s" (%s) VALUES (%s)'
                    % (table, ','.join('"%s"' % c for c in data), ','.join('?' * len(data))),
                    list(data.values()))

    synth('GvrCabinet', dict(
        GvrCabinetId=0, CreationDate='2008-08-26 12:00:00', ModifiedDate=0, PropagationDate=0,
        CabinetId=0, OperatorNumber=0, DongleNumber=0, SerialNumber='HOME00000001',
        GameTitle='NASCAR', GameVersion='1.1.0', CabinetNumber=0, Status='A',
        GvrCabinetLocationId=0, CabinetPWD='', GvrOperatorId=0, GvrIspAccountId=0, WebCanSearch=0))
    synth('CabinetBilling', dict(
        CabinetBillingId=0, CreationDate='2008-08-26 12:00:00', ModifiedDate=0, PropagationDate=0,
        CabinetId=0, DongleId=0, OperatorId=0, PricingModelId=pm, RegionId=1234))
    print('  provision: GvrCabinet[0] + CabinetBilling[0] (PricingModel %s)' % pm)

    # 3) free play. The OEM row ships FreePlay=0 because this is coin-op software:
    #    with no coin mechanism the frontend would ask for credits it can never
    #    receive. This is the one value we deliberately change from the OEM
    #    cabinet settings; it stays a normal operator setting (O key -> operator
    #    menu) so genuine coin-op behaviour can be restored.
    if free_play:
        try:
            n = cur.execute('UPDATE CabinetConfiguration_NAS1 SET FreePlay=1').rowcount
            print('  provision: CabinetConfiguration_NAS1.FreePlay = 1 (%d row(s))' % n)
        except sqlite3.Error as e:
            print('  provision !! FreePlay: %s' % e)

    # 3b) make all six tracks selectable. The OEM row ships TracksDisabled=0 and, despite
    #     the column name, NASCARPlugIn reads it as a bitmask of AVAILABLE tracks
    #     (NASCAR_DISABLE_TRACKS_GET_VALUE = (value & bit[track]) > 0). With 0 the shell
    #     counts no enabled tracks, skips the track-select screen and always races Daytona.
    #     63 (0x3F) enables Daytona, Talladega, Lowe's, Indianapolis, Phoenix and Bristol in
    #     both the US and the international bit tables (NASCAR_FINDINGS.md 12.6). Still an
    #     ordinary operator setting: Game Settings -> Disable Tracks.
    try:
        n = cur.execute('UPDATE CabinetConfiguration_NAS1 SET TracksDisabled=63').rowcount
        print('  provision: CabinetConfiguration_NAS1.TracksDisabled = 63 (all tracks) (%d row(s))' % n)
    except sqlite3.Error as e:
        print('  provision !! TracksDisabled: %s' % e)

    # 3c) mark the operator's first-time setup as done. On the first start NASCARPlugIn's
    #     Operator_FirstTime_Init runs when GlobalVariable gOperatorFirstTimeInitilized (sic)
    #     is 0: Operator_ResetFactoryDefaults overwrites the row above (FreePlay 0, every
    #     on/off option 1, TracksDisabled 65535), and Operator_SetTimeZone changes the PC's
    #     time zone. Seen on a fresh install: free play was off after the first start.
    try:
        n = cur.execute("UPDATE GlobalVariable SET VariableValue='1' "
                        "WHERE VariableName='gOperatorFirstTimeInitilized'").rowcount
        print('  provision: GlobalVariable gOperatorFirstTimeInitilized = 1 (%d row(s))' % n)
    except sqlite3.Error as e:
        print('  provision !! gOperatorFirstTimeInitilized: %s' % e)

    # 4) point the cabinet's game identity at NASCAR's GvrGame row.
    try:
        row = cur.execute('SELECT GvrGameId, GameName FROM GvrGame WHERE GameCode=?',
                          (NASCAR_GAME_CODE,)).fetchone()
        if row:
            print('  provision: GvrGame %s = id %s (%s)' % (NASCAR_GAME_CODE, row[0], row[1]))
        else:
            cur.execute('INSERT OR REPLACE INTO GvrGame '
                        '(GvrGameId,CreationDate,ModifiedDate,PropagationDate,GameCode,GameName,OnlineSupport) '
                        'VALUES (?,?,?,?,?,?,?)',
                        (NASCAR_GAME_ID, '2008-08-26 12:00:00', 0, 0, NASCAR_GAME_CODE, 'NAS1', 0))
            print('  provision: GvrGame %s inserted (id %d)' % (NASCAR_GAME_CODE, NASCAR_GAME_ID))
    except sqlite3.Error as e:
        print('  provision !! GvrGame: %s' % e)


# ---------------------------------------------------------------- main

def main():
    src = sys.argv[1] if len(sys.argv) > 1 else 'NASCARcabinet.txt'
    out = sys.argv[2] if len(sys.argv) > 2 else 'game.db'
    here = os.path.dirname(os.path.abspath(src))

    content = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, 'NASCARcabinet_Content.txt')
    tbl_dir = sys.argv[4] if len(sys.argv) > 4 else None
    if not tbl_dir:
        for cand in (os.path.join(here, 'game'), os.path.join(here, 'scripts'), here):
            if os.path.exists(os.path.join(cand, 'a+pricingmodel.tbl')):
                tbl_dir = cand
                break

    text = open(src, encoding='utf-8', errors='replace').read()
    if os.path.exists(out):
        os.remove(out)
    con = sqlite3.connect(out)
    cur = con.cursor()

    tables = list(parse_tables(text))
    for table, cols in tables:
        pk_cols = [c for c in cols if c[2]]
        coldefs = []
        for (name, sqltype, is_pk) in cols:
            d = '"%s" %s' % (name, sqlite_type(sqltype))
            if is_pk and len(pk_cols) == 1:
                d += ' PRIMARY KEY'
            coldefs.append(d)
        cur.execute('DROP TABLE IF EXISTS "%s"' % table)
        cur.execute('CREATE TABLE "%s" (\n  %s\n)' % (table, ',\n  '.join(coldefs)))

    # original SQL Server column types, so the provider can type DataColumns
    # correctly (bigint->Int64, datetime->DateTime, bit->bool, ...)
    cur.execute('DROP TABLE IF EXISTS "_gvrmeta"')
    cur.execute('CREATE TABLE "_gvrmeta" (tbl TEXT, col TEXT, sstype TEXT)')
    for table, cols in tables:
        for (name, sqltype, _pk) in cols:
            # normalized, so the provider's own type map sees 'bigint', not '[bigint]'
            cur.execute('INSERT INTO "_gvrmeta" (tbl,col,sstype) VALUES (?,?,?)',
                        (table, name, normalize_type(sqltype)))
    con.commit()
    print('schema: %d tables created in %s' % (len(tables), out))

    tbl_cols = {t.lower(): [c[0] for c in cols] for t, cols in tables}

    if content and os.path.exists(content):
        applied, skipped, per_table = seed_content(cur, tbl_cols, content)
        con.commit()
        print('seed: %d rows applied, %d skipped (%s)' % (applied, skipped, os.path.basename(content)))
        for t in sorted(per_table, key=lambda k: -per_table[k])[:6]:
            print('   %-28s %d' % (t, per_table[t]))
    else:
        print('seed: content file not found (%s)' % content)

    seed_global_variables(cur, src)
    con.commit()

    provision_plus(cur, tbl_cols, tbl_dir)
    con.commit()

    cur.execute("SELECT count(*) FROM sqlite_master WHERE type='table'")
    print('done: %d tables, %s' % (cur.fetchone()[0], out))
    con.close()


if __name__ == '__main__':
    main()
