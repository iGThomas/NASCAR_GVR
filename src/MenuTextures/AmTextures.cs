// AmPictures.cs - reading, fitting and replacing the textures inside an Anark .am menu scene.
//
// Layout (both NASCAR games, every scene): each texture is a 64-byte header whose first 4 bytes at
// offset -64 hold (texture bytes + 64), followed by either one PNG, one JPEG, or a colour JPEG directly
// followed by a grey JPEG that is its transparency mask (one header covers both). A replacement must
// keep the exact length of each part, so nothing else in the file moves. The free space is hidden
// inside the texture: a private "gvRp" chunk before IEND in a PNG, a comment segment after SOI in a JPEG.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Media.Imaging;
using WpfPixelFormats = System.Windows.Media.PixelFormats;

namespace MenuTextures
{
    public class Part
    {
        public int Start, End;
        public bool Png;
        public int Length { get { return End - Start; } }
    }

    public class Picture
    {
        public int Index;
        public List<Part> Parts = new List<Part>();
        public int Width, Height;
        public bool HasAlpha;
        public int PngColorType = -1;   // PNG: 0 grey, 2 RGB, 3 palette, 4 grey+alpha, 6 RGBA
        public int JpegComponents;      // JPEG colour part: 1 grey, 3 colour
        public uint Crc;
        public string Name = "";        // the scene's own name for it, e.g. "CarSkin16" (empty when the scene has none)
        public string SourcePath = "";  // the artist's original file, e.g. ...\textures\DriverSelect\CarSkin16.tga
        public string Folder            // the folder of that file = the screen it belongs to, e.g. "DriverSelect"
        {
            get
            {
                if (SourcePath.Length > 0)
                {
                    string[] parts = SourcePath.Split('\\', '/');
                    if (parts.Length >= 2) return parts[parts.Length - 2];
                }
                return Name.StartsWith("Map_#") ? "3D models" : "";
            }
        }
        public string Kind
        {
            get
            {
                if (Parts[0].Png) return "PNG";
                return Parts.Count == 2 ? "JPEG + mask" : "JPEG";
            }
        }
        public int TotalLength { get { int n = 0; foreach (Part p in Parts) n += p.Length; return n; } }
        // replacement file name: number, the scene's name for the texture (readable), fingerprint of the original
        public string Key
        {
            get
            {
                var sb = new StringBuilder();
                foreach (char c in Name) if (char.IsLetterOrDigit(c) || c == '-') sb.Append(c);
                string n = sb.Length > 40 ? sb.ToString(0, 40) : sb.ToString();
                return Index.ToString("000") + "_" + (n.Length > 0 ? n + "_" : "") + Crc.ToString("X8");
            }
        }
    }

    public static class Am
    {
        // ------------------------------------------------------------------ scanning
        static int JpegEnd(byte[] d, int i)
        {
            int k = i + 2;
            while (k < d.Length - 4)
            {
                if (d[k] != 0xFF) return -1;
                int m = d[k + 1];
                if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { k += 2; continue; }
                int len = (d[k + 2] << 8) | d[k + 3];
                if (m == 0xDA)
                {
                    k += 2 + len;
                    while (k < d.Length - 1)
                    {
                        if (d[k] == 0xFF && d[k + 1] != 0 && !(d[k + 1] >= 0xD0 && d[k + 1] <= 0xD7))
                            return d[k + 1] == 0xD9 ? k + 2 : -1;
                        k++;
                    }
                    return -1;
                }
                k += 2 + len;
            }
            return -1;
        }

        static int Find(byte[] d, byte[] sig, int from)
        {
            for (int i = from; i <= d.Length - sig.Length; i++)
            {
                if (d[i] != sig[0]) continue;
                int j = 1;
                while (j < sig.Length && d[i + j] == sig[j]) j++;
                if (j == sig.Length) return i;
            }
            return -1;
        }

        static readonly byte[] PngSig = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        static readonly byte[] JpgSig = { 0xFF, 0xD8, 0xFF };
        static readonly byte[] Iend = { 0x49, 0x45, 0x4E, 0x44 };

