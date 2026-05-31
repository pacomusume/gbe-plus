// GB Enhanced+ Copyright Daniel Baxter 2015
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : custom_graphics.cpp
// Date : May 31, 2026
// Description : GBA custom graphics
//
// Handles dumping original BG and Sprite tiles
// Handles loading custom pixel data for GBA

#include <fstream>
#include <sstream>

#include "SDL2/SDL_image.h"
#include "common/hash.h"
#include "common/util.h"
#include "common/cgfx_common.h"
#include "lcd.h"

/****** Loads the manifest file ******/
bool AGB_LCD::load_manifest(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::in);
	std::string input_line = "";
	std::string line_char = "";

	//Clear existing hash data
	cgfx_stat.obj_hash_list.clear();
	cgfx_stat.bg_hash_list.clear();

	//Clear existing manifest data
	cgfx_stat.manifest.clear();
	cgfx_stat.manifest_entry_size.clear();
	cgfx_stat.m_hashes.clear();
	cgfx_stat.m_hashes_raw.clear();
	cgfx_stat.m_files.clear();
	cgfx_stat.m_types.clear();
	cgfx_stat.m_id.clear();
	cgfx_stat.m_auto_bright.clear();

	cgfx_stat.m_meta_files.clear();
	cgfx_stat.m_meta_names.clear();
	cgfx_stat.m_meta_forms.clear();

	//Clear existing pixel data
	cgfx_stat.obj_pixel_data.clear();
	cgfx_stat.bg_pixel_data.clear();
	cgfx_stat.meta_pixel_data.clear();

	//Clear GPU texture data
	cgfx_stat.obj_textures.clear();
	cgfx_stat.bg_textures.clear();
	cgfx_stat.obj_loaded.clear();
	cgfx_stat.bg_loaded.clear();
	cgfx_stat.obj_img_width.clear();
	cgfx_stat.obj_img_height.clear();
	cgfx_stat.bg_img_width.clear();
	cgfx_stat.bg_img_height.clear();
	cgfx_stat.obj_last_used.clear();
	cgfx_stat.bg_last_used.clear();
	cgfx_stat.cgfx_current_frame = 0;
	cgfx_stat.pending_loads.clear();
	cgfx_stat.cgfx_max_cached = 512;

	if(!file.is_open())
	{
		std::cout<<"CGFX::Could not open manifest file " << filename << ". Check file path or permissions. \n";
		return false;
	}

	//Cycle through whole file, line-by-line
	while(getline(file, input_line))
	{
		line_char = input_line[0];
		bool ignore = false;
		u8 item_count = 0;

		if(line_char == "[")
		{
			std::string line_item = "";

			for(int x = 0; ++x < input_line.length();)
			{
				line_char = input_line[x];

				if((line_char == "'") && (!ignore)) { ignore = true; }
				else if((line_char == "'") && (ignore)) { ignore = false; }
				else if(((line_char == ":") || (line_char == "]")) && (!ignore))
				{
					cgfx_stat.manifest.push_back(line_item);
					line_item = "";
					item_count++;
				}
				else { line_item += line_char; }
			}
		}

		if((item_count != 5) && (item_count != 3) && (item_count != 0))
		{
			std::cout<<"CGFX::Manifest file " << filename << " has some missing parameters for some entries. \n";
			file.close();
			return false;
		}
		else if(item_count != 0) { cgfx_stat.manifest_entry_size.push_back(item_count); }
	}

	file.close();

	u32 hash_id = 0;

	//Parse entries
	for(int x = 0, y = 0; y < cgfx_stat.manifest_entry_size.size(); y++)
	{
		if(cgfx_stat.manifest_entry_size[y] == 5)
		{
			std::string hash = cgfx_stat.manifest[x++];
			std::string sub_hash = "";

			if((cgfx_stat.m_hashes.find(hash) == cgfx_stat.m_hashes.end()))
			{
				cgfx_stat.m_files.push_back(cgfx_stat.manifest[x++]);

				u32 type_byte = 0;
				util::from_str(cgfx_stat.manifest[x++], type_byte);
				cgfx_stat.m_types.push_back(type_byte);

				switch(type_byte)
				{
					//GBA OBJ
					case 3:
						sub_hash = hash.substr(4);
						cgfx_stat.m_id.push_back(cgfx_stat.obj_hash_list.size());
						cgfx_stat.obj_hash_list.push_back(hash);
						cgfx_stat.m_hashes.insert(std::make_pair(hash, hash_id));
						cgfx_stat.m_hashes_raw.insert(std::make_pair(sub_hash, hash_id));
						break;

					//GBA BG
					case 30:
						sub_hash = hash.substr(4);
						cgfx_stat.m_id.push_back(cgfx_stat.bg_hash_list.size());
						cgfx_stat.bg_hash_list.push_back(hash);
						cgfx_stat.m_hashes.insert(std::make_pair(hash, hash_id));
						cgfx_stat.m_hashes_raw.insert(std::make_pair(sub_hash, hash_id));
						break;

					default:
						std::cout<<"CGFX::Undefined hash type " << (int)type_byte << "\n";
						return false;
				}

				//Push placeholder for lazy loading
				if(type_byte < 10)
				{
					cgfx_stat.obj_pixel_data.push_back(std::vector<u32>());
					cgfx_stat.obj_textures.push_back(0);
					cgfx_stat.obj_loaded.push_back(false);
					cgfx_stat.obj_img_width.push_back(0);
					cgfx_stat.obj_img_height.push_back(0);
					cgfx_stat.obj_last_used.push_back(0);
				}
				else
				{
					cgfx_stat.bg_pixel_data.push_back(std::vector<u32>());
					cgfx_stat.bg_textures.push_back(0);
					cgfx_stat.bg_loaded.push_back(false);
					cgfx_stat.bg_img_width.push_back(0);
					cgfx_stat.bg_img_height.push_back(0);
					cgfx_stat.bg_last_used.push_back(0);
				}

				x += 2; // Skip vram_addr and auto_bright fields
				u32 bright_value = 0;
				cgfx_stat.m_auto_bright.push_back(0);
				hash_id++;
			}
			else
			{
				std::cout<<"CGFX::Warning - Duplicate hash detected.\n";
				x += 4;
			}
		}
		//Parse metatile entries
		else
		{
			cgfx_stat.m_meta_files.push_back(cgfx_stat.manifest[x++]);

			if(!load_meta_data()) { return false; }

			cgfx_stat.m_meta_names.push_back(cgfx_stat.manifest[x++]);

			u32 form_value = 0;
			util::from_str(cgfx_stat.manifest[x++], form_value);
			cgfx_stat.m_meta_forms.push_back(form_value);
		}
	}

	std::cout<<"CGFX::" << filename << " loaded successfully\n";
	return true;
}

