#include "save_material.h"
#include <string>

const char* matfile = "%s = { \n\
		material_contexts = {\n\
			surface_material = \"\"\n\
		}\n\
		shader = \"gui_gradient:DIFFUSE_MAP:MC_DISTANCE_FIELD\"\n\
		textures = {\n\
			diffuse_map = \"materials/fonts/%s\"\n\
		}\n\
		variables = {\n\
			max_value = {\n\
			type = \"vector2\"\n\
			value = [\n\
				0.35\n\
				1\n\
			]\n\
		}\n\
		min_value = {\n\
			type = \"vector2\"\n\
			value = [\n\
				0\n\
				0\n\
			]\n\
		}\n\
		outline_color = {\n\
			type = \"vector3\"\n\
			value = [\n\
				0\n\
				0\n\
				0\n\
			]\n\
		}\n\
		smoothing_denominator = {\n\
			type = \"scalar\"\n\
			value = 4\n\
		}\n\
	}\n\
}";

const char* textureFile = "{\n\
	common: {\n\
		input: {\n\
			filename: \"materials/fonts/%s\"\n\
		},\n\
		output : {\n\
			apply_processing: true,\n\
			category : \"texture_categories/gui\",\n\
			cut_alpha_threshold : 0.5,\n\
			enable_cut_alpha_threshold : false,\n\
			format : \"A8R8G8B8\",\n\
			mipmap_filter : \"kaiser\",\n\
			mipmap_filter_wrap_mode : \"mirror\",\n\
			mipmap_keep_original : false,\n\
			mipmap_num_largest_steps_to_discard : 0,\n\
			mipmap_num_smallest_steps_to_discard : 0\n\
		}\n\
	}\n\
}";

void msdfgen::saveMaterial(const char* filename) {
	std::string file(filename);
	file.erase(file.begin() + file.find_last_of('.'), file.end());

	char* buffer = (char*)malloc(1000);
	sprintf(buffer, matfile, file.c_str(), file.c_str());

	file += ".material";
	size_t l = strlen(buffer);
	FILE* f = fopen(file.c_str(), "wb");
	if (f) {
		fwrite(buffer, sizeof(char), l, f);
		fclose(f);
	}

	free(buffer);
}

void msdfgen::saveTexture(const char* filename) {
	std::string file(filename);
	file.erase(file.begin() + file.find_last_of('.'), file.end());

	char* buffer = (char*)malloc(1000);
	sprintf(buffer, textureFile, file.c_str());

	file += ".texture";
	size_t l = strlen(buffer);
	FILE* f = fopen(file.c_str(), "wb");
	if (f) {
		fwrite(buffer, sizeof(char), l, f);
		fclose(f);
	}

	free(buffer);
}