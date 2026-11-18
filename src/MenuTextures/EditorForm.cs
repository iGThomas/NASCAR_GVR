// EditorForm.cs - the window: a scene list, thumbnails of every texture, and Save a copy / Replace / Use the original.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace MenuTextures
{
    public class EditorForm : Form
    {
        const string Title = "NASCAR menu textures";
        readonly string shell;
        ComboBox sceneBox;
        CheckBox onlyReplaced;
        Label sceneInfo;
        ListView list;
        ImageList thumbs;
        Label heading, info, replacedLabel;
        PictureBox original, replacement;
        Button saveCopy, replace, useOriginal;
        ToolStripStatusLabel status;

        byte[] data;
        List<Picture> pictures = new List<Picture>();
        string scene;

        public EditorForm(string shellDir, string startScene, int startPicture)
        {
            shell = shellDir;
            Text = Title;
            Width = 1180; Height = 780;
            MinimumSize = new Size(900, 600);
            StartPosition = FormStartPosition.CenterScreen;
            Font = new Font("Segoe UI", 9f);

            var top = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(8, 8, 8, 0), WrapContents = false };
            top.Controls.Add(new Label { Text = "Menu scene:", AutoSize = true, Margin = new Padding(0, 6, 4, 0) });
            sceneBox = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 260 };
            top.Controls.Add(sceneBox);
            onlyReplaced = new CheckBox { Text = "Show only replaced textures", AutoSize = true, Margin = new Padding(16, 5, 0, 0) };
            top.Controls.Add(onlyReplaced);
            var exportAll = new Button { Text = "Save all textures...", AutoSize = true, Margin = new Padding(16, 0, 0, 0) };
            top.Controls.Add(exportAll);
            var sharePack = new Button { Text = "Export my textures...", AutoSize = true, Margin = new Padding(8, 0, 0, 0) };
            top.Controls.Add(sharePack);
            var addPack = new Button { Text = "Import a texture pack...", AutoSize = true, Margin = new Padding(8, 0, 0, 0) };
            top.Controls.Add(addPack);
            var openMods = new Button { Text = "Open my textures folder", AutoSize = true, Margin = new Padding(8, 0, 0, 0) };
            top.Controls.Add(openMods);

            sceneInfo = new Label { Dock = DockStyle.Top, Height = 24, Padding = new Padding(10, 4, 10, 0), ForeColor = Color.DimGray, AutoEllipsis = true };
            var side = new Panel { Dock = DockStyle.Right, Width = 380, Padding = new Padding(10) };
            heading = new Label { Font = new Font("Segoe UI", 12f, FontStyle.Bold), AutoSize = false, Height = 28, Dock = DockStyle.Top, Text = "Choose a texture", AutoEllipsis = true };
            info = new Label { AutoSize = false, Height = 78, Dock = DockStyle.Top };
            original = new PictureBox { Height = 300, Dock = DockStyle.Top, SizeMode = PictureBoxSizeMode.Zoom, BackgroundImage = Checker(), BorderStyle = BorderStyle.FixedSingle };
            replacedLabel = new Label { AutoSize = false, Height = 26, Dock = DockStyle.Top, TextAlign = ContentAlignment.BottomLeft };
            replacement = new PictureBox { Height = 150, Dock = DockStyle.Top, SizeMode = PictureBoxSizeMode.Zoom, BackgroundImage = Checker(), BorderStyle = BorderStyle.FixedSingle, Visible = false };
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 80, FlowDirection = FlowDirection.TopDown };
            saveCopy = new Button { Text = "Save a copy to edit...", Width = 220, Enabled = false };
            replace = new Button { Text = "Replace with my texture...", Width = 220, Enabled = false };
            useOriginal = new Button { Text = "Use the original again", Width = 220, Enabled = false };
            buttons.Controls.AddRange(new Control[] { saveCopy, replace, useOriginal });
            buttons.Height = 100;
            side.Controls.Add(replacement); side.Controls.Add(replacedLabel); side.Controls.Add(original);
            side.Controls.Add(info); side.Controls.Add(heading); side.Controls.Add(buttons);

            thumbs = new ImageList { ImageSize = new Size(112, 112), ColorDepth = ColorDepth.Depth32Bit };
            list = new ListView { Dock = DockStyle.Fill, LargeImageList = thumbs, View = View.LargeIcon, MultiSelect = false, HideSelection = false, ShowGroups = true };

            var strip = new StatusStrip();
            status = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
            strip.Items.Add(status);

            Controls.Add(list); Controls.Add(side); Controls.Add(sceneInfo); Controls.Add(top); Controls.Add(strip);

            Text = Title + " - " + Mods.GameName(shell);
            // only scenes that hold textures (a few scenes have none, or one plain fill)
            foreach (string f in Directory.GetFiles(shell, "*.am").OrderBy(x => x, StringComparer.OrdinalIgnoreCase))
            {
                try { if (Am.Scan(File.ReadAllBytes(Mods.CleanScene(f))).Count > 0) sceneBox.Items.Add(Path.GetFileNameWithoutExtension(f)); }
                catch (Exception) { }
            }
            sharePack.Click += (s, e) => SharePack();
            addPack.Click += (s, e) => AddPack();
            sceneBox.SelectedIndexChanged += (s, e) => LoadScene((string)sceneBox.SelectedItem);
            onlyReplaced.CheckedChanged += (s, e) => FillList();
            list.SelectedIndexChanged += (s, e) => ShowSelected();
            saveCopy.Click += (s, e) => SaveCopy();
            replace.Click += (s, e) => Replace();
            useOriginal.Click += (s, e) => UseOriginal();
            exportAll.Click += (s, e) => ExportAll();
            openMods.Click += (s, e) =>
            {
                string dir = scene == null ? Path.Combine(shell, "Mods") : Mods.ModsDir(shell, scene);
                Directory.CreateDirectory(dir);
                Process.Start("explorer.exe", "\"" + dir + "\"");
            };
            int sel = startScene != null ? sceneBox.Items.IndexOf(startScene) : -1;
            if (sel < 0) sel = sceneBox.Items.IndexOf("NASCAR_Selection");
            sceneBox.SelectedIndex = sel >= 0 ? sel : (sceneBox.Items.Count > 0 ? 0 : -1);
            if (startPicture >= 0) Shown += (s, e) => Reselect(startPicture);
        }

        static Bitmap Checker()
        {
            var b = new Bitmap(16, 16);
            using (var g = Graphics.FromImage(b))
            {
                g.Clear(Color.FromArgb(200, 200, 200));
                g.FillRectangle(Brushes.White, 0, 0, 8, 8); g.FillRectangle(Brushes.White, 8, 8, 8, 8);
            }
            return b;
        }

        void LoadScene(string name)
        {
            if (name == null) return;
            Cursor = Cursors.WaitCursor;
            try
            {
                scene = name;
                string am = Path.Combine(shell, name + ".am");
                data = File.ReadAllBytes(Mods.CleanScene(am));
                pictures = Am.Scan(data);
                thumbs.Images.Clear();
                foreach (Picture p in pictures)
                {
                    using (Bitmap b = Am.Decode(data, p)) thumbs.Images.Add(Thumb(b));
                }
                // what this scene shows, read from the scene itself: the folders its textures were made in
                var groups = pictures.GroupBy(p => GroupName(p)).OrderByDescending(g => g.Count())
                                     .Select(g => g.Key + " (" + g.Count() + ")");
                sceneInfo.Text = "Groups in this scene, by the folder the textures came from: " + string.Join(", ", groups);
                FillList();
                status.Text = pictures.Count + " textures in " + name + ". Choose one to see it larger.";
            }
            catch (Exception e)
            {
                MessageBox.Show("This scene could not be read: " + e.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            finally { Cursor = Cursors.Default; }
        }

        Bitmap Thumb(Bitmap b)
        {
            var t = new Bitmap(112, 112);
            using (var g = Graphics.FromImage(t))
            {
                using (var tb = new TextureBrush(Checker())) g.FillRectangle(tb, 0, 0, 112, 112);
                float s = Math.Min(112f / b.Width, 112f / b.Height);
                int w = Math.Max(1, (int)(b.Width * s)), h = Math.Max(1, (int)(b.Height * s));
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(b, (112 - w) / 2, (112 - h) / 2, w, h);
            }
            return t;
        }

        static string GroupName(Picture p)
        {
            string f = p.Folder;
            return f.Length > 0 ? f : "Other";
        }

        void FillList()
        {
            list.BeginUpdate();
            list.Items.Clear();
            list.Groups.Clear();
            var groups = new Dictionary<string, ListViewGroup>(StringComparer.OrdinalIgnoreCase);
            foreach (var g in pictures.GroupBy(p => GroupName(p), StringComparer.OrdinalIgnoreCase).OrderByDescending(g => g.Count()))
            {
                var lg = new ListViewGroup(g.Key, g.Key + "  (" + g.Count() + ")");
                groups[g.Key] = lg; list.Groups.Add(lg);
            }
            foreach (Picture p in pictures)
            {
                bool rep = Mods.FindReplacement(shell, scene, p) != null;
                if (onlyReplaced.Checked && !rep) continue;
                string name = p.Name.Length > 22 ? p.Name.Substring(0, 21) + "..." : p.Name;
                var it = new ListViewItem((rep ? "* " : "") + p.Index + (name.Length > 0 ? " " + name : ""), p.Index) { Tag = p };
                it.ToolTipText = p.Name + " (" + p.Width + "x" + p.Height + ")";
                it.Group = groups[GroupName(p)];
                if (rep) it.ForeColor = Color.DarkGreen;
                list.Items.Add(it);
            }
            list.ShowItemToolTips = true;
            list.EndUpdate();
        }

        Picture Selected { get { return list.SelectedItems.Count == 0 ? null : (Picture)list.SelectedItems[0].Tag; } }

        void ShowSelected()
        {
            Picture p = Selected;
            saveCopy.Enabled = replace.Enabled = p != null;
            if (p == null) { useOriginal.Enabled = false; return; }
            heading.Text = "Texture " + p.Index + (p.Name.Length > 0 ? ": " + p.Name : "");
            info.Text = p.Width + " x " + p.Height + " pixels, " + p.Kind + (p.HasAlpha ? ", with transparency" : "") +
                        "\nGroup: " + GroupName(p) + (p.SourcePath.Length > 0 ? "   (made from " + Path.GetFileName(p.SourcePath) + ")" : "") +
                        "\nSpace in the scene: " + (p.TotalLength / 1024) + " KB" +
                        "\nReplacement file: " + p.Key + ".png";
            if (original.Image != null) original.Image.Dispose();
            original.Image = Am.Decode(data, p);
            string rep = Mods.FindReplacement(shell, scene, p);
            useOriginal.Enabled = rep != null;
            if (replacement.Image != null) { replacement.Image.Dispose(); replacement.Image = null; }
            if (rep != null)
            {
                replacedLabel.Text = "Replaced with:";
                try { replacement.Image = Am.LoadFile(rep); } catch { }
                replacement.Visible = true;
            }
            else { replacedLabel.Text = "Not replaced."; replacement.Visible = false; }
        }

        void SaveCopy()
        {
            Picture p = Selected; if (p == null) return;
            using (var d = new SaveFileDialog { Filter = "PNG texture|*.png", FileName = scene + "_" + p.Index.ToString("000") + ".png" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                using (Bitmap b = Am.Decode(data, p)) b.Save(d.FileName, System.Drawing.Imaging.ImageFormat.Png);
                status.Text = "Saved " + Path.GetFileName(d.FileName) + ". Edit it, keep the size " + p.Width + "x" + p.Height + ", then use Replace.";
            }
        }

        void Replace()
        {
            Picture p = Selected; if (p == null) return;
            using (var d = new OpenFileDialog { Filter = "Textures|*.png;*.jpg;*.jpeg;*.bmp" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                Bitmap img;
                try { img = Am.LoadFile(d.FileName); }
                catch (Exception e) { MessageBox.Show("This file could not be opened as a texture: " + e.Message, Title); return; }
                try
                {
                    if (img.Width != p.Width || img.Height != p.Height)
                    {
                        var answer = MessageBox.Show(string.Format("Your texture is {0} x {1}. This one must be {2} x {3}.\n\nResize your texture to {2} x {3}?",
                            img.Width, img.Height, p.Width, p.Height), Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                        if (answer != DialogResult.Yes) return;
                        Bitmap r = Am.Resize(img, p.Width, p.Height); img.Dispose(); img = r;
                    }
                    string note;
                    Cursor = Cursors.WaitCursor;
                    Mods.SaveReplacement(shell, scene, p, img, out note);
                    Cursor = Cursors.Default;
                    status.Text = "Texture " + p.Index + " replaced (" + note + "). Start the game to see it.";
                }
                catch (InvalidOperationException e)
                {
                    Cursor = Cursors.Default;
                    MessageBox.Show("This texture cannot be used: " + e.Message + ".\n\nTry a simpler texture, with fewer fine details.", Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                finally { img.Dispose(); }
            }
            int keep = p.Index;
            FillList();
            Reselect(keep);
        }

        void UseOriginal()
        {
            Picture p = Selected; if (p == null) return;
            Mods.RemoveReplacement(shell, scene, p);
            status.Text = "Texture " + p.Index + " is the original again from the next game start.";
            int keep = p.Index;
            FillList();
            Reselect(keep);
        }

        void Reselect(int index)
        {
            foreach (ListViewItem it in list.Items)
                if (((Picture)it.Tag).Index == index) { it.Selected = true; it.EnsureVisible(); return; }
            ShowSelected();
        }

        void SharePack()
        {
            string game = Mods.GameName(shell);
            using (var d = new SaveFileDialog { Filter = "Texture pack (zip)|*.zip", FileName = game + " menu textures.zip" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                int n = Mods.ExportPack(shell, d.FileName);
                if (n == 0) { File.Delete(d.FileName); MessageBox.Show("You have not replaced any texture yet, so there is nothing to export.", Title); return; }
                status.Text = n + " textures saved to " + Path.GetFileName(d.FileName) + ". Send this file to other " + game + " players.";
                MessageBox.Show(n + " of your textures are in\n" + d.FileName + "\n\nOther " + game + " players add it with \"Import a texture pack...\".", Title);
            }
        }

        void AddPack()
        {
            using (var d = new OpenFileDialog { Filter = "Texture pack (zip)|*.zip" })
            {
                if (d.ShowDialog(this) != DialogResult.OK) return;
                string game = Mods.GameName(shell);
                try
                {
                    string packGame = Mods.PackGame(d.FileName);
                    if (packGame.Length > 0 && packGame != game &&
                        MessageBox.Show("This pack was made for " + packGame + ", and this is " + game + ".\nTextures that do not fit this game are skipped.\n\nImport it anyway?",
                                        Title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
                    bool replaceMine = true;
                    int clash = Mods.PackConflicts(shell, d.FileName);
                    if (clash > 0)
                    {
                        var a = MessageBox.Show("You already replaced " + clash + " of the textures in this pack.\n\nYes = use the pack's textures for those\nNo = keep yours",
                                                Title, MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                        if (a == DialogResult.Cancel) return;
                        replaceMine = a == DialogResult.Yes;
                    }
                    Cursor = Cursors.WaitCursor;
                    Mods.PackResult r = Mods.ImportPack(shell, d.FileName, replaceMine);
                    Cursor = Cursors.Default;
                    string msg = r.Added + " textures added" + (r.Replaced > 0 ? " (" + r.Replaced + " of them replace yours)" : "") +
                                 (r.Skipped > 0 ? ", " + r.Skipped + " skipped" : "") + ".\nStart the game to see them.";
                    if (r.Notes.Count > 0) msg += "\n\nSkipped:\n" + string.Join("\n", r.Notes.Take(12)) + (r.Notes.Count > 12 ? "\n..." : "");
                    MessageBox.Show(msg, Title, MessageBoxButtons.OK, r.Added > 0 ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
                    status.Text = r.Added + " textures added from " + Path.GetFileName(d.FileName) + ".";
                }
                catch (Exception ex)
                {
                    Cursor = Cursors.Default;
                    MessageBox.Show("This file could not be read as a texture pack: " + ex.Message, Title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
            }
            int keep = Selected == null ? -1 : Selected.Index;
            FillList();
            if (keep >= 0) Reselect(keep);
        }

        void ExportAll()
        {
            using (var fb = new FolderBrowserDialog { Description = "Choose a folder for all textures of " + scene + "." })
            {
                if (fb.ShowDialog(this) != DialogResult.OK) return;
                Cursor = Cursors.WaitCursor;
                foreach (Picture p in pictures)
                    using (Bitmap b = Am.Decode(data, p)) b.Save(Path.Combine(fb.SelectedPath, scene + "_" + p.Index.ToString("000") + ".png"), System.Drawing.Imaging.ImageFormat.Png);
                Cursor = Cursors.Default;
                status.Text = pictures.Count + " textures saved to " + fb.SelectedPath + ".";
            }
        }
    }
}
