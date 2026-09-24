#include "TopScreenBoxBmp.h"

#include <cstdio>
#include <nds.h>

bool loadTopScreenBoxBmp(const std::string &path, u16 *pix, int &outW, int &outH,
                         int &bx, int &by, int &bw, int &bh, int maxW, int maxH) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	u8 hdr[54];
	if (fread(hdr, 1, 54, f) != 54) { fclose(f); return false; }
	u32 dataOff = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
	int w = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
	int h = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
	int bpp = hdr[28] | (hdr[29] << 8);
	if ((bpp != 4 && bpp != 8) || w <= 0 || w > maxW || h <= 0 || h > maxH) { fclose(f); return false; }

	u16 pal[256];
	bool trans[256] = {false};
	int ncol = (int)(dataOff - 54) / 4;
	fseek(f, 54, SEEK_SET);
	for (int i = 0; i < ncol && i < 256; i++) {
		u8 pe[4];
		if (fread(pe, 1, 4, f) != 4) break;
		u8 b = pe[0], g = pe[1], r = pe[2];
		trans[i] = (r >= 248 && g <= 8 && b >= 248); // magenta #FF00FF => transparent
		pal[i] = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | BIT(15);
	}

	// BMP rows padded to 4 bytes (4bpp = 2 pixels/byte, high nibble first).
	int rowsz = (bpp == 4) ? ((((w + 1) / 2) + 3) & ~3) : ((w + 3) & ~3);
	u8 rowbuf[256 + 4]; // 256 = SCREEN_WIDTH, the largest maxW any caller passes
	fseek(f, dataOff, SEEK_SET);
	for (int yy = 0; yy < h; yy++) {
		if (fread(rowbuf, 1, rowsz, f) != (size_t)rowsz) break;
		int y = h - 1 - yy; // BMP is bottom-up
		for (int x = 0; x < w; x++) {
			u8 idx = (bpp == 4) ? ((x & 1) ? (rowbuf[x / 2] & 0xF) : (rowbuf[x / 2] >> 4)) : rowbuf[x];
			pix[y * w + x] = trans[idx] ? 0 : pal[idx];
		}
	}
	fclose(f);
	outW = w;
	outH = h;

	// Locate the opaque box within the canvas. If nothing opaque, fall back to the whole asset.
	int minX = w, minY = h, maxX = -1, maxY = -1;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (pix[y * w + x]) {
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
	if (maxX < 0) { minX = minY = 0; maxX = w - 1; maxY = h - 1; }
	bx = minX; by = minY; bw = maxX - minX + 1; bh = maxY - minY + 1;
	return true;
}
