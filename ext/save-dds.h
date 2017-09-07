#pragma once

#include "../core/Bitmap.h"
#include "../core/arithmetics.hpp"
namespace msdfgen {

	/// Saves the bitmap as a PNG file.
	//bool saveDDS(const Bitmap<float> &bitmap, const char *filename);
	bool saveDDS(const Bitmap<FloatRGB> &bitmap, const char *filename);

}