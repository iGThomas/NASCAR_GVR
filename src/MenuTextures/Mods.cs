// Mods.cs - replacement textures kept as player data in Shell\Mods\<scene>\<NNN>_<CRC>.png and put into the
// scenes at every game start (NascarLaunch.exe runs "GvrMenuTextures.exe apply <install folder>").
//
// <scene>.am.nomods is the scene as the installer left it, without replacements. The launcher-side apply
// rebuilds <scene>.am from it whenever a replacement or the scene changed. An update that rewrites a scene
// is noticed (its size or time differs from what apply last wrote) and becomes the new .nomods copy, so
// replacements survive updates. With no replacements left, the scene goes back to its .nomods copy.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;

namespace MenuTextures
{
    public static class Mods
    {
        public static string ModsDir(string shell, string scene) { return Path.Combine(Path.Combine(shell, "Mods"), scene); }

        // The scene as it is without replacements: the .nomods copy when there is one.
        public static string CleanScene(string amPath)
        {
            string clean = amPath + ".nomods";
            return File.Exists(clean) && !SceneChangedSinceApply(amPath) ? clean : amPath;
        }

        // A replacement file is "<number>_<name>_<fingerprint>.png" (the name part is optional, for people only).
        public static string FindReplacement(string shell, string scene, Picture p)
        {
            string dir = ModsDir(shell, scene);
            if (!Directory.Exists(dir)) return null;
            string crc = p.Crc.ToString("X8");
            foreach (string f in Directory.GetFiles(dir, p.Index.ToString("000") + "_*"))
            {
                string n = Path.GetFileNameWithoutExtension(f);
                if (IsPicture(f) && n.Substring(n.LastIndexOf('_') + 1).Equals(crc, StringComparison.OrdinalIgnoreCase)) return f;
            }
            return null;
        }

        static bool ParseName(string name, out int index, out uint crc)
        {
            index = -1; crc = 0;
            int a = name.IndexOf('_'), b = name.LastIndexOf('_');
            if (a <= 0 || b < a) return false;
            return int.TryParse(name.Substring(0, a), out index) &&
                   uint.TryParse(name.Substring(b + 1), System.Globalization.NumberStyles.HexNumber, null, out crc);
        }

        static bool IsPicture(string f)
        {
            string e = Path.GetExtension(f).ToLowerInvariant();
            return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp";
        }

        // Saves img (already the right size) as the replacement for texture p. Checks that it fits first.
        public static string SaveReplacement(string shell, string scene, Picture p, Bitmap img, out string note)
        {
            Am.Fit(img, p, out note);                          // throws when it cannot fit
            string dir = ModsDir(shell, scene);
            Directory.CreateDirectory(dir);
            string old = FindReplacement(shell, scene, p);
            if (old != null) File.Delete(old);
            string path = Path.Combine(dir, p.Key + ".png");
            img.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            return path;
        }

        public static void RemoveReplacement(string shell, string scene, Picture p)
        {
            string f = FindReplacement(shell, scene, p);
            if (f != null) File.Delete(f);
        }

        // ------------------------------------------------------------------ texture packs (zip files to share)
        // A pack is a zip with pack.txt (which game) and <scene>/<number>_<name>_<fingerprint>.png for each texture.
        public static string GameName(string shell)
        {
            return File.Exists(Path.Combine(shell, "NASCAR_Career.am")) ? "NASCAR Team Racing" : "NASCAR Racing";
        }

        public static int ExportPack(string shell, string zipPath)
        {
            string mods = Path.Combine(shell, "Mods");
            int n = 0;
            if (File.Exists(zipPath)) File.Delete(zipPath);
            using (var zip = System.IO.Compression.ZipFile.Open(zipPath, System.IO.Compression.ZipArchiveMode.Create))
            {
                var info = zip.CreateEntry("pack.txt");
                using (var w = new StreamWriter(info.Open()))
                    w.Write("NASCAR GVR menu textures\r\ngame=" + GameName(shell) + "\r\nmade=" + DateTime.Now.ToString("yyyy-MM-dd") + "\r\n");
                if (!Directory.Exists(mods)) return 0;
                foreach (string dir in Directory.GetDirectories(mods))
                    foreach (string f in Directory.GetFiles(dir).Where(IsPicture))
                    {
                        System.IO.Compression.ZipFileExtensions.CreateEntryFromFile(zip, f, Path.GetFileName(dir) + "/" + Path.GetFileName(f));
                        n++;
                    }
            }
            return n;
        }

        public class PackResult { public int Added, Skipped, Replaced; public List<string> Notes = new List<string>(); public string Game = ""; }

