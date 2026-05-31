// GB Enhanced+ Copyright Daniel Baxter 2015
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : custom_graphics.h
// Date : May 31, 2026
// Description : Custom graphics data for GBA
//
// Defines the data structure that hold info about custom GBA graphics

#ifndef GBA_CGFX_DATA
#define GBA_CGFX_DATA

#include <vector>
#include <string>
#include <map>

#include "SDL2/SDL_opengl.h"
#include "common/common.h"

struct gba_cgfx_data
{
	//Manifest parsing
	std::vector<std::string> manifest;
	std::vector<u8> manifest_entry_size;

	//Data pulled from manifest file - Regular entries
	std::map<std::string, u32> m_hashes;
	std::map<std::string, u32> m_hashes_raw;
	std::vector<std::string> m_files;
	std::vector<u8> m_types;
	std::vector<u16> m_id;
	std::vector<u16> m_auto_bright;

	//Data pulled from manifest file - Metatile entries
	std::vector<std::string> m_meta_files;
	std::vector<std::string> m_meta_names;
	std::vector<u8> m_meta_forms;
	std::vector<u32> m_meta_width;
	std::vector<u32> m_meta_height;

	u32 last_id;

	//Working hash list of graphics in VRAM
	std::vector<std::string> current_bg_hash[4];  // One per BG layer (0-3)
	std::vector<std::string> current_obj_hash;

	//List of all computed hashes
	std::vector<std::string> obj_hash_list;
	std::vector<std::string> bg_hash_list;

	//Pixel data for all computed hashes (lazy loaded)
	std::vector<std::vector<u32>> obj_pixel_data;
	std::vector<std::vector<u32>> bg_pixel_data;
	std::vector<std::vector<u32>> meta_pixel_data;

	//GPU textures for CGFX tiles (0 = not created)
	std::vector<GLuint> obj_textures;
	std::vector<GLuint> bg_textures;

	//Whether pixel data is loaded for each entry
	std::vector<bool> obj_loaded;
	std::vector<bool> bg_loaded;

	//Image dimensions per entry
	std::vector<u32> obj_img_width;
	std::vector<u32> obj_img_height;
	std::vector<u32> bg_img_width;
	std::vector<u32> bg_img_height;

	//LRU tracking
	std::vector<u32> obj_last_used;
	std::vector<u32> bg_last_used;
	u32 cgfx_current_frame;

	//Maximum number of cached tiles (0 = unlimited, default 512 for GBA)
	u32 cgfx_max_cached;

	//Deferred loading queue
	std::vector<u32> pending_loads;

	//BG update tracking
	std::vector<bool> bg_update_list[4];
	bool update_bg;

	//Palette brightness max and min
	u8 bg_pal_max[16];
	u8 bg_pal_min[16];
	u8 obj_pal_max[16];
	u8 obj_pal_min[16];
};

#endif // GBA_CGFX_DATA
