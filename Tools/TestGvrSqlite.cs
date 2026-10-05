// Functional test for the GvrSqlite provider against the NASCAR game.db.
// Exercises the same call shapes PLUSDE uses: ExecuteScalar, stored-proc
// Get/GetSet fills, an InsertOrUpdate round-trip, and an InsertDepth cascade.
//
// Build (from Tools\Test-GvrSqlite.ps1):
//   csc /target:exe /r:GvrSqlite.dll /r:System.Data.dll TestGvrSqlite.cs
using System;
using System.Data;
using GvrSqlite;

class TestGvrSqlite
{
    static int fail = 0;

    static void Check(string label, bool ok, string detail)
    {
        Console.WriteLine((ok ? "  PASS  " : "  FAIL  ") + label + (detail == null ? "" : "   [" + detail + "]"));
        if (!ok) fail++;
    }

    static GvrCommand Proc(GvrConnection c, string name)
    {
        GvrCommand cmd = new GvrCommand();
        cmd.Connection = c;
        cmd.CommandText = name;
        cmd.CommandType = CommandType.StoredProcedure;
        return cmd;
    }

    static void Add(GvrCommand cmd, string name, object val)
    {
        // same shape PLUSDE uses: Add(name, type, size, sourceColumn) then set Value
        GvrParameter p = cmd.Parameters.Add("@" + name, SqlDbType.VarChar, 0, name);
        p.Value = val;
    }

    static DataSet Fill(GvrCommand cmd)
    {
        GvrDataAdapter da = new GvrDataAdapter();
        da.SelectCommand = cmd;
        DataSet ds = new DataSet();
        da.Fill(ds);
        return ds;
    }

    [STAThread]
    static int Main(string[] args)
    {
        string db = args.Length > 0 ? args[0] : "game.db";
        Console.WriteLine("GvrSqlite functional test");
        Console.WriteLine("  db = " + db);

        GvrConnection c = new GvrConnection();
        c.ConnectionString = "sqlite=" + db;
        c.Open();
        Check("connection opens", c.State == ConnectionState.Open, "state=" + c.State);

        // 1. ExecuteScalar (NASCAR's PLUSDE uses it; NFSU's build never did)
        GvrCommand sc = new GvrCommand("SELECT COUNT(*) FROM Tracks_NAS1", c);
        object n = sc.ExecuteScalar();
        Check("ExecuteScalar COUNT(*) Tracks_NAS1", n != null && Convert.ToInt64(n) == 28, "got " + n);

        // 2. CommandTimeout setter must bind
        sc.CommandTimeout = 45;
        Check("CommandTimeout accepted", sc.CommandTimeout == 45, null);

        // 3. SP_Get_<T> -> single row by PK
        GvrCommand g = Proc(c, "SP_Get_CabinetConfiguration_NAS1");
        Add(g, "CabinetConfigurationId", 3903L);
        DataSet ds = Fill(g);
        bool got = ds.Tables.Count == 1 && ds.Tables[0].Rows.Count == 1;
        string freeplay = got ? Convert.ToString(ds.Tables[0].Rows[0]["FreePlay"]) : "(no row)";
        Check("SP_Get_CabinetConfiguration_NAS1 returns the cabinet row", got, "rows=" +
              (ds.Tables.Count > 0 ? ds.Tables[0].Rows.Count : 0));
        Check("FreePlay provisioned to 1", freeplay == "1", "FreePlay=" + freeplay);

        // 4. SP_GetSet_<T> -> whole table
        DataSet all = Fill(Proc(c, "SP_GetSet_Drivers_NAS1"));
        int drivers = all.Tables.Count > 0 ? all.Tables[0].Rows.Count : 0;
        Check("SP_GetSet_Drivers_NAS1 returns the driver roster", drivers == 12, "rows=" + drivers);

        DataSet lb = Fill(Proc(c, "SP_GetSet_CabinetLeaderboard_NAS1"));
        int lbrows = lb.Tables.Count > 0 ? lb.Tables[0].Rows.Count : 0;
        Check("SP_GetSet_CabinetLeaderboard_NAS1 has default entries", lbrows == 120, "rows=" + lbrows);

        // 5. InsertOrUpdate round-trip on GlobalVariable (the shell's progress store)
        string stamp = DateTime.Now.Ticks.ToString();
        GvrCommand up = Proc(c, "SP_InsertOrUpdate_GlobalVariable");
        Add(up, "GlobalVariableId", 5L);
        Add(up, "VariableName", "CabinetId");
        Add(up, "VariableValue", stamp);
        Add(up, "ModifiedDate", "633253059627024993");
        up.ExecuteNonQuery();

        GvrCommand rd = Proc(c, "SP_Get_GlobalVariable");
        Add(rd, "GlobalVariableId", 5L);
        DataSet gv = Fill(rd);
        string back = (gv.Tables.Count > 0 && gv.Tables[0].Rows.Count > 0)
            ? Convert.ToString(gv.Tables[0].Rows[0]["VariableValue"]) : "(none)";
        Check("GlobalVariable upsert round-trips", back == stamp, "wrote " + stamp + ", read " + back);

        int rows = (gv.Tables.Count > 0) ? gv.Tables[0].Rows.Count : 0;
        Check("GlobalVariable upsert did not duplicate the row", rows == 1, "rows=" + rows);

        // 6. InsertDepth cascade: primary row written, nested propagation cols ignored
        GvrCommand dep = Proc(c, "SP_InsertDepth_GvrCabinet");
        Add(dep, "GvrCabinet_GvrCabinetId", 77L);
        Add(dep, "GvrCabinet_SerialNumber", "DEPTHTEST01");
        Add(dep, "GvrCabinet_GameTitle", "NASCAR");
        Add(dep, "GvrCabinetLocation_GvrCabinetLocationId_GvrCabinetLocationId", 5L);
        Add(dep, "GvrCabinetLocation_GvrCabinetLocationId_LocationName", "ignored");
        dep.ExecuteNonQuery();

        GvrCommand chk = Proc(c, "SP_Get_GvrCabinet");
        Add(chk, "GvrCabinetId", 77L);
        DataSet dd = Fill(chk);
        string serial = (dd.Tables.Count > 0 && dd.Tables[0].Rows.Count > 0)
            ? Convert.ToString(dd.Tables[0].Rows[0]["SerialNumber"]) : "(none)";
        Check("InsertDepth writes the primary row", serial == "DEPTHTEST01", "serial=" + serial);

        // 7. exception surface used by PLUSDE's catch blocks
        try
        {
            GvrCommand bad = new GvrCommand("SELECT * FROM NoSuchTable_NAS1", c);
            Fill(bad);
            Check("bad SQL raises GvrException", false, "no exception");
        }
        catch (GvrException ex)
        {
            bool okmsg = ex.Message != null && ex.Message.Length > 0;
            bool okerr = ex.Errors != null && ex.Errors.Count > 0 && ex.Errors[0].Message != null;
            Check("GvrException.Message populated", okmsg, null);
            Check("GvrException.Errors[0].Message populated", okerr, null);
        }

        c.Close();
        Check("connection closes", c.State == ConnectionState.Closed, "state=" + c.State);

        Console.WriteLine(fail == 0 ? "\nALL TESTS PASSED" : "\n" + fail + " TEST(S) FAILED");
        return fail == 0 ? 0 : 1;
    }
}