/****** Loads meta tile image data ******/
bool AGB_LCD::load_meta_data()
{
	std::string filename = config::data_path + cgfx_stat.m_meta_files.back();
	SDL_Surface* source = IMG_Load(filename.c_str());

	if(source == NULL)
	{
		std::cout<<"GBE::CGFX - Could not load " << filename << "\n";
		return false;
	}

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(source);
	if(converted == NULL) { return false; }
	source = converted;

	if((source->w % 8) != 0 || (source->h % 8) != 0)
	{
		std::cout<<"GBE::CGFX - " << filename << " has irregular dimensions\n";
		SDL_FreeSurface(source);
		return false;
	}

	std::vector<u32> cgfx_pixels;
	u32* pixel_data = (u32*)source->pixels;

	for(int a = 0; a < (source->w * source->h); a++)
	{
		cgfx_pixels.push_back(pixel_data[a] | 0xFF000000);
	}

	cgfx_stat.meta_pixel_data.push_back(cgfx_pixels);
	cgfx_stat.m_meta_width.push_back(source->w);
	cgfx_stat.m_meta_height.push_back(source->h);

	SDL_FreeSurface(source);
	return true;
}

/****** Loads image data for a specific hash ID (lazy loading) ******/
bool AGB_LCD::load_image_data_by_id(u32 hash_id)
{
	if(hash_id >= cgfx_stat.m_files.size()) { return false; }

	std::string filename = config::data_path + cgfx_stat.m_files[hash_id];
	u8 type_byte = cgfx_stat.m_types[hash_id];
	u16 pixel_id = cgfx_stat.m_id[hash_id];

	SDL_Surface* source = IMG_Load(filename.c_str());

	if(source == NULL)
	{
		if(find_meta_data_by_id(hash_id)) { return true; }
		return false;
	}

	SDL_Surface* converted = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(source);
	if(converted == NULL) { return false; }
	source = converted;

	if((source->w % 8) != 0 || (source->h % 8) != 0)
	{
		SDL_FreeSurface(source);
		return false;
	}

	std::vector<u32> cgfx_pixels;
	cgfx_pixels.reserve(source->w * source->h);
	u32* pixel_data = (u32*)source->pixels;

	for(int a = 0; a < (source->w * source->h); a++)
	{
		cgfx_pixels.push_back(pixel_data[a] | 0xFF000000);
	}

	u32 img_w = source->w;
	u32 img_h = source->h;

	if(type_byte < 10)
	{
		if(pixel_id < cgfx_stat.obj_pixel_data.size())
		{
			cgfx_stat.obj_pixel_data[pixel_id] = cgfx_pixels;
			cgfx_stat.obj_loaded[pixel_id] = true;
			cgfx_stat.obj_img_width[pixel_id] = img_w;
			cgfx_stat.obj_img_height[pixel_id] = img_h;
			cgfx_stat.obj_last_used[pixel_id] = cgfx_stat.cgfx_current_frame;
		}
	}
	else
	{
		if(pixel_id < cgfx_stat.bg_pixel_data.size())
		{
			cgfx_stat.bg_pixel_data[pixel_id] = cgfx_pixels;
			cgfx_stat.bg_loaded[pixel_id] = true;
			cgfx_stat.bg_img_width[pixel_id] = img_w;
			cgfx_stat.bg_img_height[pixel_id] = img_h;
			cgfx_stat.bg_last_used[pixel_id] = cgfx_stat.cgfx_current_frame;
		}
	}

	SDL_FreeSurface(source);
	return true;
}

