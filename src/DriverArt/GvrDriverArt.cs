// GvrDriverArt.exe - pictures for the extra playable drivers of NASCAR Racing 1.1.
//
// Everything is built on the player's PC from the game's own files (nothing from the disc ships in
// the download): name plates from letters cut out of the existing plates, car numbers from the
// shape of the number on each car's roof, menu car pictures from the race paints, leaderboard
// badges from the race HUD archive, and the picture sheets the driver select screen loads.
//
//   GvrDriverArt.exe install <install folder> [team racing textures folder]
//       everything; writes ExtraDriverN into nascar_registry.ini
//   GvrDriverArt.exe sheets <install folder> [team racing textures folder]
//       launcher, every start: rebuilds the face / number sheets when Shell\Textures\Faces or
//       Numbers changed, recreates deleted files there and in Names
//
// Players edit Shell\Textures\Faces (256x256), Names (512x64) and Numbers (128x64).
// Data: extra-drivers.txt next to this program. Exit code 0 = ok, 1 = some drivers failed, 2 = error.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace GvrDriverArt
{
    // ---------------------------------------------------------------- image with RGBA bytes
    class Img
    {
        public int W, H; public byte[] P;
        public Img(int w, int h) { W = w; H = h; P = new byte[w * h * 4]; }
        public int I(int x, int y) { return (y * W + x) * 4; }
        public Img Clone() { var o = new Img(W, H); Buffer.BlockCopy(P, 0, o.P, 0, P.Length); return o; }

        public static Img FromBitmap(Bitmap b)
        {
            var o = new Img(b.Width, b.Height);
            using (var c = new Bitmap(b.Width, b.Height, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(c)) { g.CompositingMode = CompositingMode.SourceCopy; g.DrawImage(b, 0, 0, b.Width, b.Height); }
                var d = c.LockBits(new Rectangle(0, 0, c.Width, c.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                var row = new byte[c.Width * 4];
                for (int y = 0; y < c.Height; y++)
                {
                    Marshal.Copy(new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), row, 0, row.Length);
                    for (int x = 0; x < c.Width; x++)
                    {
                        int s = x * 4, t = (y * c.Width + x) * 4;
                        o.P[t] = row[s + 2]; o.P[t + 1] = row[s + 1]; o.P[t + 2] = row[s]; o.P[t + 3] = row[s + 3];
                    }
                }
                c.UnlockBits(d);
            }
            return o;
        }
        public Bitmap ToBitmap()
        {
            var b = new Bitmap(W, H, PixelFormat.Format32bppArgb);
            var d = b.LockBits(new Rectangle(0, 0, W, H), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            var row = new byte[W * 4];
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    int s = (y * W + x) * 4, t = x * 4;
                    row[t] = P[s + 2]; row[t + 1] = P[s + 1]; row[t + 2] = P[s]; row[t + 3] = P[s + 3];
                }
                Marshal.Copy(row, 0, new IntPtr(d.Scan0.ToInt64() + (long)y * d.Stride), row.Length);
            }
            b.UnlockBits(d);
            return b;
        }
        public static Img Load(string path)
        {
            var bytes = File.ReadAllBytes(path);                    // no file lock (players edit these files)
            using (var ms = new MemoryStream(bytes)) using (var b = new Bitmap(ms)) return FromBitmap(b);
        }
        public static Img LoadBytes(byte[] bytes)
        {
            using (var ms = new MemoryStream(bytes)) using (var b = new Bitmap(ms)) return FromBitmap(b);
        }
        public void SavePng(string path)
        {
            using (var b = ToBitmap()) { var tmp = path + ".tmp"; b.Save(tmp, ImageFormat.Png); Replace(tmp, path); }
        }
        public void SaveJpeg(string path, long quality)
        {
            using (var b = ToBitmap())
            using (var rgb = new Bitmap(W, H, PixelFormat.Format24bppRgb))
            {
                using (var g = Graphics.FromImage(rgb)) g.DrawImage(b, 0, 0, W, H);
                var enc = ImageCodecInfo.GetImageEncoders().First(e => e.MimeType == "image/jpeg");
                var ps = new EncoderParameters(1); ps.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, quality);
                var tmp = path + ".tmp"; rgb.Save(tmp, enc, ps); Replace(tmp, path);
            }
        }
        static void Replace(string tmp, string path) { if (File.Exists(path)) File.Delete(path); File.Move(tmp, path); }

        public Img Crop(int x0, int y0, int x1, int y1)          // end exclusive
        {
            var o = new Img(Math.Max(0, x1 - x0), Math.Max(0, y1 - y0));
            for (int y = 0; y < o.H; y++)
                for (int x = 0; x < o.W; x++)
                {
                    int sx = x + x0, sy = y + y0;
                    if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
                    Buffer.BlockCopy(P, I(sx, sy), o.P, o.I(x, y), 4);
                }
            return o;
        }
        public Img Resize(int nw, int nh)
        {
            if (nw == W && nh == H) return Clone();
            using (var src = ToBitmap())
            using (var dst = new Bitmap(nw, nh, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(dst))
                using (var ia = new ImageAttributes())
                {
                    g.CompositingMode = CompositingMode.SourceCopy;
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    ia.SetWrapMode(WrapMode.TileFlipXY);
                    g.DrawImage(src, new Rectangle(0, 0, nw, nh), 0, 0, W, H, GraphicsUnit.Pixel, ia);
                }
                return FromBitmap(dst);
            }
        }
        // PIL paste(src, pos, src): every channel blended by the source alpha
        public void PasteMask(Img s, int ox, int oy)
        {
            for (int y = 0; y < s.H; y++)
                for (int x = 0; x < s.W; x++)
                {
                    int dx = x + ox, dy = y + oy;
                    if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
                    int si = s.I(x, y), di = I(dx, dy); double m = s.P[si + 3] / 255.0;
                    for (int c = 0; c < 4; c++) P[di + c] = (byte)(s.P[si + c] * m + P[di + c] * (1 - m) + 0.5);
                }
        }
        // straight copy (PIL paste without mask)
        public void Paste(Img s, int ox, int oy)
        {
            for (int y = 0; y < s.H; y++)
                for (int x = 0; x < s.W; x++)
                {
                    int dx = x + ox, dy = y + oy;
                    if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
                    Buffer.BlockCopy(s.P, s.I(x, y), P, I(dx, dy), 4);
                }
        }
        // "over" compositing (PIL alpha_composite)
        public void Over(Img s, int ox, int oy)
        {
            for (int y = 0; y < s.H; y++)
                for (int x = 0; x < s.W; x++)
                {
                    int dx = x + ox, dy = y + oy;
                    if (dx < 0 || dy < 0 || dx >= W || dy >= H) continue;
                    int si = s.I(x, y), di = I(dx, dy);
                    double sa = s.P[si + 3] / 255.0, da = P[di + 3] / 255.0, oa = sa + da * (1 - sa);
                    if (oa <= 0) { P[di] = P[di + 1] = P[di + 2] = P[di + 3] = 0; continue; }
                    for (int c = 0; c < 3; c++) P[di + c] = (byte)Math.Min(255, (s.P[si + c] * sa + P[di + c] * da * (1 - sa)) / oa + 0.5);
                    P[di + 3] = (byte)Math.Min(255, oa * 255 + 0.5);
                }
        }
        public byte[] Alpha() { var a = new byte[W * H]; for (int i = 0; i < a.Length; i++) a[i] = P[i * 4 + 3]; return a; }
        public int[] BBox(int thr)                               // alpha > thr; {x0, y0, x1, y1} end exclusive
        {
            int x0 = W, y0 = H, x1 = -1, y1 = -1;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
                    if (P[I(x, y) + 3] > thr) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            return x1 < 0 ? null : new[] { x0, y0, x1 + 1, y1 + 1 };
        }
        public Img CropBBox(int thr) { var b = BBox(thr); return b == null ? this : Crop(b[0], b[1], b[2], b[3]); }
        public static Img Solid(int w, int h, int r, int g, int b, byte[] alpha)
        {
            var o = new Img(w, h);
            for (int i = 0; i < w * h; i++) { o.P[i * 4] = (byte)r; o.P[i * 4 + 1] = (byte)g; o.P[i * 4 + 2] = (byte)b; o.P[i * 4 + 3] = alpha[i]; }
            return o;
        }
    }

    // ---------------------------------------------------------------- 8-bit masks
    static class Mask
    {
        public static byte[] MaxF(byte[] m, int w, int h, int n) { return Rank(m, w, h, n, true); }
        public static byte[] MinF(byte[] m, int w, int h, int n) { return Rank(m, w, h, n, false); }
        static byte[] Rank(byte[] m, int w, int h, int n, bool max)            // square window, separable
        {
            int r = n / 2; var t = new byte[m.Length]; var o = new byte[m.Length];
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int v = max ? 0 : 255;
                    for (int k = -r; k <= r; k++) { int xx = Math.Min(w - 1, Math.Max(0, x + k)); int q = m[y * w + xx]; v = max ? Math.Max(v, q) : Math.Min(v, q); }
                    t[y * w + x] = (byte)v;
                }
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                {
                    int v = max ? 0 : 255;
                    for (int k = -r; k <= r; k++) { int yy = Math.Min(h - 1, Math.Max(0, y + k)); int q = t[yy * w + x]; v = max ? Math.Max(v, q) : Math.Min(v, q); }
                    o[y * w + x] = (byte)v;
                }
            return o;
        }
        public static byte[] Median(byte[] m, int w, int h, int n)
        {
            int r = n / 2, half = (n * n) / 2; var o = new byte[m.Length]; var hist = new int[256];
            for (int y = 0; y < h; y++)
            {
                Array.Clear(hist, 0, 256);
                for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) hist[Px(m, w, h, dx, y + dy)]++;
                for (int x = 0; x < w; x++)
                {
                    if (x > 0)
                        for (int dy = -r; dy <= r; dy++) { hist[Px(m, w, h, x - r - 1, y + dy)]--; hist[Px(m, w, h, x + r, y + dy)]++; }
                    int c = 0, v = 0; for (; v < 256; v++) { c += hist[v]; if (c > half) break; }
                    o[y * w + x] = (byte)Math.Min(255, v);
                }
            }
            return o;
        }
        static byte Px(byte[] m, int w, int h, int x, int y) { x = Math.Min(w - 1, Math.Max(0, x)); y = Math.Min(h - 1, Math.Max(0, y)); return m[y * w + x]; }
        public static byte[] Gauss(byte[] m, int w, int h, double sigma)
        {
            int r = (int)Math.Ceiling(sigma * 3); var k = new double[2 * r + 1]; double s = 0;
            for (int i = -r; i <= r; i++) { k[i + r] = Math.Exp(-i * i / (2 * sigma * sigma)); s += k[i + r]; }
            for (int i = 0; i < k.Length; i++) k[i] /= s;
            var t = new double[m.Length]; var o = new byte[m.Length];
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
                { double v = 0; for (int i = -r; i <= r; i++) { int xx = x + i; if (xx >= 0 && xx < w) v += m[y * w + xx] * k[i + r]; } t[y * w + x] = v; }
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
                { double v = 0; for (int i = -r; i <= r; i++) { int yy = y + i; if (yy >= 0 && yy < h) v += t[yy * w + x] * k[i + r]; } o[y * w + x] = (byte)Math.Min(255, v + 0.5); }
            return o;
        }
        public static byte[] Resize(byte[] m, int w, int h, int nw, int nh)
        {
            var a = Img.Solid(w, h, 255, 255, 255, m).Resize(nw, nh); return a.Alpha();
        }
        public static int[] BBox(byte[] m, int w, int h, int thr)
        {
            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
                    if (m[y * w + x] > thr) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            return x1 < 0 ? null : new[] { x0, y0, x1 + 1, y1 + 1 };
        }
        public static byte[] Crop(byte[] m, int w, int[] b) { int nw = b[2] - b[0], nh = b[3] - b[1]; var o = new byte[nw * nh]; for (int y = 0; y < nh; y++) Buffer.BlockCopy(m, (y + b[1]) * w + b[0], o, y * nw, nw); return o; }
        public static byte[] Pad(byte[] m, int w, int h, int pad) { int nw = w + 2 * pad; var o = new byte[nw * (h + 2 * pad)]; for (int y = 0; y < h; y++) Buffer.BlockCopy(m, y * w, o, (y + pad) * nw + pad, w); return o; }
        // 4-connected components: label per pixel + (count, y0, y1, x0, x1) per label
        public static int[] Components(bool[] m, int w, int h, List<int[]> info)
        {
            var lab = new int[m.Length]; int n = 0; var q = new Queue<int>();
            for (int i = 0; i < m.Length; i++)
            {
                if (!m[i] || lab[i] != 0) continue;
                n++; lab[i] = n; q.Enqueue(i); int cnt = 0, y0 = h, y1 = -1, x0 = w, x1 = -1;
                while (q.Count > 0)
                {
                    int p = q.Dequeue(), py = p / w, px = p % w; cnt++;
                    if (py < y0) y0 = py; if (py > y1) y1 = py; if (px < x0) x0 = px; if (px > x1) x1 = px;
                    if (py > 0 && m[p - w] && lab[p - w] == 0) { lab[p - w] = n; q.Enqueue(p - w); }
                    if (py < h - 1 && m[p + w] && lab[p + w] == 0) { lab[p + w] = n; q.Enqueue(p + w); }
                    if (px > 0 && m[p - 1] && lab[p - 1] == 0) { lab[p - 1] = n; q.Enqueue(p - 1); }
                    if (px < w - 1 && m[p + 1] && lab[p + 1] == 0) { lab[p + 1] = n; q.Enqueue(p + 1); }
                }
                info.Add(new[] { n, cnt, y0, y1, x0, x1 });
            }
            return lab;
        }
    }

    // ---------------------------------------------------------------- readers for the game's own files
    static class Disc
    {
        // race HUD archive: "CUBEMAS4.10", count, data size, 256-byte records {name length, offset,
        // size, packed size, hash, name}, then the zlib-packed files
        public static Dictionary<string, Img> HudCarPlates(string mas)
        {
            var d = File.ReadAllBytes(mas); var o = new Dictionary<string, Img>();
            if (Encoding.ASCII.GetString(d, 0, 11) != "CUBEMAS4.10") throw new Exception("not a CUBEMAS4.10 archive: " + mas);
            int cnt = BitConverter.ToInt32(d, 16), bas = 0x18 + 256 * cnt;
            for (int i = 0; i < cnt; i++)
            {
                int r = 0x18 + 256 * i, nl = BitConverter.ToInt32(d, r), off = BitConverter.ToInt32(d, r + 4),
                    usize = BitConverter.ToInt32(d, r + 8), csize = BitConverter.ToInt32(d, r + 12);
                string name = Encoding.ASCII.GetString(d, r + 20, Math.Min(nl, 200)).Split('\0')[0];
                if (!name.StartsWith("CAR_") || !name.EndsWith(".TGA")) continue;
                byte[] data;
                if (csize != usize)
                {
                    data = new byte[usize];
                    using (var ms = new MemoryStream(d, bas + off + 2, csize - 2))     // skip the 2-byte zlib header
                    using (var z = new DeflateStream(ms, CompressionMode.Decompress))
                    { int got = 0; while (got < usize) { int n = z.Read(data, got, usize - got); if (n <= 0) break; got += n; } }
                }
                else { data = new byte[usize]; Buffer.BlockCopy(d, bas + off, data, 0, usize); }
                o[name.Substring(4, name.Length - 8)] = Tga(data);
            }
            return o;
        }
        public static Img Tga(byte[] d)
        {
            int idLen = d[0], type = d[2], w = BitConverter.ToUInt16(d, 12), h = BitConverter.ToUInt16(d, 14), bpp = d[16], desc = d[17];
            if ((type != 2 && type != 10) || (bpp != 32 && bpp != 24)) throw new Exception("unsupported TGA");
            int bytes = bpp / 8, p = 18 + idLen; var o = new Img(w, h); var px = new byte[w * h * 4]; int n = 0;
            while (n < w * h)
            {
                if (type == 2) { Put(px, n++, d, p, bytes); p += bytes; continue; }
                int c = d[p++]; int run = (c & 0x7F) + 1;
                if ((c & 0x80) != 0) { for (int k = 0; k < run && n < w * h; k++) Put(px, n++, d, p, bytes); p += bytes; }
                else for (int k = 0; k < run && n < w * h; k++) { Put(px, n++, d, p, bytes); p += bytes; }
            }
            bool top = (desc & 0x20) != 0;
            for (int y = 0; y < h; y++) Buffer.BlockCopy(px, y * w * 4, o.P, (top ? y : h - 1 - y) * w * 4, w * 4);
            return o;
        }
        static void Put(byte[] px, int n, byte[] d, int p, int bytes)
        { px[n * 4] = d[p + 2]; px[n * 4 + 1] = d[p + 1]; px[n * 4 + 2] = d[p]; px[n * 4 + 3] = bytes == 4 ? d[p + 3] : (byte)255; }

        // race paints: DXT1 / DXT3 / DXT5 or A8R8G8B8, top mip level only
        public static Img Dds(string path)
        {
            var d = File.ReadAllBytes(path);
            if (Encoding.ASCII.GetString(d, 0, 4) != "DDS ") throw new Exception("not a DDS file: " + path);
            int h = BitConverter.ToInt32(d, 12), w = BitConverter.ToInt32(d, 16); string cc = Encoding.ASCII.GetString(d, 84, 4);
            var o = new Img(w, h); int p = 128;
            if (cc == "DXT1" || cc == "DXT3" || cc == "DXT5")
            {
                for (int by = 0; by < (h + 3) / 4; by++)
                    for (int bx = 0; bx < (w + 3) / 4; bx++)
                    {
                        byte[] alpha = new byte[16]; for (int i = 0; i < 16; i++) alpha[i] = 255;
                        if (cc == "DXT3") { for (int i = 0; i < 16; i++) alpha[i] = (byte)(((d[p + i / 2] >> ((i & 1) * 4)) & 0xF) * 17); p += 8; }
                        else if (cc == "DXT5")
                        {
                            int a0 = d[p], a1 = d[p + 1]; ulong bits = 0; for (int i = 0; i < 6; i++) bits |= (ulong)d[p + 2 + i] << (8 * i);
                            var pal = new int[8]; pal[0] = a0; pal[1] = a1;
                            if (a0 > a1) for (int i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
                            else { for (int i = 1; i < 5; i++) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5; pal[6] = 0; pal[7] = 255; }
                            for (int i = 0; i < 16; i++) alpha[i] = (byte)pal[(int)((bits >> (3 * i)) & 7)];
                            p += 8;
                        }
                        int c0 = d[p] | d[p + 1] << 8, c1 = d[p + 2] | d[p + 3] << 8; uint idx = BitConverter.ToUInt32(d, p + 4); p += 8;
                        var col = new int[4, 4]; Rgb565(c0, col, 0); Rgb565(c1, col, 1);
                        bool four = c0 > c1 || cc != "DXT1";
                        for (int k = 0; k < 3; k++)
                        {
                            if (four) { col[2, k] = (2 * col[0, k] + col[1, k]) / 3; col[3, k] = (col[0, k] + 2 * col[1, k]) / 3; }
                            else { col[2, k] = (col[0, k] + col[1, k]) / 2; col[3, k] = 0; }
                        }
                        for (int i = 0; i < 16; i++)
                        {
                            int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2); if (x >= w || y >= h) continue;
                            int ci = (int)((idx >> (2 * i)) & 3), t = o.I(x, y);
                            o.P[t] = (byte)col[ci, 0]; o.P[t + 1] = (byte)col[ci, 1]; o.P[t + 2] = (byte)col[ci, 2];
                            o.P[t + 3] = (cc == "DXT1" && !four && ci == 3) ? (byte)0 : alpha[i];
                        }
                    }
            }
            else if (BitConverter.ToInt32(d, 88) == 32)
            {
                for (int i = 0; i < w * h; i++) { o.P[i * 4] = d[p + i * 4 + 2]; o.P[i * 4 + 1] = d[p + i * 4 + 1]; o.P[i * 4 + 2] = d[p + i * 4]; o.P[i * 4 + 3] = d[p + i * 4 + 3]; }
            }
            else throw new Exception("unsupported DDS format in " + path);
            return o;
        }
        static void Rgb565(int c, int[,] col, int i)
        { col[i, 0] = ((c >> 11) & 31) * 255 / 31; col[i, 1] = ((c >> 5) & 63) * 255 / 63; col[i, 2] = (c & 31) * 255 / 31; }

        // the driver select scene keeps its pictures as PNG files, upside down with red and blue swapped
        public static Img AmPicture(byte[] am, int off, int w, int h)
        {
            if (off + 8 > am.Length || am[off] != 0x89 || am[off + 1] != (byte)'P' || am[off + 2] != (byte)'N' || am[off + 3] != (byte)'G')
                throw new Exception(string.Format("no picture at 0x{0:x} in the driver select scene", off));
            int end = IndexOf(am, Encoding.ASCII.GetBytes("IEND"), off) + 8; var blob = new byte[end - off];
            Buffer.BlockCopy(am, off, blob, 0, blob.Length);
            var im = Img.LoadBytes(blob);
            if (im.W != w || im.H != h) throw new Exception(string.Format("picture at 0x{0:x} is {1}x{2}, expected {3}x{4}", off, im.W, im.H, w, h));
            var o = new Img(w, h);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
                { int s = im.I(x, h - 1 - y), t = o.I(x, y); o.P[t] = im.P[s + 2]; o.P[t + 1] = im.P[s + 1]; o.P[t + 2] = im.P[s]; o.P[t + 3] = im.P[s + 3]; }
            return o;
        }
        static int IndexOf(byte[] d, byte[] pat, int from)
        {
            for (int i = from; i <= d.Length - pat.Length; i++) { int k = 0; while (k < pat.Length && d[i + k] == pat[k]) k++; if (k == pat.Length) return i; }
            throw new Exception("IEND not found");
        }
    }

    // ---------------------------------------------------------------- name plates from the disc's own letters
    class Plates
    {
        const double SLANT = 0.22; const int PAD = 12, H = 35;
        readonly Dictionary<string, Img> glyphs = new Dictionary<string, Img>();
        int GAP, WGAP, bigH, smallH, refBottom;
        static int R(double v) { return (int)Math.Round(v, MidpointRounding.ToEven); }
        static string Key(char c, bool big) { return c + (big ? "B" : "s"); }
        static bool Fill(Img a, int x, int y) { int i = a.I(x, y); return a.P[i + 3] > 200 && (a.P[i] + a.P[i + 1] + a.P[i + 2]) / 3.0 > 170; }

        public Plates(string textures, List<string[]> plates)
        {
            var gaps = new List<int>(); var wgaps = new List<int>();
            foreach (var pl in plates)
            {
                string f = Path.Combine(textures, "DriverSelectName" + pl[0] + ".png");
                if (!File.Exists(f)) continue;
                var a = Img.Load(f).Crop(0, 0, 512, H);
                var d = new Img(a.W + PAD * 2 + (int)(SLANT * H) + 2, H);
                for (int y = 0; y < H; y++) Buffer.BlockCopy(a.P, a.I(0, y), d.P, d.I(R(SLANT * y) + PAD, y), a.W * 4);
                var cores = Runs(d, (x, y) => Fill(d, x, y)); var all = Runs(d, (x, y) => d.P[d.I(x, y) + 3] > 20);
                string letters = pl[1].Replace(" ", "");
                if (cores.Count != letters.Length || all.Count == 0) { Log.W("plate " + pl[0] + ": " + cores.Count + " letters found, " + letters.Length + " expected - skipped"); continue; }
                var first = new HashSet<int>(); int k = 0; foreach (var wd in pl[1].Split(' ')) { first.Add(k); k += wd.Length; }
                int lo = all[0][0], hi = all[all.Count - 1][1];
                for (int i = 0; i < cores.Count; i++)
                {
                    int l = i == 0 ? lo : (cores[i - 1][1] + cores[i][0]) / 2, r = i == cores.Count - 1 ? hi : (cores[i][1] + cores[i + 1][0]) / 2;
                    string key = Key(letters[i], first.Contains(i));
                    if (!glyphs.ContainsKey(key)) glyphs[key] = d.Crop(l, 0, r, H);
                    if (i > 0) (first.Contains(i) ? wgaps : gaps).Add(cores[i][0] - cores[i - 1][1]);
                }
            }
            if (!glyphs.ContainsKey(Key('B', true)) || !glyphs.ContainsKey(Key('A', false))) throw new Exception("name plate letters not found");
            GAP = Median(gaps); WGAP = Median(wgaps);
            var b = InkRows(glyphs[Key('B', true)]); var s = InkRows(glyphs[Key('A', false)]);
            bigH = b[1] - b[0] + 1; smallH = s[1] - s[0] + 1; refBottom = b[1];
        }
        static int Median(List<int> v) { if (v.Count == 0) return 4; v.Sort(); int n = v.Count; return n % 2 == 1 ? v[n / 2] : (int)((v[n / 2 - 1] + v[n / 2]) / 2.0); }
        static List<int[]> Runs(Img d, Func<int, int, bool> on)      // column runs where any row is on; end exclusive
        {
            var cols = new bool[d.W]; for (int x = 0; x < d.W; x++) for (int y = 0; y < d.H; y++) if (on(x, y)) { cols[x] = true; break; }
            var o = new List<int[]>(); int i = 0;
            while (i < d.W) { if (cols[i]) { int s = i; while (i < d.W && cols[i]) i++; o.Add(new[] { s, i }); } else i++; }
            return o;
        }
        static int[] InkRows(Img a)
        {
            int t = -1, b = -1;
            for (int y = 0; y < a.H; y++) for (int x = 0; x < a.W; x++) if (a.P[a.I(x, y) + 3] > 20) { if (t < 0) t = y; b = y; break; }
            return new[] { t, b };
        }
        static Img Trim(Img a)
        {
            int x0 = -1, x1 = -1; for (int x = 0; x < a.W; x++) for (int y = 0; y < a.H; y++) if (a.P[a.I(x, y) + 3] > 0) { if (x0 < 0) x0 = x; x1 = x; break; }
            return x0 < 0 ? a : a.Crop(x0, 0, x1 + 1, a.H);
        }
        static Img Slanted(Img a)                                        // as seen on screen
        {
            int extra = R(SLANT * a.H) + 2; var o = new Img(a.W + extra, a.H);
            for (int y = 0; y < a.H; y++) Buffer.BlockCopy(a.P, a.I(0, y), o.P, o.I(extra - 1 - R(SLANT * y), y), a.W * 4);
            return o;
        }
        Img Scale(Img a, bool big)
        {
            var ir = InkRows(a); double f = big ? (double)bigH / smallH : (double)smallH / bigH;
            var sl = Trim(Slanted(a)); var im = sl.Crop(0, ir[0], sl.W, ir[1] + 1);
            int nh = R((ir[1] + 1 - ir[0]) * f), nw = Math.Max(1, R(im.W * f));
            im = im.Resize(nw, nh);
            var full = new Img(nw, H); full.Paste(im, 0, refBottom + 1 - nh);
            var us = new Img(nw + R(SLANT * H) + 2, H);
            for (int y = 0; y < H; y++) Buffer.BlockCopy(full.P, full.I(0, y), us.P, us.I(R(SLANT * y), y), nw * 4);
            return Trim(us);
        }
        Img Derive(char c, bool big)
        {
            Img g;
            if (glyphs.TryGetValue(Key(c, big), out g)) return g;
            if (glyphs.TryGetValue(Key(c, !big), out g)) return Scale(g, big);
            if (c == 'P') return big ? MakeP() : Scale(Derive('P', true), false);
            if (c == 'X') return MakeX(big);
            throw new Exception("no letter " + c + " for the name plates");
        }
        static List<int[]> RowRuns(bool[] row)
        {
            var o = new List<int[]>();
            for (int x = 0; x < row.Length; x++) { if (!row[x]) continue; if (o.Count > 0 && o[o.Count - 1][1] == x - 1) o[o.Count - 1][1] = x; else o.Add(new[] { x, x }); }
            return o;
        }
        Img MakeP()                         // P = this font's R (tall bowl, vertical leg) without its leg
        {
            var a = Derive('R', true).Clone(); var ir = InkRows(a); int t = ir[0], bt = ir[1];
            var fill = new bool[a.H][]; var dk = new List<int[]>(); var fl = new List<int[]>();
            for (int y = 0; y < a.H; y++)
            {
                fill[y] = new bool[a.W];
                for (int x = 0; x < a.W; x++)
                {
                    int i = a.I(x, y); fill[y][x] = Fill(a, x, y);
                    if (a.P[i + 3] > 200) (fill[y][x] ? fl : dk).Add(new int[] { a.P[i], a.P[i + 1], a.P[i + 2] });
                }
            }
            var oc = Med(dk); var fc = Med(fl);
            int y0 = -1; for (int y = t + (bt - t) / 2; y < bt; y++) if (RowRuns(fill[y]).Count >= 2) { y0 = y; break; }
            if (y0 < 0) return a;
            int keep = RowRuns(fill[Math.Min(y0 + 3, bt)])[0][1] + 2;
            int Rr = 0; for (int y = t; y < t + (bt - t) / 3; y++) { var r = RowRuns(fill[y]); if (r.Count > 0) Rr = Math.Max(Rr, r[r.Count - 1][1]); }
            for (int y = t + (bt - t) / 3; y < y0; y++)
            {
                var r = RowRuns(fill[y]);
                if (r.Count > 0 && r[r.Count - 1][1] < Rr)
                {
                    for (int x = r[r.Count - 1][1] + 1; x <= Rr && x < a.W; x++) Set(a, x, y, fc);
                    for (int k = 1; k <= 2; k++) if (Rr + k < a.W) Set(a, Rr + k, y, oc);
                }
            }
            for (int x = keep + 1; x < Math.Min(Rr + 3, a.W); x++) Set(a, x, y0, oc);
            for (int y = y0 + 1; y < a.H; y++) for (int x = keep + 1; x < a.W; x++) { int i = a.I(x, y); a.P[i] = a.P[i + 1] = a.P[i + 2] = a.P[i + 3] = 0; }
            return a;
        }
        static int[] Med(List<int[]> v)
        {
            if (v.Count == 0) return new[] { 60, 20, 20 };
            var o = new int[3]; for (int c = 0; c < 3; c++) { var s = v.Select(p => p[c]).OrderBy(z => z).ToList(); int n = s.Count; o[c] = n % 2 == 1 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2; }
            return o;
        }
        static void Set(Img a, int x, int y, int[] c) { int i = a.I(x, y); a.P[i] = (byte)c[0]; a.P[i + 1] = (byte)c[1]; a.P[i + 2] = (byte)c[2]; a.P[i + 3] = 255; }
        Img MakeX(bool big)                 // X = upper half of V + the mirrored V below
        {
            var a = Derive('V', big).Clone(); var ir = InkRows(a); int t = ir[0], b = ir[1], m = (t + b) / 2, sh = (H - 1 - b) - t;
            var src = a.Clone();
            for (int y = m; y < H; y++)
            {
                int fy = ((y + sh) % H + H) % H;           // roll of the flipped glyph
                int sy = H - 1 - fy;                       // flip
                Buffer.BlockCopy(src.P, src.I(0, sy), a.P, a.I(0, y), a.W * 4);
            }
            return a;
        }
        static Img Solid(Img a) { var o = a.Clone(); for (int i = 0; i < o.W * o.H; i++) if (o.P[i * 4 + 3] < 242) { o.P[i * 4] = o.P[i * 4 + 1] = o.P[i * 4 + 2] = o.P[i * 4 + 3] = 0; } return o; }

        // capital letter = large letter, lower case = small (the arcade's small caps); space = word gap
        public Img Compose(string name)
        {
            var cells = new List<Img>(); var gapsBefore = new List<int>(); int gapNext = 0;
            foreach (char ch in name)
            {
                if (ch == ' ') { gapNext = WGAP; continue; }
                bool big = char.IsUpper(ch);
                cells.Add(Solid(Derive(char.ToUpperInvariant(ch), big)));
                gapsBefore.Add(cells.Count > 1 ? (gapNext != 0 ? gapNext : GAP) : 0); gapNext = 0;
            }
            int width = cells.Sum(c => c.W) + gapsBefore.Sum() + 8;
            var line = new Img(width + 4, H); int xpos = 0, end = 0; int?[] prev = null;
            for (int ci = 0; ci < cells.Count; ci++)
            {
                var a = cells[ci]; int gp = gapsBefore[ci];
                var left = new int?[H]; var right = new int?[H];
                for (int y = 0; y < H; y++) for (int x = 0; x < a.W; x++) if (Fill(a, x, y)) { if (left[y] == null) left[y] = x; right[y] = x; }
                if (prev != null)
                {
                    int pmax = prev.Where(v => v != null).Max().Value, lmin = left.Where(v => v != null).Min().Value;
                    int xbox = pmax + 1 + gp - lmin;
                    if (gp == GAP)
                    {
                        var rows = new List<int>(); for (int y = 0; y < H; y++) if (prev[y] != null && left[y] != null) rows.Add(prev[y].Value + 1 + gp - left[y].Value);
                        xpos = Math.Max(rows.Count > 0 ? rows.Max() : xbox, xbox - 4);
                    }
                    else xpos = xbox;
                }
                for (int y = 0; y < H; y++)
                    for (int x = 0; x < a.W; x++)
                    {
                        int dx = x + xpos; if (dx < 0 || dx >= line.W) continue;
                        int si = a.I(x, y), di = line.I(dx, y); double mm = a.P[si + 3] / 255.0;
                        for (int c = 0; c < 4; c++) line.P[di + c] = (byte)(a.P[si + c] * mm + line.P[di + c] * (1 - mm));
                        line.P[di + 3] = Math.Max(line.P[di + 3], a.P[si + 3]);
                    }
                var nr = new int?[H]; for (int y = 0; y < H; y++) nr[y] = right[y] == null ? (int?)null : right[y] + xpos;
                if (prev == null) prev = nr;
                else for (int y = 0; y < H; y++) prev[y] = prev[y] == null ? nr[y] : (nr[y] == null ? prev[y] : Math.Max(prev[y].Value, nr[y].Value));
                end = xpos + a.W;
            }
            line = line.Crop(0, 0, Math.Min(line.W, end + 4), H);
            // slant back, then one continuous soft shadow under the whole name
            var sl = new Img(line.W + PAD * 2 + (int)(SLANT * H) + 2, H);
            for (int y = 0; y < H; y++) Buffer.BlockCopy(line.P, line.I(0, y), sl.P, sl.I(PAD + R(SLANT * (H - 1)) - R(SLANT * y), y), line.W * 4);
            int c0 = -1, c1 = -1; for (int x = 0; x < sl.W; x++) for (int y = 0; y < H; y++) if (sl.P[sl.I(x, y) + 3] > 0) { if (c0 < 0) c0 = x; c1 = x; break; }
            sl = sl.Crop(c0, 0, c1 + 1, H);
            var blur = Mask.Gauss(sl.Alpha(), sl.W, sl.H, 2.0);
            var body = sl.Clone();
            for (int i = 0; i < sl.W * sl.H; i++)
            {
                double sa = Math.Min(1.0, blur[i] / 255.0 * 1.6), ba = body.P[i * 4 + 3] / 255.0, al = ba + sa * (1 - ba);
                double[] sc = { 16, 0, 0 };
                for (int c = 0; c < 3; c++)
                {
                    double v = body.P[i * 4 + c] * ba + sc[c] * sa * (1 - ba);
                    sl.P[i * 4 + c] = (byte)Math.Min(255, al > 0 ? v / Math.Max(al, 1e-6) : 0);
                }
                sl.P[i * 4 + 3] = (byte)Math.Min(255, al * 255);
            }
            var plate = new Img(512, 64); int w = Math.Min(sl.W, 498);
            plate.Paste(sl.Crop(sl.W - w, 0, sl.W, H), 498 - w, 0);
            for (int r = 35; r < 52; r++)                 // reflection: the name mirrored, every 2nd row, fading
            {
                int t = 34 - (r - 35) * 2; double k = 0.55 - (r - 35) * 0.025;
                for (int x = 0; x < 512; x++)
                {
                    int s = plate.I(x, t), d = plate.I(x, r);
                    plate.P[d] = plate.P[s]; plate.P[d + 1] = plate.P[s + 1]; plate.P[d + 2] = plate.P[s + 2]; plate.P[d + 3] = (byte)(plate.P[s + 3] * k);
                }
            }
            return plate;
        }
    }

    // ---------------------------------------------------------------- one extra driver
    class Driver
    {
        public string Num, Make, Donor, ColourSlot, Name, Info, TrFace; public int Cell, Tol, Sample, WithLine; public int[] Fill, Line;
        public string FileName { get { return Num + " " + Name.TrimEnd('.') + ".png"; } }
    }
    class Donor { public string Slot, Num; public int FaceOff, NumOff; }

    static class Log
    {
        public static string File_;
        public static void W(string s)
        {
            Console.WriteLine(s);
            if (File_ != null) try { File.AppendAllText(File_, DateTime.Now.ToString("HH:mm:ss") + " " + s + "\r\n"); } catch { }
        }
    }

    static class Program
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern bool WritePrivateProfileString(string sec, string key, string val, string file);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern int GetPrivateProfileString(string sec, string key, string def, StringBuilder buf, int size, string file);

        static string root, shell, tex, game, veh, trTex;
        static List<Driver> drivers = new List<Driver>(); static Dictionary<string, Donor> donors = new Dictionary<string, Donor>(); static List<string[]> plates = new List<string[]>();
        static Plates plateMaker; static byte[] amCache;

        static int Main(string[] args)
        {
            if (args.Length < 2 || (args[0] != "install" && args[0] != "sheets")) { Console.WriteLine("usage: GvrDriverArt.exe install|sheets <install folder> [team racing textures folder]"); return 2; }
            root = Path.GetFullPath(args[1]); shell = Path.Combine(root, "Shell"); tex = Path.Combine(shell, "Textures"); game = Path.Combine(root, "Game");
            veh = Path.Combine(game, "GameData", "Vehicles", "2005 NEXTEL Cup");
            // the Team Racing textures folder given at install time is remembered for the launcher's runs
            string cfg = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "GvrDriverArt.cfg");
            if (args.Length > 2) { trTex = args[2].Length > 0 && Directory.Exists(args[2]) ? args[2] : null; try { File.WriteAllText(cfg, trTex ?? ""); } catch { } }
            else if (File.Exists(cfg)) { var t = File.ReadAllText(cfg).Trim(); trTex = t.Length > 0 && Directory.Exists(t) ? t : null; }
            if (Directory.Exists(Path.Combine(root, "LOG"))) Log.File_ = Path.Combine(root, "LOG", "gvrdriverart.log");
            try
            {
                ReadData(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "extra-drivers.txt"));
                foreach (var dir in new[] { "Faces", "Names", "Numbers" }) Directory.CreateDirectory(Path.Combine(tex, dir));
                WriteReadme();
                int failed = args[0] == "install" ? Install() : Sheets(false);
                Log.W(args[0] + ": " + (drivers.Count - failed) + " of " + drivers.Count + " drivers ready");
                return failed == 0 ? 0 : 1;
            }
            catch (Exception e) { Log.W("error: " + e.Message); return 2; }
        }

        static void ReadData(string path)
        {
            foreach (var raw in File.ReadAllLines(path))
            {
                var l = raw.Trim(); if (l.Length == 0 || l.StartsWith("#")) continue; var f = l.Split('|');
                if (f[0] == "plate") plates.Add(new[] { f[1], f[2] });
                else if (f[0] == "donor") donors[f[1]] = new Donor { Slot = f[1], Num = f[2], FaceOff = Convert.ToInt32(f[3], 16), NumOff = Convert.ToInt32(f[4], 16) };
                else if (f[0] == "driver")
                    drivers.Add(new Driver
                    {
                        Num = f[1], Make = f[2], Donor = f[3], Cell = int.Parse(f[4]), ColourSlot = f[5], Name = f[6], Info = f[7], TrFace = f[8],
                        Fill = f[9].Split(',').Select(int.Parse).ToArray(), Line = f[10].Split(',').Select(int.Parse).ToArray(),
                        Tol = int.Parse(f[11]), Sample = int.Parse(f[12]), WithLine = int.Parse(f[13])
                    });
            }
            if (drivers.Count == 0) throw new Exception("no drivers in " + path);
        }

        static byte[] Am() { return amCache ?? (amCache = File.ReadAllBytes(Path.Combine(shell, "NASCAR_Selection.am"))); }
        static Plates PlateMaker() { return plateMaker ?? (plateMaker = new Plates(tex, plates)); }
        static string Paint(Driver d) { return Path.Combine(veh, d.Make, d.Num, d.Num + "-01", d.Num + "-01.dds"); }

        static int Install()
        {
            int failed = 0;
            // leaderboard badges (race HUD plates) and menu car pictures: not player-editable
            Dictionary<string, Img> hud = null;
            try { hud = Disc.HudCarPlates(Path.Combine(game, "GameData", "HUD", "Hud.mas")); } catch (Exception e) { Log.W("leaderboard badges: " + e.Message); }
            if (hud != null)
                foreach (var dn in donors.Values)
                {
                    try
                    {
                        var s = new Img(256, 32); s.Paste(hud[dn.Num], 0, 0);
                        foreach (var d in drivers.Where(x => x.Donor == dn.Slot)) if (hud.ContainsKey(d.Num)) s.Paste(hud[d.Num], 64 * d.Cell, 0);
                        s.SavePng(Path.Combine(tex, "GvrHud_" + dn.Num + ".png"));
                    }
                    catch (Exception e) { Log.W("badges for " + dn.Slot + ": " + e.Message); }
                }
            foreach (var d in drivers)
            {
                string skin = Path.Combine(tex, "CarSkin" + d.Num + ".jpg");
                if (File.Exists(skin)) continue;                                  // the disc's own picture wins
                try { var p = Disc.Dds(Paint(d)); if (p.W != 512) p = p.Resize(512, 512); p.SaveJpeg(skin, 92); }
                catch (Exception e) { Log.W("car picture #" + d.Num + ": " + e.Message); failed++; }
            }
            failed += Sheets(true);
            // the driver list the menus read (one value per driver)
            string reg = Path.Combine(root, "nascar_registry.ini"), sec = "GlobalVR\\NASCAR"; int n = 0;
            foreach (var d in drivers)
            {
                if (!File.Exists(Path.Combine(tex, "Names", d.FileName))) continue;
                n++;
                string e = string.Join(";", new[] { d.Num, d.Donor, d.Make, "Names\\" + d.FileName, d.ColourSlot, d.Info, d.Cell.ToString(), donors[d.Donor].Num });
                WritePrivateProfileString(sec, "ExtraDriver" + n, "\"" + e + "\"", reg);
            }
            for (int k = n + 1; k <= 60; k++)
            {
                var sb = new StringBuilder(16); GetPrivateProfileString(sec, "ExtraDriver" + k, "", sb, 16, reg);
                if (sb.Length == 0) break; WritePrivateProfileString(sec, "ExtraDriver" + k, null, reg);
            }
            Log.W(n + " drivers written to nascar_registry.ini");
            return failed;
        }

        // face / number sheets from Faces + Numbers; deleted files there and in Names are recreated
        static int Sheets(bool force)
        {
            int failed = 0;
            foreach (var d in drivers)
            {
                try { if (!File.Exists(Path.Combine(tex, "Faces", d.FileName))) FaceTemplate(d).SavePng(Path.Combine(tex, "Faces", d.FileName)); }
                catch (Exception e) { Log.W("face #" + d.Num + ": " + e.Message); }
                try
                {
                    string nf = Path.Combine(tex, "Names", d.FileName);
                    string disc = Path.Combine(tex, "DriverSelectName" + d.Name.Replace(" ", "").Replace(".", "") + ".png");
                    if (!File.Exists(nf)) { if (File.Exists(disc)) File.Copy(disc, nf); else PlateMaker().Compose(d.Name).SavePng(nf); }   // the disc's own plate wins
                }
                catch (Exception e) { Log.W("name plate #" + d.Num + ": " + e.Message); failed++; }
                try { if (!File.Exists(Path.Combine(tex, "Numbers", d.FileName))) Number(d).SavePng(Path.Combine(tex, "Numbers", d.FileName)); }
                catch (Exception e) { Log.W("number #" + d.Num + ": " + e.Message); failed++; }
            }
            string state = Path.Combine(tex, "GvrDriverArt.state"), sig = Signature();
            bool have = donors.Values.All(dn => File.Exists(Path.Combine(tex, "GvrFace_" + dn.Slot + ".png")) && File.Exists(Path.Combine(tex, "GvrNum_" + dn.Slot + ".png")));
            if (!force && have && File.Exists(state) && File.ReadAllText(state) == sig) { Log.W("faces and numbers unchanged"); return failed; }
            foreach (var dn in donors.Values)
            {
                try
                {
                    var fs = new Img(1024, 256); fs.Paste(Disc.AmPicture(Am(), dn.FaceOff, 256, 256), 0, 0);
                    var ns = new Img(512, 64); ns.Paste(Disc.AmPicture(Am(), dn.NumOff, 128, 64), 0, 0);
                    foreach (var d in drivers.Where(x => x.Donor == dn.Slot))
                    {
                        var face = Img.Load(Path.Combine(tex, "Faces", d.FileName)); if (face.W != 256 || face.H != 256) face = face.Resize(256, 256);
                        if (trTex != null && d.TrFace.Length > 0 && File.Exists(Path.Combine(trTex, d.TrFace)) && SamePixels(face, FaceTemplate(d)))
                            face = Img.Load(Path.Combine(trTex, d.TrFace)).Resize(256, 256);      // Team Racing on this PC, while the face is the placeholder
                        fs.Paste(face, 256 * d.Cell, 0);
                        var num = Img.Load(Path.Combine(tex, "Numbers", d.FileName)); if (num.W != 128 || num.H != 64) num = num.Resize(128, 64);
                        ns.Paste(num, 128 * d.Cell, 0);
                    }
                    fs.SavePng(Path.Combine(tex, "GvrFace_" + dn.Slot + ".png")); ns.SavePng(Path.Combine(tex, "GvrNum_" + dn.Slot + ".png"));
                }
                catch (Exception e) { Log.W("sheets for " + dn.Slot + ": " + e.Message); failed++; }
            }
            File.WriteAllText(state, sig);
            Log.W("face and number sheets rebuilt");
            return failed;
        }
        static string Signature()
        {
            var sb = new StringBuilder("v1|" + (trTex ?? "") + "|");
            foreach (var dir in new[] { "Faces", "Numbers" })
                foreach (var f in Directory.GetFiles(Path.Combine(tex, dir), "*.png").OrderBy(z => z, StringComparer.OrdinalIgnoreCase))
                { var i = new FileInfo(f); sb.Append(dir + "/" + i.Name + ":" + i.Length + ":" + i.LastWriteTimeUtc.Ticks + "|"); }
            return sb.ToString();
        }
        static bool SamePixels(Img a, Img b) { if (a.W != b.W || a.H != b.H) return false; for (int i = 0; i < a.P.Length; i++) if (a.P[i] != b.P[i]) return false; return true; }

        static Img FaceTemplate(Driver d)
        {
            var im = new Img(256, 256);
            using (var b = im.ToBitmap())
            {
                using (var g = Graphics.FromImage(b))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias; g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                    using (var path = Rounded(new Rectangle(40, 30, 176, 220), 14))
                    {
                        using (var br = new SolidBrush(Color.FromArgb(60, 255, 255, 255))) g.FillPath(br, path);
                        using (var pen = new Pen(Color.FromArgb(200, 70, 70, 70), 4)) g.DrawPath(pen, path);
                    }
                    var parts = d.Name.Split(' '); var lines = parts.Length > 1 ? new[] { parts[0], string.Join(" ", parts.Skip(1)) } : new[] { d.Name };
                    using (var fb = new Font("Arial", 26, FontStyle.Bold, GraphicsUnit.Pixel))
                    using (var fn = new Font("Arial Black", 44, FontStyle.Regular, GraphicsUnit.Pixel))
                    using (var br = new SolidBrush(Color.FromArgb(255, 60, 60, 60)))
                    {
                        var sf = new StringFormat { Alignment = StringAlignment.Center }; float y = 70;
                        foreach (var ln in lines) { g.DrawString(ln, fb, br, new RectangleF(0, y, 256, 34), sf); y += 32; }
                        g.DrawString("#" + d.Num, fn, br, new RectangleF(0, y + 14, 256, 60), sf);
                    }
                }
                return Img.FromBitmap(b);
            }
        }
        static GraphicsPath Rounded(Rectangle r, int rad)
        {
            var p = new GraphicsPath(); int d = rad * 2;
            p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc(r.X, r.Bottom - d, d, d, 90, 90); p.CloseFigure(); return p;
        }

        // car number in the car's own livery font: the digit shape from the roof of its paint, redrawn clean
        static Img Number(Driver d)
        {
            Img cell = null;
            try { cell = LiveryNumber(d); } catch (Exception e) { Log.W("number #" + d.Num + " from the paint: " + e.Message); }
            return cell ?? DrawnNumber(d);
        }
        static Img LiveryNumber(Driver d)
        {
            var p = Disc.Dds(Paint(d)); if (p.W != 512 || p.H != 512) p = p.Resize(512, 512);
            var c = p.Crop(200, 125, 350, 242); int w = c.W, h = c.H, n = w * h;
            Func<int, int[], double> dist = (i, col) => Math.Sqrt(Sq(c.P[i * 4] - col[0]) + Sq(c.P[i * 4 + 1] - col[1]) + Sq(c.P[i * 4 + 2] - col[2]));
            var fill = (int[])d.Fill.Clone();
            if (d.Sample == 1)
            {
                var near = Enumerable.Range(0, n).Where(i => dist(i, d.Fill) < 110).ToList();
                if (near.Count > 200) for (int k = 0; k < 3; k++) { var v = near.Select(i => (int)c.P[i * 4 + k]).OrderBy(z => z).ToList(); fill[k] = v.Count % 2 == 1 ? v[v.Count / 2] : (v[v.Count / 2 - 1] + v[v.Count / 2]) / 2; }
            }
            var m = new byte[n]; for (int i = 0; i < n; i++) m[i] = dist(i, fill) < d.Tol ? (byte)255 : (byte)0;
            if (d.WithLine == 1)
            {
                var nearM = Mask.MaxF(m, w, h, 9);
                for (int i = 0; i < n; i++) if (dist(i, d.Line) < d.Tol && nearM[i] > 128) m[i] = 255;
            }
            m = Mask.MinF(Mask.MaxF(m, w, h, 3), w, h, 3);                          // close thin lines through the digits
            var mb = m.Select(v => v > 128).ToArray(); var info = new List<int[]>(); var lab = Mask.Components(mb, w, h, info);
            var keepIds = new HashSet<int>(info.Where(q => (q[3] - q[2]) > h * 0.35 && q[1] > 250).Select(q => q[0]));
            if (keepIds.Count == 0) return null;
            var keep = new bool[n]; for (int i = 0; i < n; i++) keep[i] = keepIds.Contains(lab[i]);
            var inv = keep.Select(v => !v).ToArray(); var info2 = new List<int[]>(); var lab2 = Mask.Components(inv, w, h, info2);
            var holes = new HashSet<int>(info2.Where(q => q[1] < 60 && q[2] > 0 && q[4] > 0 && q[3] < h - 1 && q[5] < w - 1).Select(q => q[0]));
            for (int i = 0; i < n; i++) if (holes.Contains(lab2[i])) keep[i] = true;
            var mk = keep.Select(v => v ? (byte)255 : (byte)0).ToArray();
            var bb = Mask.BBox(mk, w, h, 0); mk = Mask.Crop(mk, w, bb); int mw = bb[2] - bb[0], mh = bb[3] - bb[1];
            mk = Mask.Median(mk, mw, mh, 3);
            mk = Mask.Resize(mk, mw, mh, mw * 4, mh * 4); mw *= 4; mh *= 4;
            mk = Mask.Median(mk, mw, mh, 9); mk = Mask.Gauss(mk, mw, mh, 3.5);
            for (int i = 0; i < mk.Length; i++) mk[i] = mk[i] > 128 ? (byte)255 : (byte)0;
            mk = Mask.Gauss(mk, mw, mh, 1.0);
            // redraw: fill, coloured inner outline, black outer outline, soft drop shadow
            int pad = 40, pw = mw + 2 * pad, ph = mh + 2 * pad; var bas = Mask.Pad(mk, mw, mh, pad); byte[] inner, outer;
            if (d.WithLine == 1) { inner = bas; outer = Mask.MaxF(bas, pw, ph, 15); bas = Mask.MinF(bas, pw, ph, 11); }
            else { inner = Mask.MaxF(bas, pw, ph, 11); outer = Mask.MaxF(inner, pw, ph, 13); }
            var outImg = new Img(pw, ph);
            var sh = Mask.Gauss(outer, pw, ph, 8).Select(v => (byte)(v * 0.6)).ToArray();
            outImg.Over(Img.Solid(pw, ph, 0, 0, 0, sh), 12, 12);
            outImg.Over(Img.Solid(pw, ph, 0, 0, 0, outer), 0, 0);
            outImg.Over(Img.Solid(pw, ph, d.Line[0], d.Line[1], d.Line[2], inner), 0, 0);
            outImg.Over(Img.Solid(pw, ph, d.Fill[0], d.Fill[1], d.Fill[2], bas), 0, 0);
            return FitCell(outImg.CropBBox(0));
        }
        static double Sq(int v) { return (double)v * v; }
        static Img FitCell(Img o)
        {
            double k = Math.Min(124.0 / o.W, 60.0 / o.H); int nw = Math.Max(1, (int)Math.Round(o.W * k)), nh = Math.Max(1, (int)Math.Round(o.H * k));
            o = o.Resize(nw, nh); var cell = new Img(128, 64); cell.PasteMask(o, 2, (64 - nh) / 2); return cell;
        }
        // fallback when the paint's number cannot be read: the number drawn in the car's colours
        static Img DrawnNumber(Driver d)
        {
            var im = new Img(420, 190);
            using (var b = im.ToBitmap())
            {
                using (var g = Graphics.FromImage(b))
                using (var path = new GraphicsPath())
                using (var ff = new FontFamily("Arial Black"))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    path.AddString(d.Num, ff, (int)FontStyle.Regular, 120, new Point(40, 10), StringFormat.GenericDefault);
                    using (var m = new Matrix(1, 0, -0.24f, 1, 30, 0)) path.Transform(m);
                    using (var pb = new Pen(Color.Black, 26) { LineJoin = LineJoin.Round }) g.DrawPath(pb, path);
                    using (var pl = new Pen(Color.FromArgb(d.Line[0], d.Line[1], d.Line[2]), 14) { LineJoin = LineJoin.Round }) g.DrawPath(pl, path);
                    using (var bf = new SolidBrush(Color.FromArgb(d.Fill[0], d.Fill[1], d.Fill[2]))) g.FillPath(bf, path);
                }
                return FitCell(Img.FromBitmap(b).CropBBox(0));
            }
        }

        static void WriteReadme()
        {
            File.WriteAllText(Path.Combine(tex, "Faces", "README.txt"), string.Join("\r\n", new[] {
                "Pictures of the extra drivers on the NASCAR Racing driver select screen.",
                "",
                "Shell\\Textures\\Faces     portraits, 256 x 256 pixels",
                "Shell\\Textures\\Names     name plates, 512 x 64 pixels",
                "Shell\\Textures\\Numbers   card numbers, 128 x 64 pixels",
                "",
                "One file per driver, named <car number> <driver name>.png, e.g. \"18 J.J. Yeley.png\".",
                "To change a picture: replace the file (same name, same size, PNG with a transparent",
                "background) and start the game - the launcher picks up the change.",
                "Delete a file to get the original back on the next start.",
                "",
                "Portraits: head and shoulders, face centred, shoulders cut off by the bottom edge,",
                "like the 12 portraits the game already has. Until a portrait is added, the card",
                "shows a frame with the driver's name and number.",
                "",
                "Sharing: the face frames are free to share, and so is anything you draw yourself.",
                "The name plates and numbers made by the installer are built from the game disc's own",
                "artwork - keep those on your PC.",
                "" }));
        }
    }
}
