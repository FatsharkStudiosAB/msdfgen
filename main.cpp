
/*
 * MULTI-CHANNEL SIGNED DISTANCE FIELD GENERATOR v1.5 (2017-07-23) - standalone console program
 * --------------------------------------------------------------------------------------------
 * A utility by Viktor Chlumsky, (c) 2014 - 2017
 *
 */

#ifdef MSDFGEN_STANDALONE

#define _USE_MATH_DEFINES
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#include "msdfgen.h"
#include "msdfgen-ext.h"

#ifdef _WIN32
    #pragma warning(disable:4996)
#endif

#define LARGE_VALUE 1e240

using namespace msdfgen;

enum Format {
    AUTO,
    PNG,
    BMP,
    TEXT,
    TEXT_FLOAT,
    BINARY,
    BINARY_FLOAT,
    BINART_FLOAT_BE,
	DDS
};

struct Glyph {
	int code; //unicode code
	Shape shape; //vector shape of the glyph
	float advance; //how much should the cursor advance after writing
	int x; //x position in font atlas
	int y; //y position in font atlas
	float xoffset;
	float yoffset;
	int width;
	int height;
	Bitmap<FloatRGB> bitmap; //generated bitmap msdf

	
};

static char toupper(char c) {
    return c >= 'a' && c <= 'z' ? c-'a'+'A' : c;
}

static bool parseUnsigned(unsigned &value, const char *arg) {
    static char c;
    return sscanf(arg, "%u%c", &value, &c) == 1;
}

static bool parseUnsignedLL(unsigned long long &value, const char *arg) {
    static char c;
    return sscanf(arg, "%llu%c", &value, &c) == 1;
}

static bool parseUnsignedHex(unsigned &value, const char *arg) {
    static char c;
    return sscanf(arg, "%x%c", &value, &c) == 1;
}

static bool parseDouble(double &value, const char *arg) {
    static char c;
    return sscanf(arg, "%lf%c", &value, &c) == 1;
}

static bool parseUnicode(int &unicode, const char *arg) {
    unsigned uuc;
    if (parseUnsigned(uuc, arg)) {
        unicode = uuc;
        return true;
    }
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X') && parseUnsignedHex(uuc, arg+2)) {
        unicode = uuc;
        return true;
    }
    if (arg[0] == '\'' && arg[1] && arg[2] == '\'' && !arg[3]) {
        unicode = arg[1];
        return true;
    }
    return false;
}

static bool parseTextfile(const char* filename, std::vector<int>& charCodesOut) {
	FILE* fin = fopen(filename, "r, ccs=UTF-8");
	if (!fin) {
		return false;
	}
	wint_t code;
	while ((code = fgetwc(fin)) != WEOF) {
		if(code != L'\n')
			charCodesOut.push_back(code);
	}
	if (feof(fin)) {
		fclose(fin);
		return true;
	}
	printf("Error reading textfile input\n");
	return false;
}

static bool parseAngle(double &value, const char *arg) {
    char c1, c2;
    int result = sscanf(arg, "%lf%c%c", &value, &c1, &c2);
    if (result == 1)
        return true;
    if (result == 2 && (c1 == 'd' || c1 == 'D')) {
        value = M_PI*value/180;
        return true;
    }
    return false;
}

static void parseColoring(Shape &shape, const char *edgeAssignment) {
    unsigned c = 0, e = 0;
    if (shape.contours.size() < c) return;
    Contour *contour = &shape.contours[c];
    bool change = false;
    bool clear = true;
    for (const char *in = edgeAssignment; *in; ++in) {
        switch (*in) {
            case ',':
                if (change)
                    ++e;
                if (clear)
                    while (e < contour->edges.size()) {
                        contour->edges[e]->color = WHITE;
                        ++e;
                    }
                ++c, e = 0;
                if (shape.contours.size() <= c) return;
                contour = &shape.contours[c];
                change = false;
                clear = true;
                break;
            case '?':
                clear = false;
                break;
            case 'C': case 'M': case 'W': case 'Y': case 'c': case 'm': case 'w': case 'y':
                if (change) {
                    ++e;
                    change = false;
                }
                if (e < contour->edges.size()) {
                    contour->edges[e]->color = EdgeColor(
                        (*in == 'C' || *in == 'c')*CYAN|
                        (*in == 'M' || *in == 'm')*MAGENTA|
                        (*in == 'Y' || *in == 'y')*YELLOW|
                        (*in == 'W' || *in == 'w')*WHITE);
                    change = true;
                }
                break;
        }
    }
}