/****** Finds meta data for lazy-loaded entries ******/
bool AGB_LCD::find_meta_data_by_id(u32 hash_id)
{
	if(hash_id >= cgfx_stat.m_files.size()) { return false; }

	std::string original_name = cgfx_stat.m_files[hash_id];
	u8 type_byte = cgfx_stat.m_types[hash_id];
	u16 pixel_id = cgfx_stat.m_id[hash_id];

	std::string base_name = "";
	std::string base_number = "";
	bool is_meta_tile = false;
	u32 meta_tile_number = 0;

	for(int x = original_name.size() - 1; x >= 0; x--)
	{
		std::string temp = "";
		temp += original_name[x];
		if(temp == "_") { is_meta_tile = true; }
		if(is_meta_tile) { base_name = temp + base_name; }
		else { base_number = temp + base_number; }
	}

	base_name = base_name.substr(0, base_name.size() - 1);
	if(!is_meta_tile) { return false; }

	util::from_str(base_number, meta_tile_number);

	u32 meta_id = 0;
	bool found_match = false;

	for(int x = 0; x < cgfx_stat.m_meta_names.size(); x++)
	{
		if(cgfx_stat.m_meta_names[x] == base_name) { found_match = true; meta_id = x; break; }
	}

	if(!found_match || cgfx_stat.m_meta_forms[meta_id] > 3) { return false; }

	std::vector<u32> cgfx_pixels;
	u32 width = cgfx_stat.m_meta_width[meta_id];
	u32 height = cgfx_stat.m_meta_height[meta_id];

	u32 tile_w = width / (8 * cgfx::scaling_factor);
	u32 tile_h = height / (8 * cgfx::scaling_factor);
	u32 pixel_w = width / tile_w;
	u32 pixel_h = height / tile_h;

	u32 pos = (width * pixel_h) * (meta_tile_number / tile_w);
	pos += (meta_tile_number % tile_w) * pixel_w;

	for(int y = 0; y < pixel_h; y++)
	{
		for(int x = 0; x < pixel_w; x++)
		{
			cgfx_pixels.push_back(cgfx_stat.meta_pixel_data[meta_id][pos++]);
		}
		pos -= pixel_w;
		pos += width;
	}

	if(type_byte < 10)
	{
		if(pixel_id < cgfx_stat.obj_pixel_data.size())
		{
			cgfx_stat.obj_pixel_data[pixel_id] = cgfx_pixels;
			cgfx_stat.obj_loaded[pixel_id] = true;
			cgfx_stat.obj_img_width[pixel_id] = pixel_w;
			cgfx_stat.obj_img_height[pixel_id] = pixel_h;
			cgfx_stat.obj_last_used[pixel_id] = cgfx_stat.cgfx_current_frame;
		}
	}
	else
	{
		if(pixel_id < cgfx_stat.bg_pixel_data.size())
		{
			cgfx_stat.bg_pixel_data[pixel_id] = cgfx_pixels;
			cgfx_stat.bg_loaded[pixel_id] = true;
			cgfx_stat.bg_img_width[pixel_id] = pixel_w;
			cgfx_stat.bg_img_height[pixel_id] = pixel_h;
			cgfx_stat.bg_last_used[pixel_id] = cgfx_stat.cgfx_current_frame;
		}
	}

	return true;
}