        // Reads pack.txt only, for the question before adding.
        public static string PackGame(string zipPath)
        {
            using (var zip = System.IO.Compression.ZipFile.OpenRead(zipPath))
            {
                var e = zip.GetEntry("pack.txt");
                if (e == null) return "";
                using (var r = new StreamReader(e.Open()))
                {
                    string line;
                    while ((line = r.ReadLine()) != null) if (line.StartsWith("game=")) return line.Substring(5).Trim();
                }
            }
            return "";
        }

        public static PackResult ImportPack(string shell, string zipPath, bool replaceMine)
        {
            var res = new PackResult { Game = PackGame(zipPath) };
            var scenes = new Dictionary<string, List<Picture>>(StringComparer.OrdinalIgnoreCase);
            using (var zip = System.IO.Compression.ZipFile.OpenRead(zipPath))
            {
                foreach (var e in zip.Entries)
                {
                    if (e.Name.Length == 0 || e.Name.Equals("pack.txt", StringComparison.OrdinalIgnoreCase)) continue;
                    string[] path = e.FullName.Replace('\\', '/').Split('/');
                    string scene = path.Length >= 2 ? path[path.Length - 2] : "";
                    string label = e.FullName;
                    if (!IsPicture(e.Name)) { res.Skipped++; res.Notes.Add(label + ": not a texture"); continue; }
                    string am = Path.Combine(shell, scene + ".am");
                    if (scene.Length == 0 || !File.Exists(am)) { res.Skipped++; res.Notes.Add(label + ": this game has no scene " + scene); continue; }
                    List<Picture> pics;
                    if (!scenes.TryGetValue(scene, out pics)) { pics = Am.Scan(File.ReadAllBytes(CleanScene(am))); scenes[scene] = pics; }
                    int index; uint crc; Picture p = null;
                    if (ParseName(Path.GetFileNameWithoutExtension(e.Name), out index, out crc))
                    {
                        if (index >= 0 && index < pics.Count && pics[index].Crc == crc) p = pics[index];
                        else p = pics.FirstOrDefault(x => x.Crc == crc);
                    }
                    if (p == null) { res.Skipped++; res.Notes.Add(label + ": no such texture in this game's " + scene + " (made for the other game or another version?)"); continue; }
                    try
                    {
                        Bitmap img;
                        using (var ms = new MemoryStream())
                        {
                            using (var s = e.Open()) s.CopyTo(ms);
                            img = Am.Load(ms.ToArray());
                        }
                        using (img)
                        {
                            if (img.Width != p.Width || img.Height != p.Height) { res.Skipped++; res.Notes.Add(label + ": wrong size"); continue; }
                            bool mine = FindReplacement(shell, scene, p) != null;
                            if (mine && !replaceMine) { res.Skipped++; res.Notes.Add(label + ": you already replaced texture " + p.Index + " - kept yours"); continue; }
                            string note;
                            SaveReplacement(shell, scene, p, img, out note);
                            if (mine) res.Replaced++;
                            res.Added++;
                        }
                    }
                    catch (Exception ex) { res.Skipped++; res.Notes.Add(label + ": " + ex.Message); }
                }
            }
            return res;
        }

        // How many textures in the pack the player has already replaced (asked before adding).
        public static int PackConflicts(string shell, string zipPath)
        {
            int n = 0;
            using (var zip = System.IO.Compression.ZipFile.OpenRead(zipPath))
                foreach (var e in zip.Entries)
                {
                    string[] path = e.FullName.Replace('\\', '/').Split('/');
                    if (path.Length < 2 || !IsPicture(e.Name)) continue;
                    string dir = ModsDir(shell, path[path.Length - 2]);
                    int index; uint crc;
                    if (!Directory.Exists(dir) || !ParseName(Path.GetFileNameWithoutExtension(e.Name), out index, out crc)) continue;
                    if (Directory.GetFiles(dir, index.ToString("000") + "_*").Any(IsPicture)) n++;
                }
            return n;
        }

        // ------------------------------------------------------------------ apply (run by the launcher)
        static string StatePath(string amPath)
        {
            string shell = Path.GetDirectoryName(amPath), scene = Path.GetFileNameWithoutExtension(amPath);
            return Path.Combine(ModsDir(shell, scene), "applied.txt");
        }

        static string SceneStamp(string amPath)
        {
            var fi = new FileInfo(amPath);
            return fi.Length + " " + fi.LastWriteTimeUtc.Ticks;
        }