static void invertColor(Bitmap<FloatRGB> &bitmap) {
    for (int y = 0; y < bitmap.height(); ++y)
        for (int x = 0; x < bitmap.width(); ++x) {
            bitmap(x, y).r = 1.f-bitmap(x, y).r;
            bitmap(x, y).g = 1.f-bitmap(x, y).g;
            bitmap(x, y).b = 1.f-bitmap(x, y).b;
        }
}

static void invertColor(Bitmap<float> &bitmap) {
    for (int y = 0; y < bitmap.height(); ++y)
        for (int x = 0; x < bitmap.width(); ++x)
            bitmap(x, y) = 1.f-bitmap(x, y);
}

static bool writeTextBitmap(FILE *file, const float *values, int cols, int rows) {
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int v = clamp(int((*values++)*0x100), 0xff);
            fprintf(file, col ? " %02X" : "%02X", v);
        }
        fprintf(file, "\n");
    }
    return true;
}

static bool writeTextBitmapFloat(FILE *file, const float *values, int cols, int rows) {
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            fprintf(file, col ? " %g" : "%g", *values++);
        }
        fprintf(file, "\n");
    }
    return true;
}

static bool writeBinBitmap(FILE *file, const float *values, int count) {
    for (int pos = 0; pos < count; ++pos) {
        unsigned char v = clamp(int((*values++)*0x100), 0xff);
        fwrite(&v, 1, 1, file);
    }
    return true;
}

#ifdef __BIG_ENDIAN__
static bool writeBinBitmapFloatBE(FILE *file, const float *values, int count)
#else
static bool writeBinBitmapFloat(FILE *file, const float *values, int count)
#endif
{
    return fwrite(values, sizeof(float), count, file) == count;
}

#ifdef __BIG_ENDIAN__
static bool writeBinBitmapFloat(FILE *file, const float *values, int count)
#else
static bool writeBinBitmapFloatBE(FILE *file, const float *values, int count)
#endif
{
    for (int pos = 0; pos < count; ++pos) {
        const unsigned char *b = reinterpret_cast<const unsigned char *>(values++);
        for (int i = sizeof(float)-1; i >= 0; --i)
            fwrite(b+i, 1, 1, file);
    }
    return true;
}

static bool cmpExtension(const char *path, const char *ext) {
    for (const char *a = path+strlen(path)-1, *b = ext+strlen(ext)-1; b >= ext; --a, --b)
        if (a < path || toupper(*a) != toupper(*b))
            return false;
    return true;
}

template <typename T>
static const char * writeOutput(const Bitmap<T> &bitmap, const char *filename, Format format) {
    if (filename) {
        if (format == AUTO) {
            if (cmpExtension(filename, ".png")) format = PNG;
            else if (cmpExtension(filename, ".bmp")) format = BMP;
            else if (cmpExtension(filename, ".txt")) format = TEXT;
            else if (cmpExtension(filename, ".bin")) format = BINARY;
			else if (cmpExtension(filename, ".dds")) format = DDS;
            else
                return "Could not deduce format from output file name.";
        }
        switch (format) {
            case PNG: return savePng(bitmap, filename) ? NULL : "Failed to write output PNG image.";
            case BMP: return saveBmp(bitmap, filename) ? NULL : "Failed to write output BMP image.";
			case DDS: return saveDDS(bitmap, filename) ? NULL : "Failed to write output DDS image";
            case TEXT: case TEXT_FLOAT: {
                FILE *file = fopen(filename, "w");
                if (!file) return "Failed to write output text file.";
                if (format == TEXT)
                    writeTextBitmap(file, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width(), bitmap.height());
                else if (format == TEXT_FLOAT)
                    writeTextBitmapFloat(file, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width(), bitmap.height());
                fclose(file);
                return NULL;
            }
            case BINARY: case BINARY_FLOAT: case BINART_FLOAT_BE: {
                FILE *file = fopen(filename, "wb");
                if (!file) return "Failed to write output binary file.";
                if (format == BINARY)
                    writeBinBitmap(file, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width()*bitmap.height());
                else if (format == BINARY_FLOAT)
                    writeBinBitmapFloat(file, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width()*bitmap.height());
                else if (format == BINART_FLOAT_BE)
                    writeBinBitmapFloatBE(file, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width()*bitmap.height());
                fclose(file);
                return NULL;
            }
			
            default:
                break;
        }
    } else {
        if (format == AUTO || format == TEXT)
            writeTextBitmap(stdout, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width(), bitmap.height());
        else if (format == TEXT_FLOAT)
            writeTextBitmapFloat(stdout, reinterpret_cast<const float *>(&bitmap(0, 0)), sizeof(T)/sizeof(float)*bitmap.width(), bitmap.height());
        else
            return "Unsupported format for standard output.";
    }
    return NULL;
}