        public static List<Picture> Scan(byte[] d)
        {
            var blobs = new List<Part>();
            int i = 0;
            while ((i = Find(d, PngSig, i)) >= 0)
            {
                int j = Find(d, Iend, i);
                if (j < 0) break;
                blobs.Add(new Part { Start = i, End = j + 8, Png = true });
                i = j + 8;
            }
            i = 0;
            while ((i = Find(d, JpgSig, i)) >= 0)
            {
                int e = JpegEnd(d, i);
                if (e < 0) { i += 3; continue; }
                blobs.Add(new Part { Start = i, End = e, Png = false });
                i = e;
            }
            blobs.Sort((a, b) => a.Start.CompareTo(b.Start));
            var pics = new List<Picture>();
            for (int k = 0; k < blobs.Count; k++)
            {
                Part b = blobs[k];
                if (b.Start < 64) continue;
                int head = BitConverter.ToInt32(d, b.Start - 64);
                var p = new Picture { Index = pics.Count };
                if (head == b.Length + 64) p.Parts.Add(b);
                else if (!b.Png && k + 1 < blobs.Count && !blobs[k + 1].Png && blobs[k + 1].Start == b.End &&
                         head == blobs[k + 1].End - b.Start + 64)
                {
                    p.Parts.Add(b); p.Parts.Add(blobs[k + 1]); k++;
                }
                else continue;                     // not a texture record: leave it alone
                ReadInfo(d, p);
                p.Crc = Crc32(d, p.Parts[0].Start, p.Parts[p.Parts.Count - 1].End - p.Parts[0].Start);
                pics.Add(p);
            }
            if (pics.Count > 0) ReadNames(d, pics);
            return pics;
        }

        // Names. The scene is a list of tagged records (2-byte tag, 4-byte length, value). Before each texture, tag
        // 0x0BC1 holds the texture's object number. In the object part of the file (before the first texture), the
        // object that uses it holds that number under tag 0x0BB9, directly followed by its name (tag 0x1194, UTF-16)
        // and, for textures made from a file, the key "AKSourceFilePath" (tag 0x0FA1) with the path (tag 0x0FA3).
        static readonly byte[] ObjTag = { 0xC1, 0x0B, 0x04, 0x00, 0x00, 0x00 };
        static readonly byte[] UseTag = { 0xB9, 0x0B, 0x04, 0x00, 0x00, 0x00 };
        static readonly byte[] PathKey = Encoding.Unicode.GetBytes("AKSourceFilePath");

        static void ReadNames(byte[] d, List<Picture> pics)
        {
            int first = pics[0].Parts[0].Start;
            var uses = new Dictionary<int, int>();             // object number -> first place it is used
            for (int i = 0; (i = Find(d, UseTag, i)) >= 0 && i < first; i += 6)
            {
                int id = BitConverter.ToInt32(d, i + 6);
                if (!uses.ContainsKey(id)) uses[id] = i;
            }
            foreach (Picture p in pics)
            {
                try
                {
                    int s = p.Parts[0].Start, j = -1;
                    for (int k = s - 6; k > Math.Max(0, s - 2000); k--)
                        if (d[k] == 0xC1 && d[k + 1] == 0x0B && d[k + 2] == 4 && d[k + 3] == 0 && d[k + 4] == 0 && d[k + 5] == 0) { j = k; break; }
                    if (j < 0) continue;
                    int use;
                    if (!uses.TryGetValue(BitConverter.ToInt32(d, j + 6), out use)) continue;
                    int n = use + 10;
                    if (d[n] != 0x94 || d[n + 1] != 0x11) continue;
                    int len = BitConverter.ToInt32(d, n + 2);
                    if (len <= 0 || len > 512) continue;
                    p.Name = Encoding.Unicode.GetString(d, n + 6, len).TrimEnd('\0');
                    // a few small records later: the source file key (tag 0x0FA1) and its path (tag 0x0FA3), when there is one
                    int from = n + 6 + len, k2 = -1;
                    for (int q = from; q < Math.Min(d.Length - 8, from + 160); q++)
                        if (d[q] == 0xA1 && d[q + 1] == 0x0F && Matches(d, q + 6, PathKey)) { k2 = q; break; }
                    if (k2 >= 0 && d[k2 + 6 + PathKey.Length] == 0xA3 && d[k2 + 7 + PathKey.Length] == 0x0F)
                    {
                        int v = k2 + 6 + PathKey.Length;
                        int plen = BitConverter.ToInt32(d, v + 2);
                        if (plen > 1 && plen < 1024) p.SourcePath = Encoding.Unicode.GetString(d, v + 7, plen - 1).TrimEnd('\0');
                    }
                }
                catch (ArgumentException) { }
                catch (IndexOutOfRangeException) { }
            }
        }