/****** Ensures CGFX data is loaded for a given hash ID ******/
void AGB_LCD::ensure_cgfx_loaded(u32 hash_id)
{
	if(hash_id >= cgfx_stat.m_types.size()) { return; }

	u8 type_byte = cgfx_stat.m_types[hash_id];
	u16 pixel_id = cgfx_stat.m_id[hash_id];
	bool is_loaded = false;

	if(type_byte < 10)
	{
		if(pixel_id < cgfx_stat.obj_loaded.size()) { is_loaded = cgfx_stat.obj_loaded[pixel_id]; }
	}
	else
	{
		if(pixel_id < cgfx_stat.bg_loaded.size()) { is_loaded = cgfx_stat.bg_loaded[pixel_id]; }
	}

	if(!is_loaded)
	{
		bool already_pending = false;
		for(u32 x = 0; x < cgfx_stat.pending_loads.size(); x++)
		{
			if(cgfx_stat.pending_loads[x] == hash_id) { already_pending = true; break; }
		}
		if(!already_pending) { cgfx_stat.pending_loads.push_back(hash_id); }
	}
	else
	{
		if(type_byte < 10)
		{
			if(pixel_id < cgfx_stat.obj_last_used.size()) { cgfx_stat.obj_last_used[pixel_id] = cgfx_stat.cgfx_current_frame; }
		}
		else
		{
			if(pixel_id < cgfx_stat.bg_last_used.size()) { cgfx_stat.bg_last_used[pixel_id] = cgfx_stat.cgfx_current_frame; }
		}
	}
}

/****** Evicts LRU entries ******/
void AGB_LCD::evict_lru_entries()
{
	if(cgfx_stat.cgfx_max_cached == 0) { return; }

	u32 total_loaded = 0;
	for(u32 x = 0; x < cgfx_stat.obj_loaded.size(); x++) { if(cgfx_stat.obj_loaded[x]) total_loaded++; }
	for(u32 x = 0; x < cgfx_stat.bg_loaded.size(); x++) { if(cgfx_stat.bg_loaded[x]) total_loaded++; }

	if(total_loaded < cgfx_stat.cgfx_max_cached) { return; }

	u32 to_evict = (total_loaded - cgfx_stat.cgfx_max_cached) + (cgfx_stat.cgfx_max_cached / 4);
	if(to_evict > total_loaded) { to_evict = total_loaded; }

	struct lru_entry { u32 frame; u8 type; u32 id; };
	std::vector<lru_entry> lru_list;

	for(u32 x = 0; x < cgfx_stat.obj_loaded.size(); x++)
	{
		if(cgfx_stat.obj_loaded[x]) { lru_entry e; e.frame = cgfx_stat.obj_last_used[x]; e.type = 1; e.id = x; lru_list.push_back(e); }
	}
	for(u32 x = 0; x < cgfx_stat.bg_loaded.size(); x++)
	{
		if(cgfx_stat.bg_loaded[x]) { lru_entry e; e.frame = cgfx_stat.bg_last_used[x]; e.type = 2; e.id = x; lru_list.push_back(e); }
	}

	for(u32 i = 0; i < lru_list.size(); i++)
		for(u32 j = i + 1; j < lru_list.size(); j++)
			if(lru_list[j].frame < lru_list[i].frame) { lru_entry t = lru_list[i]; lru_list[i] = lru_list[j]; lru_list[j] = t; }

	for(u32 x = 0; x < to_evict && x < lru_list.size(); x++)
	{
		if(lru_list[x].type == 1)
		{
			u32 id = lru_list[x].id;
			if(id < cgfx_stat.obj_pixel_data.size()) { cgfx_stat.obj_pixel_data[id].clear(); }
			if(id < cgfx_stat.obj_loaded.size()) { cgfx_stat.obj_loaded[id] = false; }
		}
		else
		{
			u32 id = lru_list[x].id;
			if(id < cgfx_stat.bg_pixel_data.size()) { cgfx_stat.bg_pixel_data[id].clear(); }
			if(id < cgfx_stat.bg_loaded.size()) { cgfx_stat.bg_loaded[id] = false; }
		}
	}
}