void PackGlyphs(std::vector<Glyph>& glyphs, int glyphSize, int& width, int& height) {
	stbrp_context context;
	//calc possible size
	int w = (int)sqrt(glyphSize * glyphSize * glyphs.size()) + 1;
	w = (w + glyphSize - 1) & ~(glyphSize - 1); //make image divisable by four to make sure compression can work
	stbrp_node* nodes = (stbrp_node*)malloc(w * 2 * sizeof(stbrp_node));
	width = w; height = w;
	stbrp_init_target(&context, w, w, nodes, w * 2);
	stbrp_rect* rects = (stbrp_rect*)malloc(glyphs.size() * sizeof(stbrp_rect));
	//build rects
	//TODO: Pack them into smaller rectangles than the base size based on metrics
	for (unsigned i = 0; i < glyphs.size(); ++i) {
		rects[i].w = glyphSize;
		rects[i].h = glyphSize;
		rects[i].id = i;
	}
	
	stbrp_pack_rects(&context, rects, glyphs.size());

	for (unsigned i = 0; i < glyphs.size(); ++i) {
		glyphs[i].x = rects[i].x;
		glyphs[i].y = rects[i].y;
	}

	free(rects);
	free(nodes);
}

void WriteGlyphsToAtlas(std::vector<Glyph>& glyphs, int glyphSize, Bitmap<FloatRGB>& atlas) {
	for (auto& g : glyphs) {
		for (int y = 0; y < glyphSize; ++y) {
			for (int x = 0; x < glyphSize; ++x) {
				atlas(g.x + x, g.y + y) = g.bitmap(x, y);
			}
		}
	}
}

void ConvertJsonToSjson(std::string& in) {
	std::replace(in.begin(), in.end(), ':', '=');
	in.erase(std::remove(in.begin(), in.end(), '"'), in.end());
	in.erase(std::remove(in.begin(), in.end(), ','), in.end());
	if (in[0] == '{')
		in[0] = ' ';
	if (in[in.size() - 1] == '}')
		in[in.size() - 1] = ' ';
}

void SerializeGlyphs(const std::vector<Glyph>& glyphs,int charSize, int atlasWidth, int atlasHeight, const char* filename) {
	using namespace nlohmann;
	std::stringstream ss;
	try {
		json root;
		root["width"] = atlasWidth;
		root["height"] = atlasHeight;
		root["size"] = charSize;
		root["line_height"] = 45; //TODO: Figure out what these do
		root["base_line"] = 35;
		for (auto& g : glyphs) {
			json o;
			o["x"] = g.x;
			o["y"] = g.y;
			o["code"] = g.code;
			o["xadvance"] = g.advance;
			o["width"] = g.width;
			o["height"] = g.height;
			o["xoffset"] = g.xoffset;
			o["yoffset"] = g.yoffset;
			root["glyphs"].push_back(o);
		}
		ss << std::setw(4) << root;
	}
	catch (std::exception e) {
		printf("json error: %s\n", e.what());
	}
	std::string s = ss.str();
	ConvertJsonToSjson(s);

	std::string file(filename);
	file.erase(file.begin() + file.find_last_of('.'), file.end());
	file += ".font";

	std::ofstream os(file);
	os << std::setw(4) << s;
	os.close();
}

