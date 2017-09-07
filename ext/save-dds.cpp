#include "save-dds.h"
#include <dxgi1_3.h> //for DXGI_FORMAT
#include <stdio.h>

struct DDS_PIXELFORMAT {
	DWORD dwSize;
	DWORD dwFlags;
	DWORD dwFourCC;
	DWORD dwRGBBitCount;
	DWORD dwRBitMask;
	DWORD dwGBitMask;
	DWORD dwBBitMask;
	DWORD dwABitMask;
};

struct DDSHeader {
	DWORD           dwSize;
	DWORD           dwFlags;
	DWORD           dwHeight;
	DWORD           dwWidth;
	DWORD           dwPitchOrLinearSize;
	DWORD           dwDepth;
	DWORD           dwMipMapCount;
	DWORD           dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	DWORD           dwCaps;
	DWORD           dwCaps2;
	DWORD           dwCaps3;
	DWORD           dwCaps4;
	DWORD           dwReserved2;
};

enum DDS_FLAGS {
	DDSD_CAPS = 0x1,
	DDSD_HEIGHT = 0x2,
	DDSD_WIDTH = 0x4,
	DDSD_PITCH = 0x8,
	DDSD_PIXELFORMAT = 0x1000,
	DDSD_MIPMAPCOUNT = 0x20000,
	DDSD_LINEARSIZE = 0x80000,
	DDSD_DEPTH = 0x800000,
	DDSCAPS_TEXTURE = 0x1000
};
namespace msdfgen {
	bool saveDDS(const Bitmap<FloatRGB> &bitmap, const char *filename) {
		DDSHeader header;
		memset(&header, 0x0, sizeof(DDSHeader));
		header.dwSize = sizeof(DDSHeader);
		header.dwWidth = bitmap.width();
		header.dwHeight = bitmap.height();
		header.dwMipMapCount = 1;
		header.dwDepth = 0;
		header.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		header.dwPitchOrLinearSize = (bitmap.width() * 32 + 7) / 8;
		header.dwCaps = DDSCAPS_TEXTURE;
		header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
		//header.ddspf.dwFourCC = 0x44583130; //"DX10"
		header.ddspf.dwFlags = 0x40 | 0x1;
		header.ddspf.dwRGBBitCount = 32;
		header.ddspf.dwABitMask = 0xff000000;
		header.ddspf.dwRBitMask = 0x00ff0000;
		header.ddspf.dwGBitMask = 0x0000ff00;
		header.ddspf.dwBBitMask = 0x000000ff;

		unsigned char* pixelBuffer = (unsigned char*)malloc(4 * header.dwWidth * header.dwHeight);
		for (int y = header.dwHeight - 1; y >= 0; --y) {
			for (int x = 0; x < header.dwWidth; x++) {
				pixelBuffer[(y * header.dwWidth + x) * 4] = clamp(int(bitmap(x, y).r * 0x100), 0xff);
				pixelBuffer[(y * header.dwWidth + x) * 4 + 1] = clamp(int(bitmap(x, y).g * 0x100), 0xff);
				pixelBuffer[(y * header.dwWidth + x) * 4 + 2] = clamp(int(bitmap(x, y).b * 0x100), 0xff);
				pixelBuffer[(y * header.dwWidth + x) * 4 + 3] = 0xff;
			}
		}
		FILE* f = fopen(filename, "wb");
		if (!f)
			return false;
		int magic = 0x20534444; //"DDS "
		fwrite(&magic, sizeof(int), 1, f);
		fwrite(&header, sizeof(DDSHeader), 1, f);
		fwrite(pixelBuffer, header.dwPitchOrLinearSize, header.dwHeight, f);
		fclose(f);
		free(pixelBuffer);
		return true;
	}
}
