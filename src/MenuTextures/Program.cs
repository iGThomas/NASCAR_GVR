// GvrMenuTextures.exe - see and replace the textures in the NASCAR arcade menus.
//
//   GvrMenuTextures.exe                     the editor window (finds the install folder from its own place, <install>\Tools)
//   GvrMenuTextures.exe apply <install>     put the replacements into the scenes (run by NascarLaunch.exe at every start)
//   GvrMenuTextures.exe export <scene.am> <folder>   save every texture of a scene as PNG
//   GvrMenuTextures.exe export-pack <install> <pack.zip>   save all replacements as a texture pack
//   GvrMenuTextures.exe import-pack <install> <pack.zip>   import a texture pack (replaces earlier replacements)
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace MenuTextures
{
    static class Program
    {
        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length >= 2 && args[0].Equals("apply", StringComparison.OrdinalIgnoreCase)) return RunApply(args[1]);
            if (args.Length >= 3 && args[0].Equals("export", StringComparison.OrdinalIgnoreCase)) return RunExport(args[1], args[2]);
            if (args.Length >= 3 && args[0].Equals("export-pack", StringComparison.OrdinalIgnoreCase))
            {
                Console.WriteLine(Mods.ExportPack(Path.Combine(args[1], "Shell"), args[2]) + " textures in " + args[2]);
                return 0;
            }
            if (args.Length >= 3 && args[0].Equals("import-pack", StringComparison.OrdinalIgnoreCase))
            {
                Mods.PackResult r = Mods.ImportPack(Path.Combine(args[1], "Shell"), args[2], true);
                Console.WriteLine("pack for '" + r.Game + "': " + r.Added + " imported, " + r.Replaced + " replaced earlier ones, " + r.Skipped + " skipped");
                foreach (string n in r.Notes) Console.WriteLine("  " + n);
                return r.Skipped > 0 ? 1 : 0;
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            // optional: <install or Shell folder> [scene] [texture number] opens straight at that texture
            string shell = FindShell(args.Length > 0 ? args[0] : null);
            if (shell == null) return 1;
            int pic = -1;
            if (args.Length > 2) int.TryParse(args[2], out pic);
            Application.Run(new EditorForm(shell, args.Length > 1 ? args[1] : null, pic));
            return 0;
        }

        static string FindShell(string given)
        {
            var tries = new List<string>();
            if (!string.IsNullOrEmpty(given)) { tries.Add(given); tries.Add(Path.Combine(given, "Shell")); }
            string exeDir = Path.GetDirectoryName(Application.ExecutablePath);
            tries.Add(Path.Combine(Path.GetDirectoryName(exeDir) ?? exeDir, "Shell"));
            tries.Add(Path.Combine(exeDir, "Shell"));
            foreach (string t in tries)
                if (Directory.Exists(t) && Directory.GetFiles(t, "*.am").Length > 0) return Path.GetFullPath(t);
            using (var fb = new FolderBrowserDialog { Description = "Choose the game's install folder (the one with the Shell folder)." })
            {
                while (fb.ShowDialog() == DialogResult.OK)
                {
                    foreach (string t in new[] { Path.Combine(fb.SelectedPath, "Shell"), fb.SelectedPath })
                        if (Directory.Exists(t) && Directory.GetFiles(t, "*.am").Length > 0) return t;
                    MessageBox.Show("No menu scenes (.am files) found there.", "NASCAR menu textures", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
            return null;
        }

        static int RunApply(string root)
        {
            string logDir = Path.Combine(root, "LOG");
            string logPath = Path.Combine(logDir, "gvrmenutextures.log");
            Action<string> log = s =>
            {
                try
                {
                    Directory.CreateDirectory(logDir);
                    if (File.Exists(logPath) && new FileInfo(logPath).Length > 256 * 1024) File.Delete(logPath);
                    File.AppendAllText(logPath, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " " + s + Environment.NewLine);
                }
                catch { }
            };
            try { return Mods.ApplyAll(root, log) > 0 ? 1 : 0; }
            catch (Exception e) { log("apply failed: " + e.Message); return 2; }
        }

        static int RunExport(string am, string outDir)
        {
            Directory.CreateDirectory(outDir);
            byte[] d = File.ReadAllBytes(am);
            List<Picture> pics = Am.Scan(d);
            foreach (Picture p in pics)
                using (Bitmap b = Am.Decode(d, p)) b.Save(Path.Combine(outDir, p.Key + ".png"), System.Drawing.Imaging.ImageFormat.Png);
            Console.WriteLine(pics.Count + " textures saved to " + outDir);
            return 0;
        }
    }
}