static const char *helpText =
    "\n"
    "Multi-channel signed distance field generator by Viktor Chlumsky v" MSDFGEN_VERSION "\n"
    "---------------------------------------------------------------------\n"
    "  Usage: msdfgen"
    #ifdef _WIN32
        ".exe"
    #endif
        " <mode> <input specification> <options>\n"
    "\n"
    "MODES\n"
    "  sdf - Generate conventional monochrome signed distance field.\n"
    "  psdf - Generate monochrome signed pseudo-distance field.\n"
    "  msdf - Generate multi-channel signed distance field. This is used by default if no mode is specified.\n"
    "  metrics - Report shape metrics only.\n"
    "\n"
    "INPUT SPECIFICATION\n"
    "  -defineshape <definition>\n"
        "\tDefines input shape using the ad-hoc text definition.\n"
    "  -font <filename.ttf> <character code>\n"
        "\tLoads a single glyph from the specified font file. Format of character code is '?', 63 or 0x3F.\n"
    "  -shapedesc <filename.txt>\n"
        "\tLoads text shape description from a file.\n"
    "  -stdin\n"
        "\tReads text shape description from the standard input.\n"
    "  -svg <filename.svg>\n"
        "\tLoads the last vector path found in the specified SVG file.\n"
    "\n"
    "OPTIONS\n"
    "  -angle <angle>\n"
        "\tSpecifies the minimum angle between adjacent edges to be considered a corner. Append D for degrees.\n"
    "  -ascale <x scale> <y scale>\n"
        "\tSets the scale used to convert shape units to pixels asymmetrically.\n"
    "  -autoframe\n"
        "\tAutomatically scales (unless specified) and translates the shape to fit.\n"
    "  -edgecolors <sequence>\n"
        "\tOverrides automatic edge coloring with the specified color sequence.\n"
    "  -errorcorrection <threshold>\n"
        "\tChanges the threshold used to detect and correct potential artifacts. 0 disables error correction.\n"
    "  -exportshape <filename.txt>\n"
        "\tSaves the shape description into a text file that can be edited and loaded using -shapedesc.\n"
    "  -format <png / bmp / text / textfloat / bin / binfloat / binfloatbe>\n"
        "\tSpecifies the output format of the distance field. Otherwise it is chosen based on output file extension.\n"
    "  -help\n"
        "\tDisplays this help.\n"
    "  -keeporder\n"
        "\tDisables the detection of shape orientation and keeps it as is.\n"
    "  -legacy\n"
        "\tUses the original (legacy) distance field algorithms.\n"
    "  -o <filename>\n"
        "\tSets the output file name. The default value is \"output.png\".\n"
    "  -printmetrics\n"
        "\tPrints relevant metrics of the shape to the standard output.\n"
    "  -pxrange <range>\n"
        "\tSets the width of the range between the lowest and highest signed distance in pixels.\n"
    "  -range <range>\n"
        "\tSets the width of the range between the lowest and highest signed distance in shape units.\n"
    "  -scale <scale>\n"
        "\tSets the scale used to convert shape units to pixels.\n"
    "  -size <width> <height>\n"
        "\tSets the dimensions of the output image.\n"
    "  -stdout\n"
        "\tPrints the output instead of storing it in a file. Only text formats are supported.\n"
    "  -testrender <filename.png> <width> <height>\n"
        "\tRenders an image preview using the generated distance field and saves it as a PNG file.\n"
    "  -testrendermulti <filename.png> <width> <height>\n"
        "\tRenders an image preview without flattening the color channels.\n"
    "  -translate <x> <y>\n"
        "\tSets the translation of the shape in shape units.\n"
    "  -reverseorder\n"
        "\tDisables the detection of shape orientation and reverses the order of its vertices.\n"
    "  -seed <n>\n"
        "\tSets the random seed for edge coloring heuristic.\n"
    "  -yflip\n"
        "\tInverts the Y axis in the output distance field. The default order is bottom to top.\n"
    "\n";