        static bool Matches(byte[] d, int at, byte[] what)
        {
            if (at < 0 || at + what.Length > d.Length) return false;
            for (int i = 0; i < what.Length; i++) if (d[at + i] != what[i]) return false;
            return true;
        }

        static void ReadInfo(byte[] d, Picture p)
        {
            Part a = p.Parts[0];
            if (a.Png)
            {
                p.Width = BigInt(d, a.Start + 16); p.Height = BigInt(d, a.Start + 20);
                p.PngColorType = d[a.Start + 25];
                p.HasAlpha = p.PngColorType == 4 || p.PngColorType == 6 || Find(Slice(d, a.Start, a.Length), Encoding.ASCII.GetBytes("tRNS"), 0) >= 0;
                return;
            }
            int k = a.Start + 2;
            while (k < a.End - 9)
            {
                int m = d[k + 1];
                int len = (d[k + 2] << 8) | d[k + 3];
                if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
                {
                    p.Height = (d[k + 5] << 8) | d[k + 6]; p.Width = (d[k + 7] << 8) | d[k + 8];
                    p.JpegComponents = d[k + 9];
                    break;
                }
                k += 2 + len;
            }
            p.HasAlpha = p.Parts.Count == 2;
        }

        static int BigInt(byte[] d, int o) { return (d[o] << 24) | (d[o + 1] << 16) | (d[o + 2] << 8) | d[o + 3]; }
        public static byte[] Slice(byte[] d, int start, int len) { var r = new byte[len]; Buffer.BlockCopy(d, start, r, 0, len); return r; }

        // ------------------------------------------------------------------ decoding to a 32-bit bitmap
        // The scenes store every texture upside down (bottom row first, as the 3D engine wants it). Decode
        // returns it the right way up, and Fit turns a right-way-up texture back before encoding, so players
        // only ever see and edit upright textures.
        public static Bitmap Decode(byte[] d, Picture p)
        {
            Bitmap colour = Load(Slice(d, p.Parts[0].Start, p.Parts[0].Length));
            Bitmap result = colour;
            if (p.Parts.Count == 2)
            {
                Bitmap mask = Load(Slice(d, p.Parts[1].Start, p.Parts[1].Length));
                int[] c = Pixels(colour), m = Pixels(mask);
                for (int i = 0; i < c.Length && i < m.Length; i++)
                    c[i] = (c[i] & 0x00FFFFFF) | ((m[i] & 0xFF) << 24);   // the mask's grey level is the alpha
                colour.Dispose(); mask.Dispose();
                result = FromPixels(c, p.Width, p.Height);
            }
            result.RotateFlip(RotateFlipType.RotateNoneFlipY);
            if (!p.Parts[0].Png) return result;
            int[] px = Pixels(result);
            SwapRedBlue(px);
            result.Dispose();
            return FromPixels(px, p.Width, p.Height);
        }

        // PNG textures (only those) are also stored with red and blue swapped: blue-green tire lettering, blue faces.
        // Checked against the separate Shell\Textures\CarSkinNN.jpg files: the scene's PNG car paint matches only
        // after the swap, its JPEG car paints match without it. Swapping again gives the real colours; the same swap
        // before encoding a PNG puts them back the way the menus expect.
        public static void SwapRedBlue(int[] px)
        {
            for (int i = 0; i < px.Length; i++)
            {
                int c = px[i];
                px[i] = (c & unchecked((int)0xFF00FF00)) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
            }
        }

        public static Bitmap Load(byte[] blob)
        {
            using (var ms = new MemoryStream(blob))
            using (var src = new Bitmap(ms))
            {
                var bmp = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(bmp)) { g.DrawImage(src, 0, 0, src.Width, src.Height); }
                return bmp;
            }
        }