        static string ModsStamp(string dir)
        {
            if (!Directory.Exists(dir)) return "";
            var sb = new StringBuilder();
            foreach (string f in Directory.GetFiles(dir).Where(IsPicture).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
            {
                var fi = new FileInfo(f);
                sb.Append(fi.Name).Append(' ').Append(fi.Length).Append(' ').Append(fi.LastWriteTimeUtc.Ticks).Append('\n');
            }
            return sb.ToString();
        }

        // true when apply wrote this scene before and the .am is no longer that file (an update rewrote it)
        static bool SceneChangedSinceApply(string amPath)
        {
            string state = StatePath(amPath);
            if (!File.Exists(state)) return false;
            string first = File.ReadAllLines(state).FirstOrDefault() ?? "";
            return first != SceneStamp(amPath);
        }

        // Returns the number of replacements that could not be used.
        public static int ApplyAll(string root, Action<string> log)
        {
            string shell = Path.Combine(root, "Shell");
            int failed = 0;
            if (!Directory.Exists(shell)) { log("no Shell folder in " + root); return 0; }
            foreach (string am in Directory.GetFiles(shell, "*.am"))
            {
                try { failed += Apply(am, log); }
                catch (Exception e) { log(Path.GetFileName(am) + ": " + e.Message); failed++; }
            }
            // empty replacement folders go, so the launcher no longer needs to run this at every start
            string mods = Path.Combine(shell, "Mods");
            try
            {
                if (Directory.Exists(mods))
                {
                    foreach (string d in Directory.GetDirectories(mods))
                        if (!Directory.EnumerateFileSystemEntries(d).Any()) Directory.Delete(d);
                    if (!Directory.EnumerateFileSystemEntries(mods).Any()) Directory.Delete(mods);
                }
            }
            catch { }
            return failed;
        }

        static int Apply(string amPath, Action<string> log)
        {
            string shell = Path.GetDirectoryName(amPath), scene = Path.GetFileNameWithoutExtension(amPath);
            string dir = ModsDir(shell, scene), clean = amPath + ".nomods", state = StatePath(amPath);
            bool haveMods = Directory.Exists(dir) && Directory.GetFiles(dir).Any(IsPicture);
            if (!haveMods && !File.Exists(clean)) return 0;    // never changed: nothing to do

            bool sceneChanged = SceneChangedSinceApply(amPath);
            if (!File.Exists(clean) || sceneChanged)
            {
                // the .am is the installer's file: keep it as the clean copy
                File.Copy(amPath, clean, true);
                if (sceneChanged) log(scene + ": the scene was updated - replacements put in again");
            }
            string modsStamp = ModsStamp(dir);
            if (!sceneChanged && File.Exists(state))
            {
                string[] lines = File.ReadAllLines(state);
                string had = string.Join("\n", lines.Skip(1)) + (lines.Length > 1 ? "\n" : "");
                if (had == modsStamp) return 0;                // nothing changed since the last start
            }

            byte[] data = File.ReadAllBytes(clean);
            if (!haveMods)
            {
                File.WriteAllBytes(amPath, data);
                File.Delete(clean);
                if (File.Exists(state)) File.Delete(state);
                log(scene + ": no replacements left - original textures back");
                return 0;
            }
            List<Picture> pics = Am.Scan(data);
            var byCrc = new Dictionary<uint, Picture>();
            foreach (Picture p in pics) if (!byCrc.ContainsKey(p.Crc)) byCrc[p.Crc] = p;     // a scene can hold the same texture twice
            int used = 0, failed = 0;
            foreach (string f in Directory.GetFiles(dir).Where(IsPicture).OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
            {
                string name = Path.GetFileNameWithoutExtension(f);
                uint crc; int index; Picture p = null;
                if (ParseName(name, out index, out crc))
                {
                    // the number first (the same texture can be in a scene twice), its fingerprint as a check
                    if (index >= 0 && index < pics.Count && pics[index].Crc == crc) p = pics[index];
                    else byCrc.TryGetValue(crc, out p);
                }
                if (p == null)
                {
                    log(scene + ": " + Path.GetFileName(f) + " - no texture with this name in the scene (made for another version?) - skipped");
                    failed++; continue;
                }
                try
                {
                    string note;
                    using (Bitmap img0 = Am.LoadFile(f))
                    {
                        Bitmap img = img0;
                        bool resized = false;
                        if (img.Width != p.Width || img.Height != p.Height) { img = Am.Resize(img0, p.Width, p.Height); resized = true; }
                        List<byte[]> parts = Am.Fit(img, p, out note);
                        for (int i = 0; i < parts.Count; i++) Buffer.BlockCopy(parts[i], 0, data, p.Parts[i].Start, parts[i].Length);
                        if (resized) { img.Dispose(); note += ", resized to " + p.Width + "x" + p.Height; }
                    }
                    log(scene + ": texture " + p.Index + " replaced (" + note + ")");
                    used++;
                }
                catch (Exception e)
                {
                    log(scene + ": " + Path.GetFileName(f) + " - " + e.Message + " - skipped");
                    failed++;
                }
            }
            File.WriteAllBytes(amPath, data);
            File.WriteAllText(state, SceneStamp(amPath) + "\n" + modsStamp);
            log(scene + ": " + used + " replacement(s) in place" + (failed > 0 ? ", " + failed + " skipped" : ""));
            return failed;
        }
    }
}