/****** Search for an existing hash from the manifest ******/
bool AGB_LCD::has_hash(std::string hash)
{
	if(hash.length() < 5) { return false; }

	if(cgfx_stat.m_hashes.find(hash) != cgfx_stat.m_hashes.end())
	{
		cgfx_stat.last_id = cgfx_stat.m_hashes[hash];
		return true;
	}

	std::string sub_hash = hash.substr(4);
	if(cgfx_stat.m_hashes_raw.find(sub_hash) != cgfx_stat.m_hashes_raw.end())
	{
		cgfx_stat.last_id = cgfx_stat.m_hashes_raw[sub_hash];
		return true;
	}

	return false;
}

/****** Computes hash for a GBA BG tile at a given VRAM address ******/
std::string AGB_LCD::get_bg_hash(u32 tile_addr, u8 depth, u8 palette_id)
{
	std::string final_hash = "";
	u8 byte_count = (depth == 8) ? 64 : 32;

	for(int x = 0; x < byte_count; x += 2)
	{
		u16 temp = mem->read_u16_fast(tile_addr + x);
		final_hash += hash::raw_to_64(temp);
	}

	//Prepend palette identifier
	std::string pal_str = util::to_str(palette_id);
	while(pal_str.length() < 2) { pal_str = "0" + pal_str; }
	final_hash = pal_str + "_" + final_hash;

	return final_hash;
}

/****** Computes hash for a GBA OBJ tile ******/
std::string AGB_LCD::get_obj_hash(u32 tile_addr, u8 depth, u8 palette_id, u8 shape, u8 size)
{
	std::string final_hash = "";

	u32 total_bytes = (depth == 8) ? 64 : 32;
	if(shape == 1) { total_bytes *= 2; }  // Horizontal
	else if(shape == 2) { total_bytes *= 2; } // Vertical, handled by 2x loop below

	for(int x = 0; x < total_bytes; x += 2)
	{
		u16 temp = mem->read_u16_fast(tile_addr + x);
		final_hash += hash::raw_to_64(temp);
	}

	std::string pal_str = util::to_str(palette_id);
	while(pal_str.length() < 2) { pal_str = "0" + pal_str; }
	final_hash = pal_str + "_" + final_hash;

	return final_hash;
}

/****** Invalidates all current hashes ******/
void AGB_LCD::invalidate_cgfx()
{
	for(int b = 0; b < 4; b++)
	{
		cgfx_stat.current_bg_hash[b].clear();
		cgfx_stat.current_bg_hash[b].resize(1024, "");
		cgfx_stat.bg_update_list[b].clear();
		cgfx_stat.bg_update_list[b].resize(1024, true);
	}

	cgfx_stat.current_obj_hash.clear();
	cgfx_stat.current_obj_hash.resize(128, "");

	cgfx_stat.update_bg = true;
	cgfx_stat.cgfx_current_frame = 0;
	cgfx_stat.pending_loads.clear();
	if(cgfx_stat.cgfx_max_cached == 0) { cgfx_stat.cgfx_max_cached = 512; }
}

/****** Renders CGFX BG pixel for Mode 0 ******/
void AGB_LCD::render_cgfx_bg_pixel(u16 bg_tile_id, u8 raw_color)
{
	//Safety check
	if(bg_tile_id >= cgfx_stat.bg_pixel_data.size() || cgfx_stat.bg_pixel_data[bg_tile_id].empty()) { return; }

	//Map raw_color (0-15 or 0-255) to pixel position in the CGFX tile
	//For a standard 8x8 tile at 1:1 scaling, raw_color 0 is pixel 0
	u32 cgfx_pixel_count = cgfx_stat.bg_pixel_data[bg_tile_id].size();
	u32 pixel_index = raw_color % cgfx_pixel_count;

	scanline_buffer[scanline_pixel_counter] = cgfx_stat.bg_pixel_data[bg_tile_id][pixel_index];
}

/****** Dumps GBA OBJ tile ******/
void AGB_LCD::dump_obj(int obj_index)
{
	//Called from Qt GUI via AGB_core::dump_obj
}

/****** Dumps GBA BG tile ******/
void AGB_LCD::dump_bg(int bg_index)
{
	//Called from Qt GUI via AGB_core::dump_bg
}