        public static Bitmap LoadFile(string path)
        {
            return Load(File.ReadAllBytes(path));
        }

        public static int[] Pixels(Bitmap b)
        {
            var r = new Rectangle(0, 0, b.Width, b.Height);
            BitmapData bd = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var px = new int[b.Width * b.Height];
            for (int y = 0; y < b.Height; y++) Marshal.Copy(bd.Scan0 + y * bd.Stride, px, y * b.Width, b.Width);
            b.UnlockBits(bd);
            return px;
        }

        public static Bitmap FromPixels(int[] px, int w, int h)
        {
            var b = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            BitmapData bd = b.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            for (int y = 0; y < h; y++) Marshal.Copy(px, y * w, bd.Scan0 + y * bd.Stride, w);
            b.UnlockBits(bd);
            return b;
        }

        public static Bitmap Resize(Bitmap src, int w, int h)
        {
            var b = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(b))
            {
                g.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                using (var ia = new ImageAttributes())
                {
                    ia.SetWrapMode(System.Drawing.Drawing2D.WrapMode.TileFlipXY);
                    g.DrawImage(src, new Rectangle(0, 0, w, h), 0, 0, src.Width, src.Height, GraphicsUnit.Pixel, ia);
                }
            }
            return b;
        }

        // ------------------------------------------------------------------ fitting a new texture into a slot
        // Returns the bytes for each part (each exactly as long as the original part) and a note on what was
        // needed to make it fit. Throws InvalidOperationException when it cannot fit.
        public static List<byte[]> Fit(Bitmap img, Picture p, out string note)
        {
            if (img.Width != p.Width || img.Height != p.Height)
                throw new InvalidOperationException(string.Format("the texture is {0}x{1}, it must be {2}x{3}", img.Width, img.Height, p.Width, p.Height));
            int[] px;
            using (Bitmap stored = (Bitmap)img.Clone())
            {
                stored.RotateFlip(RotateFlipType.RotateNoneFlipY);     // back to the scene's upside-down order
                px = Pixels(stored);
            }
            if (p.Parts[0].Png) SwapRedBlue(px);                       // PNG: back to its red/blue order
            var parts = new List<byte[]>();
            if (p.Parts[0].Png)
            {
                parts.Add(FitPng(px, p, p.Parts[0].Length, out note));
                return parts;
            }
            int quality;
            byte[] colour = FitJpeg(px, p.Width, p.Height, p.JpegComponents == 1, p.Parts[0].Length, false, out quality);
            note = "JPEG quality " + quality;
            parts.Add(colour);
            if (p.Parts.Count == 2)
            {
                int q2;
                parts.Add(FitJpeg(px, p.Width, p.Height, true, p.Parts[1].Length, true, out q2));
                note += ", transparency quality " + q2;
            }
            return parts;
        }

        static byte[] FitPng(int[] px, Picture p, int slot, out string note)
        {
            bool alpha = p.HasAlpha;
            bool grey = p.PngColorType == 0 || p.PngColorType == 4;
            if (grey) for (int i = 0; i < px.Length && grey; i++) { int c = px[i]; if (((c >> 16) & 255) != ((c >> 8) & 255) || ((c >> 8) & 255) != (c & 255)) grey = false; }
            byte[] png = Png.Encode(px, p.Width, p.Height, alpha, grey);
            if (Fits(png.Length, slot, 12)) { note = "full quality"; return PadPng(png, slot); }
            foreach (int colours in new[] { 256, 128, 64, 32, 16 })
            {
                png = Png.EncodePalette(px, p.Width, p.Height, alpha, colours);
                if (Fits(png.Length, slot, 12)) { note = "reduced to " + colours + " colours to fit the space"; return PadPng(png, slot); }
            }
            throw new InvalidOperationException("the texture has too much detail to fit, even with 16 colours");
        }

        static bool Fits(int len, int slot, int minPad) { return len == slot || len + minPad <= slot; }

        static byte[] PadPng(byte[] png, int slot)
        {
            int gap = slot - png.Length;
            if (gap == 0) return png;
            int dataLen = gap - 12;
            var chunk = new byte[gap];
            WriteBig(chunk, 0, dataLen);
            chunk[4] = (byte)'g'; chunk[5] = (byte)'v'; chunk[6] = (byte)'R'; chunk[7] = (byte)'p';
            WriteBig(chunk, 8 + dataLen, (int)Crc32(chunk, 4, 4 + dataLen));
            int iend = png.Length - 12;                       // IEND chunk = 12 bytes at the end
            var r = new byte[slot];
            Buffer.BlockCopy(png, 0, r, 0, iend);
            Buffer.BlockCopy(chunk, 0, r, iend, gap);
            Buffer.BlockCopy(png, iend, r, iend + gap, 12);
            return r;
        }

        static byte[] FitJpeg(int[] px, int w, int h, bool grey, int slot, bool alphaAsGrey, out int quality)
        {
            for (quality = 95; quality >= 5; quality -= 5)
            {
                byte[] j = Jpeg.Encode(px, w, h, grey, alphaAsGrey, quality);
                if (Fits(j.Length, slot, 4)) return PadJpeg(j, slot);
            }
            if (slot < 1024)
                throw new InvalidOperationException("its space in the scene is only " + slot + " bytes, too small for any new JPEG file");
            throw new InvalidOperationException("the texture has too much detail to fit, even at the lowest JPEG quality");
        }

        static byte[] PadJpeg(byte[] j, int slot)
        {
            int gap = slot - j.Length;
            if (gap == 0) return j;
            var r = new byte[slot];
            r[0] = 0xFF; r[1] = 0xD8;                         // SOI, then a comment segment of exactly gap bytes
            r[2] = 0xFF; r[3] = 0xFE; r[4] = (byte)((gap - 2) >> 8); r[5] = (byte)((gap - 2) & 255);
            Buffer.BlockCopy(j, 2, r, 2 + gap, j.Length - 2);
            return r;
        }

        public static void WriteBig(byte[] b, int o, int v) { b[o] = (byte)(v >> 24); b[o + 1] = (byte)(v >> 16); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v; }

        // ------------------------------------------------------------------ CRC-32 (PNG / zip polynomial)
        static uint[] crcTable;
        public static uint Crc32(byte[] d, int start, int len)
        {
            if (crcTable == null)
            {
                crcTable = new uint[256];
                for (uint n = 0; n < 256; n++) { uint c = n; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crcTable[n] = c; }
            }
            uint crc = 0xFFFFFFFFu;
            for (int i = start; i < start + len; i++) crc = crcTable[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
            return crc ^ 0xFFFFFFFFu;
        }
    }

    // ---------------------------------------------------------------------- PNG writer
    public static class Png
    {
        public static byte[] Encode(int[] px, int w, int h, bool alpha, bool grey)
        {
            int bpp = grey ? (alpha ? 2 : 1) : (alpha ? 4 : 3);
            int colorType = grey ? (alpha ? 4 : 0) : (alpha ? 6 : 2);
            var raw = new byte[h * w * bpp];
            int o = 0;
            foreach (int c in px)
            {
                byte a = (byte)(c >> 24), r = (byte)(c >> 16), g = (byte)(c >> 8), b = (byte)c;
                if (grey) { raw[o++] = r; if (alpha) raw[o++] = a; }
                else { raw[o++] = r; raw[o++] = g; raw[o++] = b; if (alpha) raw[o++] = a; }
            }
            return Build(w, h, 8, colorType, raw, bpp, null, null);
        }

        public static byte[] EncodePalette(int[] px, int w, int h, bool alpha, int colours)
        {
            int[] palette; byte[] index;
            Quantizer.Run(px, alpha, colours, out palette, out index);
            var plte = new byte[palette.Length * 3];
            var trns = new byte[palette.Length];
            bool anyAlpha = false;
            for (int i = 0; i < palette.Length; i++)
            {
                plte[i * 3] = (byte)(palette[i] >> 16); plte[i * 3 + 1] = (byte)(palette[i] >> 8); plte[i * 3 + 2] = (byte)palette[i];
                trns[i] = (byte)(palette[i] >> 24); if (trns[i] != 255) anyAlpha = true;
            }
            return Build(w, h, 8, 3, index, 1, plte, anyAlpha ? trns : null);
        }

        static byte[] Build(int w, int h, int depth, int colorType, byte[] raw, int bpp, byte[] plte, byte[] trns)
        {
            int stride = w * bpp;
            var filtered = new byte[h * (stride + 1)];
            var prev = new byte[stride];
            var cur = new byte[stride];
            var best = new byte[stride];
            var tryRow = new byte[stride];
            for (int y = 0; y < h; y++)
            {
                Buffer.BlockCopy(raw, y * stride, cur, 0, stride);
                int bestType = 0; long bestSum = long.MaxValue;
                for (int f = 0; f < 5; f++)
                {
                    if (colorType == 3 && f != 0) break;     // palette rows: no filter (recommended)
                    long sum = 0;
                    for (int x = 0; x < stride; x++)
                    {
                        int a = x >= bpp ? cur[x - bpp] : 0, b = prev[x], c = x >= bpp ? prev[x - bpp] : 0;
                        int v;
                        switch (f)
                        {
                            case 0: v = cur[x]; break;
                            case 1: v = cur[x] - a; break;
                            case 2: v = cur[x] - b; break;
                            case 3: v = cur[x] - ((a + b) >> 1); break;
                            default: v = cur[x] - Paeth(a, b, c); break;
                        }
                        byte bv = (byte)v; tryRow[x] = bv;
                        sum += bv < 128 ? bv : 256 - bv;
                    }
                    if (sum < bestSum) { bestSum = sum; bestType = f; Buffer.BlockCopy(tryRow, 0, best, 0, stride); }
                }
                filtered[y * (stride + 1)] = (byte)bestType;
                Buffer.BlockCopy(best, 0, filtered, y * (stride + 1) + 1, stride);
                var t = prev; prev = cur; cur = t;
            }
            var ms = new MemoryStream();
            ms.Write(PngSigBytes, 0, 8);
            var ihdr = new byte[13];
            Am.WriteBig(ihdr, 0, w); Am.WriteBig(ihdr, 4, h); ihdr[8] = (byte)depth; ihdr[9] = (byte)colorType;
            Chunk(ms, "IHDR", ihdr);
            if (plte != null) Chunk(ms, "PLTE", plte);
            if (trns != null) Chunk(ms, "tRNS", trns);
            Chunk(ms, "IDAT", Zlib(filtered));
            Chunk(ms, "IEND", new byte[0]);
            return ms.ToArray();
        }

        static readonly byte[] PngSigBytes = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

        static int Paeth(int a, int b, int c)
        {
            int p = a + b - c, pa = Math.Abs(p - a), pb = Math.Abs(p - b), pc = Math.Abs(p - c);
            return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
        }

        static void Chunk(Stream s, string type, byte[] data)
        {
            var head = new byte[8];
            Am.WriteBig(head, 0, data.Length);
            for (int i = 0; i < 4; i++) head[4 + i] = (byte)type[i];
            s.Write(head, 0, 8);
            s.Write(data, 0, data.Length);
            var both = new byte[4 + data.Length];
            Buffer.BlockCopy(head, 4, both, 0, 4); Buffer.BlockCopy(data, 0, both, 4, data.Length);
            var crc = new byte[4]; Am.WriteBig(crc, 0, (int)Am.Crc32(both, 0, both.Length));
            s.Write(crc, 0, 4);
        }

        static byte[] Zlib(byte[] data)
        {
            var ms = new MemoryStream();
            ms.WriteByte(0x78); ms.WriteByte(0xDA);
            using (var z = new DeflateStream(ms, CompressionLevel.Optimal, true)) z.Write(data, 0, data.Length);
            uint a = 1, b = 0;
            foreach (byte x in data) { a = (a + x) % 65521; b = (b + a) % 65521; }
            var ad = new byte[4]; Am.WriteBig(ad, 0, (int)((b << 16) | a));
            ms.Write(ad, 0, 4);
            return ms.ToArray();
        }
    }

    // ---------------------------------------------------------------------- JPEG writer (Windows' own encoder)
    public static class Jpeg
    {
        public static byte[] Encode(int[] px, int w, int h, bool grey, bool alphaAsGrey, int quality)
        {
            BitmapSource src;
            if (grey)
            {
                var g = new byte[w * h];
                for (int i = 0; i < g.Length; i++)
                {
                    int c = px[i];
                    g[i] = alphaAsGrey ? (byte)(c >> 24)
                                       : (byte)((((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000);
                }
                src = BitmapSource.Create(w, h, 96, 96, WpfPixelFormats.Gray8, null, g, w);
            }
            else
            {
                var b = new byte[w * h * 3];
                for (int i = 0, o = 0; i < w * h; i++) { int c = px[i]; b[o++] = (byte)c; b[o++] = (byte)(c >> 8); b[o++] = (byte)(c >> 16); }
                src = BitmapSource.Create(w, h, 96, 96, WpfPixelFormats.Bgr24, null, b, w * 3);
            }
            var enc = new JpegBitmapEncoder { QualityLevel = quality };
            enc.Frames.Add(BitmapFrame.Create(src));
            using (var ms = new MemoryStream()) { enc.Save(ms); return ms.ToArray(); }
        }
    }

    // ---------------------------------------------------------------------- colour reduction (median cut, RGBA)
    public static class Quantizer
    {
        class Box { public List<int> Colours; public int Weight; }

        public static void Run(int[] px, bool alpha, int max, out int[] palette, out byte[] index)
        {
            // colours are rounded to 5 bits per channel first (at most 32768 per alpha level): fast, and invisible here
            var counts = new Dictionary<int, int>();
            foreach (int c0 in px)
            {
                int c = Key(c0, alpha);
                int n; counts.TryGetValue(c, out n); counts[c] = n + 1;
            }
            var boxes = new List<Box> { new Box { Colours = new List<int>(counts.Keys), Weight = px.Length } };
            while (boxes.Count < max)
            {
                Box split = null; int bestRange = 0, bestCh = 0;
                foreach (Box b in boxes)
                {
                    if (b.Colours.Count < 2) continue;
                    for (int ch = 0; ch < 4; ch++)
                    {
                        if (!alpha && ch == 3) continue;
                        int lo = 255, hi = 0;
                        foreach (int c in b.Colours) { int v = (c >> (ch == 3 ? 24 : 16 - 8 * ch)) & 255; if (v < lo) lo = v; if (v > hi) hi = v; }
                        if (hi - lo > bestRange) { bestRange = hi - lo; bestCh = ch; split = b; }
                    }
                }
                if (split == null) break;
                int shift = bestCh == 3 ? 24 : 16 - 8 * bestCh;
                split.Colours.Sort((x, y) => ((x >> shift) & 255).CompareTo((y >> shift) & 255));
                int total = 0; foreach (int c in split.Colours) total += counts[c];
                int acc = 0, cut = 0;
                for (; cut < split.Colours.Count - 2; cut++) { acc += counts[split.Colours[cut]]; if (acc * 2 >= total) break; }
                var a = new Box { Colours = split.Colours.GetRange(0, cut + 1) };
                var b2 = new Box { Colours = split.Colours.GetRange(cut + 1, split.Colours.Count - cut - 1) };
                boxes.Remove(split); boxes.Add(a); boxes.Add(b2);
            }
            palette = new int[boxes.Count];
            var map = new Dictionary<int, byte>();
            for (int i = 0; i < boxes.Count; i++)
            {
                long sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                foreach (int c in boxes[i].Colours)
                {
                    int k = counts[c];
                    sa += ((c >> 24) & 255) * k; sr += ((c >> 16) & 255) * k; sg += ((c >> 8) & 255) * k; sb += (c & 255) * k; n += k;
                    map[c] = (byte)i;
                }
                palette[i] = (int)((sa / n) << 24 | (sr / n) << 16 | (sg / n) << 8 | (sb / n));
            }
            index = new byte[px.Length];
            for (int i = 0; i < px.Length; i++) index[i] = map[Key(px[i], alpha)];
        }

        static int Key(int c, bool alpha)
        {
            int a = alpha ? (c >> 24) & 255 : 255;
            if (a != 0 && a != 255) a = (a & 0xF8) | 4;            // keep fully clear and fully solid exact
            int rgb = (c & 0x00F8F8F8) | 0x00040404;
            return (a << 24) | rgb;
        }
    }
}