int main(int argc, const char * const *argv) {
    #define ABORT(msg) { puts(msg); return 1; }

    // Parse command line arguments
    enum {
        NONE,
        SVG,
        FONT,
        DESCRIPTION_ARG,
        DESCRIPTION_STDIN,
        DESCRIPTION_FILE
    } inputType = NONE;
    enum {
        SINGLE,
        PSEUDO,
        MULTI,
        METRICS
    } mode = MULTI;
    bool legacyMode = false;
    Format format = AUTO;
    const char *input = NULL;
    const char *output = "output.png";
    const char *shapeExport = NULL;
    const char *testRender = NULL;
    const char *testRenderMulti = NULL;
    bool outputSpecified = false;
    int unicode = 0;
	std::vector<int> unicodes;
    int svgPathIndex = 0;

    int width = 64, height = 64;
    int testWidth = 0, testHeight = 0;
    int testWidthM = 0, testHeightM = 0;
    bool autoFrame = false;
    enum {
        RANGE_UNIT,
        RANGE_PX
    } rangeMode = RANGE_PX;
    double range = 1;
    double pxRange = 2;
    Vector2 translate;
    Vector2 scale = 1;
    bool scaleSpecified = false;
    double angleThreshold = 3;
    double edgeThreshold = 1.00000001;
    bool defEdgeAssignment = true;
    const char *edgeAssignment = NULL;
    bool yFlip = false;
    bool printMetrics = false;
    bool skipColoring = false;
    enum {
        KEEP,
        REVERSE,
        GUESS
    } orientation = GUESS;
    unsigned long long coloringSeed = 0;

    int argPos = 1;
    bool suggestHelp = false;
    while (argPos < argc) {
        const char *arg = argv[argPos];
        #define ARG_CASE(s, p) if (!strcmp(arg, s) && argPos+(p) < argc)
        #define ARG_MODE(s, m) if (!strcmp(arg, s)) { mode = m; ++argPos; continue; }
        #define SETFORMAT(fmt, ext) do { format = fmt; if (!outputSpecified) output = "output." ext; } while (false)

        ARG_MODE("sdf", SINGLE)
        ARG_MODE("psdf", PSEUDO)
        ARG_MODE("msdf", MULTI)
        ARG_MODE("metrics", METRICS)

        ARG_CASE("-svg", 1) {
            inputType = SVG;
            input = argv[argPos+1];
            argPos += 2;
            continue;
        }
        ARG_CASE("-font", 2) {
            inputType = FONT;
            input = argv[argPos+1];
            parseUnicode(unicode, argv[argPos+2]);
            argPos += 3;
            continue;
        }
        ARG_CASE("-defineshape", 1) {
            inputType = DESCRIPTION_ARG;
            input = argv[argPos+1];
            argPos += 2;
            continue;
        }
        ARG_CASE("-stdin", 0) {
            inputType = DESCRIPTION_STDIN;
            input = "stdin";
            argPos += 1;
            continue;
        }
        ARG_CASE("-shapedesc", 1) {
            inputType = DESCRIPTION_FILE;
            input = argv[argPos+1];
            argPos += 2;
            continue;
        }
        ARG_CASE("-o", 1) {
            output = argv[argPos+1];
            outputSpecified = true;
            argPos += 2;
            continue;
        }
        ARG_CASE("-stdout", 0) {
            output = NULL;
            argPos += 1;
            continue;
        }
        ARG_CASE("-legacy", 0) {
            legacyMode = true;
            argPos += 1;
            continue;
        }
        ARG_CASE("-format", 1) {
            if (!strcmp(argv[argPos+1], "auto")) format = AUTO;
            else if (!strcmp(argv[argPos+1], "png")) SETFORMAT(PNG, "png");
            else if (!strcmp(argv[argPos+1], "bmp")) SETFORMAT(BMP, "bmp");
            else if (!strcmp(argv[argPos+1], "text") || !strcmp(argv[argPos+1], "txt")) SETFORMAT(TEXT, "txt");
            else if (!strcmp(argv[argPos+1], "textfloat") || !strcmp(argv[argPos+1], "txtfloat")) SETFORMAT(TEXT_FLOAT, "txt");
            else if (!strcmp(argv[argPos+1], "bin") || !strcmp(argv[argPos+1], "binary")) SETFORMAT(BINARY, "bin");
            else if (!strcmp(argv[argPos+1], "binfloat") || !strcmp(argv[argPos+1], "binfloatle")) SETFORMAT(BINARY_FLOAT, "bin");
            else if (!strcmp(argv[argPos+1], "binfloatbe")) SETFORMAT(BINART_FLOAT_BE, "bin");
            else
                puts("Unknown format specified.");
            argPos += 2;
            continue;
        }
        ARG_CASE("-size", 2) {
            unsigned w, h;
            if (!parseUnsigned(w, argv[argPos+1]) || !parseUnsigned(h, argv[argPos+2]) || !w || !h)
                ABORT("Invalid size arguments. Use -size <width> <height> with two positive integers.");
            width = w, height = h;
			scale = w / 32.0f;
			scaleSpecified = true;
            argPos += 3;
            continue;
        }
        ARG_CASE("-autoframe", 0) {
            autoFrame = true;
            argPos += 1;
            continue;
        }
        ARG_CASE("-range", 1) {
            double r;
            if (!parseDouble(r, argv[argPos+1]) || r < 0)
                ABORT("Invalid range argument. Use -range <range> with a positive real number.");
            rangeMode = RANGE_UNIT;
            range = r;
            argPos += 2;
            continue;
        }
        ARG_CASE("-pxrange", 1) {
            double r;
            if (!parseDouble(r, argv[argPos+1]) || r < 0)
                ABORT("Invalid range argument. Use -pxrange <range> with a positive real number.");
            rangeMode = RANGE_PX;
            pxRange = r;
            argPos += 2;
            continue;
        }
        ARG_CASE("-scale", 1) {
            double s;
            if (!parseDouble(s, argv[argPos+1]) || s <= 0)
                ABORT("Invalid scale argument. Use -scale <scale> with a positive real number.");
            scale = s;
            scaleSpecified = true;
            argPos += 2;
            continue;
        }
        ARG_CASE("-ascale", 2) {
            double sx, sy;
            if (!parseDouble(sx, argv[argPos+1]) || !parseDouble(sy, argv[argPos+2]) || sx <= 0 || sy <= 0)
                ABORT("Invalid scale arguments. Use -ascale <x> <y> with two positive real numbers.");
            scale.set(sx, sy);
            scaleSpecified = true;
            argPos += 3;
            continue;
        }
        ARG_CASE("-translate", 2) {
            double tx, ty;
            if (!parseDouble(tx, argv[argPos+1]) || !parseDouble(ty, argv[argPos+2]))
                ABORT("Invalid translate arguments. Use -translate <x> <y> with two real numbers.");
            translate.set(tx, ty);
            argPos += 3;
            continue;
        }
        ARG_CASE("-angle", 1) {
            double at;
            if (!parseAngle(at, argv[argPos+1]))
                ABORT("Invalid angle threshold. Use -angle <min angle> with a positive real number less than PI or a value in degrees followed by 'd' below 180d.");
            angleThreshold = at;
            argPos += 2;
            continue;
        }
        ARG_CASE("-errorcorrection", 1) {
            double et;
            if (!parseDouble(et, argv[argPos+1]) || et < 0)
                ABORT("Invalid error correction threshold. Use -errorcorrection <threshold> with a real number larger or equal to 1.");
            edgeThreshold = et;
            argPos += 2;
            continue;
        }
        ARG_CASE("-edgecolors", 1) {
            static const char *allowed = " ?,cmyCMY";
            for (int i = 0; argv[argPos+1][i]; ++i) {
                for (int j = 0; allowed[j]; ++j)
                    if (argv[argPos+1][i] == allowed[j])
                        goto ROLL_ARG;
                ABORT("Invalid edge coloring sequence. Use -assign <color sequence> with only the colors C, M, and Y. Separate contours by commas and use ? to keep the default assigment for a contour.");
            ROLL_ARG:;
            }
            edgeAssignment = argv[argPos+1];
            argPos += 2;
            continue;
        }
        ARG_CASE("-exportshape", 1) {
            shapeExport = argv[argPos+1];
            argPos += 2;
            continue;
        }
        ARG_CASE("-testrender", 3) {
            unsigned w, h;
            if (!parseUnsigned(w, argv[argPos+2]) || !parseUnsigned(h, argv[argPos+3]) || !w || !h)
                ABORT("Invalid arguments for test render. Use -testrender <output.png> <width> <height>.");
            testRender = argv[argPos+1];
            testWidth = w, testHeight = h;
            argPos += 4;
            continue;
        }
        ARG_CASE("-testrendermulti", 3) {
            unsigned w, h;
            if (!parseUnsigned(w, argv[argPos+2]) || !parseUnsigned(h, argv[argPos+3]) || !w || !h)
                ABORT("Invalid arguments for test render. Use -testrendermulti <output.png> <width> <height>.");
            testRenderMulti = argv[argPos+1];
            testWidthM = w, testHeightM = h;
            argPos += 4;
            continue;
        }
        ARG_CASE("-yflip", 0) {
            yFlip = true;
            argPos += 1;
            continue;
        }
        ARG_CASE("-printmetrics", 0) {
            printMetrics = true;
            argPos += 1;
            continue;
        }
        ARG_CASE("-keeporder", 0) {
            orientation = KEEP;
            argPos += 1;
            continue;
        }
        ARG_CASE("-reverseorder", 0) {
            orientation = REVERSE;
            argPos += 1;
            continue;
        }
        ARG_CASE("-guessorder", 0) {
            orientation = GUESS;
            argPos += 1;
            continue;
        }
        ARG_CASE("-seed", 1) {
            if (!parseUnsignedLL(coloringSeed, argv[argPos+1]))
                ABORT("Invalid seed. Use -seed <N> with N being a non-negative integer.");
            argPos += 2;
            continue;
        }
		ARG_CASE("-textfile", 1) {
			if (!parseTextfile(argv[argPos + 1], unicodes))
				ABORT("Error parsing textfile");
			argPos += 2;
			continue;
		}
        ARG_CASE("-help", 0)
            ABORT(helpText);
        printf("Unknown setting or insufficient parameters: %s\n", arg);
        suggestHelp = true;
        ++argPos;
    }
    if (suggestHelp)
        printf("Use -help for more information.\n");

    // Load input
    Vector2 svgDims;
    double glyphAdvance = 0;
    if (!inputType || !input)
        ABORT("No input specified! Use either -svg <file.svg> or -font <file.ttf/otf> <character code>, or see -help.");

	Shape shape;
	std::vector<Glyph> glyphs;
	if (unicode != 9608)
		unicodes.push_back(unicode);
	if (!unicodes.empty())
		unicode = 9608;

    switch (inputType) {
        case SVG: {
            if (!loadSvgShape(shape, input, svgPathIndex, &svgDims))
                ABORT("Failed to load shape from SVG file.");
            break;
        }
        case FONT: {
            if (!unicode)
                ABORT("No character specified! Use -font <file.ttf/otf> <character code>. Character code can be a number (65, 0x41), or a character in apostrophes ('A').");
            FreetypeHandle *ft = initializeFreetype();
            if (!ft) return -1;
            FontHandle *font = loadFont(ft, input);
            if (!font) {
                deinitializeFreetype(ft);
                ABORT("Failed to load font file.");
            }
			for (auto& c : unicodes) {
				if (!loadGlyph(shape, font, c, &glyphAdvance)) {
					destroyFont(font);
					deinitializeFreetype(ft);
					ABORT("Failed to load glyph from font file.");
				}
				Glyph g;
				g.shape = shape;
				g.code = c;
				g.width = width;
				g.height = height;
				g.xoffset = 0;
				g.yoffset = 0;
				glyphs.push_back(g);
			}
            destroyFont(font);
            deinitializeFreetype(ft);
            break;
        }
        case DESCRIPTION_ARG: {
            if (!readShapeDescription(input, shape, &skipColoring))
                ABORT("Parse error in shape description.");
            break;
        }
        case DESCRIPTION_STDIN: {
            if (!readShapeDescription(stdin, shape, &skipColoring))
                ABORT("Parse error in shape description.");
            break;
        }
        case DESCRIPTION_FILE: {
            FILE *file = fopen(input, "r");
            if (!file)
                ABORT("Failed to load shape description file.");
            if (!readShapeDescription(file, shape, &skipColoring))
                ABORT("Parse error in shape description.");
            fclose(file);
            break;
        }
        default:
            break;
    }
	if (glyphs.empty()) {
		Glyph g;
		g.shape = shape;
		g.code = unicode;
		g.width = width;
		g.height = height;
		g.xoffset = 0;
		g.yoffset = 0;
		glyphs.push_back(g);
	}

	std::vector<Bitmap<float>> sdfList;
    // Validate and normalize shape
	for (auto& g : glyphs) {
		if (!g.shape.validate())
			ABORT("The geometry of the loaded shape is invalid.");
		g.shape.normalize();
		if (yFlip)
			g.shape.inverseYAxis = !g.shape.inverseYAxis;

		double avgScale = .5*(scale.x + scale.y);
		struct {
			double l, b, r, t;
		} bounds = {
			LARGE_VALUE, LARGE_VALUE, -LARGE_VALUE, -LARGE_VALUE
		};
		if (autoFrame || mode == METRICS || printMetrics || orientation == GUESS)
			g.shape.bounds(bounds.l, bounds.b, bounds.r, bounds.t);
		// Auto-frame
		if (autoFrame) {
			double l = bounds.l, b = bounds.b, r = bounds.r, t = bounds.t;
			Vector2 frame(width, height);
			if (rangeMode == RANGE_UNIT)
				l -= range, b -= range, r += range, t += range;
			else if (!scaleSpecified)
				frame -= 2 * pxRange;
			if (l >= r || b >= t)
				l = 0, b = 0, r = 1, t = 1;
			if (frame.x <= 0 || frame.y <= 0)
				ABORT("Cannot fit the specified pixel range.");
			Vector2 dims(r - l, t - b);
			if (scaleSpecified)
				translate = .5*(frame / scale - dims) - Vector2(l, b);
			else {
				if (dims.x*frame.y < dims.y*frame.x) {
					translate.set(.5*((frame.x / frame.y) * dims.y - dims.x) - l, -b);
					scale = avgScale = frame.y / dims.y;
				}
				else {
					translate.set(-l, .5*((frame.y / frame.x) * dims.x - dims.y) - b);
					scale = avgScale = frame.x / dims.x;
				}
			}
			if (rangeMode == RANGE_PX && !scaleSpecified)
				translate += pxRange / scale;
		}
	

		if (rangeMode == RANGE_PX)
			range = pxRange/min(scale.x, scale.y);
	

		// Compute output
		Bitmap<float> sdf;
		Bitmap<FloatRGB> msdf;
		switch (mode) {
			case SINGLE: {
				sdf = Bitmap<float>(width, height);
				if (legacyMode)
					generateSDF_legacy(sdf, g.shape, range, scale, translate);
				else
					generateSDF(sdf, g.shape, range, scale, translate);
				break;
			}
			case PSEUDO: {
				sdf = Bitmap<float>(width, height);
				if (legacyMode)
					generatePseudoSDF_legacy(sdf, g.shape, range, scale, translate);
				else
					generatePseudoSDF(sdf, g.shape, range, scale, translate);
				break;
			}
			case MULTI: {
				if (!skipColoring)
					edgeColoringSimple(g.shape, angleThreshold, coloringSeed);
				if (edgeAssignment)
					parseColoring(g.shape, edgeAssignment);
				g.bitmap = Bitmap<FloatRGB>(width, height);
				if (legacyMode)
					generateMSDF_legacy(g.bitmap, g.shape, range, scale, translate, edgeThreshold);
				else
					generateMSDF(g.bitmap, g.shape, range, scale, translate, edgeThreshold);
				break;
			}
			default:
				break;
		}

		if (orientation == GUESS) {
			// Get sign of signed distance outside bounds
			Point2 p(bounds.l-(bounds.r-bounds.l)-1, bounds.b-(bounds.t-bounds.b)-1);
			double dummy;
			SignedDistance minDistance;
			for (std::vector<Contour>::const_iterator contour = shape.contours.begin(); contour != shape.contours.end(); ++contour)
				for (std::vector<EdgeHolder>::const_iterator edge = contour->edges.begin(); edge != contour->edges.end(); ++edge) {
					SignedDistance distance = (*edge)->signedDistance(p, dummy);
					if (distance < minDistance)
						minDistance = distance;
				}
			orientation = minDistance.distance <= 0 ? KEEP : REVERSE;
		}
		if (orientation == REVERSE) {
			invertColor(sdf);
			invertColor(g.bitmap);
		}

		//update data
		g.advance = bounds.r + bounds.l;
		g.xoffset = -translate.x;
		g.yoffset = translate.y;
	}
	//collect glyphs
	int atlasWidth, atlasHeight;
	PackGlyphs(glyphs, width, atlasWidth, atlasHeight);
	Bitmap<FloatRGB> atlas(atlasWidth, atlasHeight);
	WriteGlyphsToAtlas(glyphs, width, atlas);
	SerializeGlyphs(glyphs,width, atlasWidth, atlasHeight, output);
	saveMaterial(output);
	saveTexture(output);
	const char *error = NULL;
	switch (mode) {
	    case MULTI:
	        error = writeOutput(atlas, output, format);
	        if (error)
	            ABORT(error);
	        break;
	    default:
	        break;
	}

	
    // Save output
    //if (shapeExport) {
    //    FILE *file = fopen(shapeExport, "w");
    //    if (file) {
    //        writeShapeDescription(file, shape);
    //        fclose(file);
    //    } else
    //        puts("Failed to write shape export file.");
    //}
    //const char *error = NULL;
    //switch (mode) {
    //    case SINGLE:
    //    case PSEUDO:
    //        error = writeOutput(sdf, output, format);
    //        if (error)
    //            ABORT(error);
    //        break;
    //    case MULTI:
    //        error = writeOutput(msdf, output, format);
    //        if (error)
    //            ABORT(error);
    //        break;
    //    default:
    //        break;
    //}

    return 0;
}

#endif
