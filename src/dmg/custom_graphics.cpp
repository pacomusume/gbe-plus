// GB Enhanced Copyright Daniel Baxter 2015
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : custom_gfx.cpp
// Date : July 23, 2015
// Description : Game Boy Enhanced custom graphics (DMG/GBC version)
//
// Handles dumping original BG and Sprite tiles
// Handles loading custom pixel data

#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>
#include <chrono>

#include "common/util.h"
#include "common/cgfx_common.h"
#include "lcd.h"
#include "common/config.h"
#include "midi_driver.h"
#include "custom_sound.h"

// Maximum number of images allowed in a single pack (sanity guard)
static const int MAX_PACK_IMAGES = 2048;

/****** Signal background loader to stop, wait up to 2s, then join or detach safely ******/
void GB_LCD::stop_image_loading()
{
	cgfx_stat.stop_loading.store(true);

	// Spin-wait up to 2 seconds for the thread to set thread_finished
	if (cgfx_stat.img_loader_thread.joinable())
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (!cgfx_stat.thread_finished.load() &&
			   std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		if (cgfx_stat.thread_finished.load())
		{
			cgfx_stat.img_loader_thread.join();
		}
		else
		{
			// Thread did not finish in time; detach to avoid blocking the UI thread.
			// The thread will safely exit on its own since stop_loading is set.
			cgfx_stat.img_loader_thread.detach();
		}
	}

	// Drain any surfaces the background thread already queued
	{
		std::lock_guard<std::mutex> lock(cgfx_stat.pending_imgs_mutex);
		while (!cgfx_stat.pending_imgs.empty())
		{
			pending_img_result& item = cgfx_stat.pending_imgs.front();
			for (SDL_Surface* s : item.imgs) SDL_FreeSurface(s);
			for (SDL_Surface* s : item.himgs) SDL_FreeSurface(s);
			cgfx_stat.pending_imgs.pop();
		}
	}
}

void GB_LCD::clear_manifest() {
	// Safely stop any running background loader and drain its queue
	stop_image_loading();

	cgfx_stat.packVersion = "";

	for (u16 i = 0; i < cgfx_stat.imgs.size(); i++)
	{
		for (u16 j = 0; j < cgfx_stat.imgs[i].size(); j++)
		{
			SDL_FreeSurface(cgfx_stat.imgs[i][j]);
		}
		cgfx_stat.imgs[i].clear();
	}
	cgfx_stat.imgs.clear();

	for (u16 i = 0; i < cgfx_stat.himgs.size(); i++)
	{
		for (u16 j = 0; j < cgfx_stat.himgs[i].size(); j++)
		{
			SDL_FreeSurface(cgfx_stat.himgs[i][j]);
		}
		cgfx_stat.himgs[i].clear();
	}
	cgfx_stat.himgs.clear();
	cgfx_stat.conds.clear();
	cgfx_stat.tiles.clear();
	for (u8 i = 0; i < 60; i++)
	{
		cgfx_stat.bgs[i].clear();
	}

	SDL_FreeSurface(cgfx_stat.brightnessMod);
	SDL_FreeSurface(cgfx_stat.alphaCpy);
	SDL_FreeSurface(cgfx_stat.tempStrip);
	cgfx_stat.brightnessMod = nullptr;
	cgfx_stat.alphaCpy = nullptr;
	cgfx_stat.tempStrip = nullptr;

	// Reset async counters
	cgfx_stat.stop_loading.store(false);
	cgfx_stat.loading_complete.store(true);
	cgfx_stat.thread_finished.store(true);
	cgfx_stat.imgs_loaded_count.store(0);
	cgfx_stat.imgs_total_count.store(0);
}

u8 GB_LCD::getOpType(std::string op)
{
	if (op == "==") return pack_condition::EQ;
	if (op == "!=") return pack_condition::NE;
	if (op == ">") return pack_condition::GT;
	if (op == "<") return pack_condition::LS;
	if (op == ">=") return pack_condition::GE;
	if (op == "<=") return pack_condition::LE;
}

void GB_LCD::processConditionNames(std::string names, std::vector<pack_cond_app>* apps)
{
	std::string strRest = names;
	std::string token;
	std::size_t pos;
	pack_cond_app app;
	while (strRest.length() > 0)
	{
		pos = strRest.find("&");
		if (pos != std::string::npos)
		{
			token = util::trimfnc(strRest.substr(0, pos));
			strRest = strRest.substr(pos + 1, std::string::npos);
		}
		else
		{
			token = util::trimfnc(strRest);
			strRest = "";
		}
		pos = token.find("!");
		app.negate = (pos == 0);
		if (app.negate) 
		{
			token = token.substr(pos + 1, std::string::npos);
		}
		for (u16 i = 0; i < cgfx_stat.conds.size(); i++) {
			if (cgfx_stat.conds[i].name == token) {
				app.condIdx = i;
				apps->push_back(app);
			}
		}
	}
}

/****** Loads the manifest file ******/
bool GB_LCD::load_manifest(std::string filename)
{
	clear_manifest();

	std::ifstream file(filename.c_str(), std::ios::in);
	std::string input_line = "";
	std::string tagName = "";
	cgfx_stat.frameCnt = 0;

	bool result = false;
	cgfx::scaling_factor = 1;

	// Tasks collected during parsing for background loading
	std::vector<img_file_task> img_file_tasks;
	std::vector<img_brightness_task> img_brightness_tasks;

	//add build-in condition
	pack_condition pc;
	pc.type = pack_condition::HMIRROR;
	pc.name = "hmirror";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::VMIRROR;
	pc.name = "vmirror";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::BGPRIORITY;
	pc.name = "bgpriority";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE0;
	pc.name = "sppalette0";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE1;
	pc.name = "sppalette1";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE2;
	pc.name = "sppalette2";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE3;
	pc.name = "sppalette3";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE4;
	pc.name = "sppalette4";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE5;
	pc.name = "sppalette5";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE6;
	pc.name = "sppalette6";
	cgfx_stat.conds.push_back(pc);
	pc.type = pack_condition::PALETTE7;
	pc.name = "sppalette7";
	cgfx_stat.conds.push_back(pc);

	if (file.is_open())
	{
		std::vector<u16> imgIdxOffset;
		//Cycle through whole file, line-by-line
		while (getline(file, input_line))
		{
			input_line = util::trimfnc(input_line);
			//get tag content
			std::size_t found1 = input_line.find("<");
			std::size_t found2 = input_line.find(">");
			std::size_t found3 = input_line.find("[");
			std::size_t found4 = input_line.find("]");
			if (found1 != std::string::npos && found2 != std::string::npos && (found1 == 0 || found3 == 0))
			{
				std::string condNames;
				if (found3 == 0 && found4 > found3) {
					condNames = input_line.substr(found3 + 1, found4 - found3 - 1);
				}
				else
				{
					condNames = "";
				}
				if (found2 > found1)
				{
					tagName = input_line.substr(found1 + 1, found2 - found1 - 1);
				}
				std::string strRest = input_line.substr(found2 + 1);
				std::size_t pos;
				if (tagName == "ver") cgfx_stat.packVersion = util::trimfnc(strRest);
				else if (tagName == "scale") cgfx::scaling_factor = stoi(util::trimfnc(strRest));
				else if (tagName == "img")
				{
					// Sanity check: guard against unbounded image counts
					if ((int)cgfx_stat.imgs.size() >= MAX_PACK_IMAGES)
					{
						std::cerr << "[HD Pack] Image count limit (" << MAX_PACK_IMAGES
							<< ") reached, skipping remaining images.\n";
					}
					else
					{
						img_file_task task;
						task.imgIdx = (u16)cgfx_stat.imgs.size();
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							task.filename1 = util::trimfnc(strRest.substr(0, pos));
							task.filename2 = util::trimfnc(strRest.substr(pos + 1, std::string::npos));
						}
						else
						{
							task.filename1 = util::trimfnc(strRest);
						}
						imgIdxOffset.push_back(cgfx_stat.imgs.size());
						cgfx_stat.imgs.push_back({});  // empty placeholder
						cgfx_stat.himgs.push_back({}); // empty placeholder
						img_file_tasks.push_back(task);
					}
				}
				else if (tagName == "tile")
				{
					pack_tile pt;
					std::string token;
					u8 tokenCnt = 0;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
						{
							u16 parsedIdx = (u16)stoi(token);
							pt.imgIdx = (parsedIdx < (u16)imgIdxOffset.size())
								? imgIdxOffset[parsedIdx]
								: (u16)0xFFFF;
							break;
						}
						case 1:
							pt.tileStr = token;
							for (u16 i = 0; i < 8; i++)
							{
								pt.tileData.line[i] = std::stoul(token.substr(i * 4, 4), nullptr, 16);
							}
							break;
						case 2:
							pt.palStr = token;
							if (token.length() == 2)
							{
								pt.palette[0] = std::stoul(token, nullptr, 16);
							}
							else
							{
								for (u16 i = 0; i < 4; i++)
								{
									pt.palette[i] = std::stoul(token.substr(i * 4, 4), nullptr, 16);
								}
							}
							break;
						case 3:
							pt.x = stoi(token);
							break;
						case 4:
							pt.y = stoi(token);
							break;
						case 5:
							pt.brightness = stof(token);
							break;
						case 6:
							pt.isDefault = (token == "Y");
							break;

						default:
							break;
						}

						tokenCnt++;
					}
					if (condNames != "") {
						processConditionNames(condNames, &(pt.condApps));
					}
					cgfx_stat.tiles.push_back(pt);
				}
				else if (tagName == "condition")
				{
					std::string token;
					u8 tokenCnt = 0;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							pc.name = token;
							break;
						case 1:
							if (token == "tileNearby")
								pc.type = pack_condition::TILENEARBY;
							else if (token == "spriteNearby")
								pc.type = pack_condition::SPRITENEARBY;
							else if (token == "tileAtPosition")
								pc.type = pack_condition::TILEATPOSITION;
							else if (token == "spriteAtPosition")
								pc.type = pack_condition::SPRITEATPOSITION;
							else if (token == "memoryCheck")
								pc.type = pack_condition::MEMORYCHECK;
							else if (token == "memoryCheckConstant")
								pc.type = pack_condition::MEMORYCHECKCONSTANT;
							else if (token == "frameRange")
								pc.type = pack_condition::FRAMERANGE;
							else if (token == "bgPalValueMatch")
								pc.type = pack_condition::BGPALVAL;
							else if (token == "spPalValueMatch")
								pc.type = pack_condition::SPPALVAL;
							break;
						case 2:
							switch (pc.type)
							{
							case pack_condition::TILENEARBY:
							case pack_condition::SPRITENEARBY:
							case pack_condition::TILEATPOSITION:
							case pack_condition::SPRITEATPOSITION:
								pc.x = stoi(token);
								break;
							case pack_condition::MEMORYCHECK:
							case pack_condition::MEMORYCHECKCONSTANT:
								pc.address1 = std::stoul(token, nullptr, 16);
								pc.mask = 0xFF;
								break;
							case pack_condition::FRAMERANGE:
								pc.divisor = stoi(token);
								break;
							case pack_condition::BGPALVAL:
							case pack_condition::SPPALVAL:
								if (token.length() == 2)
								{
									pc.palette[0] = std::stoul(token, nullptr, 16);
								}
								else
								{
									for (u16 i = 0; i < 4; i++)
									{
										pc.palette[i] = std::stoul(token.substr(i * 4, 4), nullptr, 16);
									}
								}
								break;
							default:
								break;
							}
							break;
						case 3:
							switch (pc.type)
							{
							case pack_condition::TILENEARBY:
							case pack_condition::SPRITENEARBY:
							case pack_condition::TILEATPOSITION:
							case pack_condition::SPRITEATPOSITION:
								pc.y = stoi(token);
								break;
							case pack_condition::MEMORYCHECK:
							case pack_condition::MEMORYCHECKCONSTANT:
								pc.opType = getOpType(token);
								break;
							case pack_condition::FRAMERANGE:
								pc.compareVal = stoi(token);
								break;
							case pack_condition::BGPALVAL:
							case pack_condition::SPPALVAL:
								pc.palIdx = stoi(token);
								break;
							default:
								break;
							}
							break;
						case 4:
							switch (pc.type)
							{
							case pack_condition::TILENEARBY:
							case pack_condition::SPRITENEARBY:
							case pack_condition::TILEATPOSITION:
							case pack_condition::SPRITEATPOSITION:
								for (u16 i = 0; i < 8; i++)
								{
									pc.tileData.line[i] = std::stoul(token.substr(i * 4, 4), nullptr, 16);
								}
								break;
							case pack_condition::MEMORYCHECK:
								pc.address2 = std::stoul(token, nullptr, 16);
								break;
							case pack_condition::MEMORYCHECKCONSTANT:
								pc.value = std::stoul(token, nullptr, 16);
								break;
							default:
								break;
							}
							break;
						case 5:
							switch (pc.type)
							{
							case pack_condition::TILENEARBY:
							case pack_condition::SPRITENEARBY:
							case pack_condition::TILEATPOSITION:
							case pack_condition::SPRITEATPOSITION:
								if (token.length() == 2)
								{
									pc.palette[0] = std::stoul(token, nullptr, 16);
								}
								else
								{
									for (u16 i = 0; i < 4; i++)
									{
										pc.palette[i] = std::stoul(token.substr(i * 4, 4), nullptr, 16);
									}
								}
								break;
							case pack_condition::MEMORYCHECK:
							case pack_condition::MEMORYCHECKCONSTANT:
								pc.mask = std::stoul(token, nullptr, 16);
								break;
							default:
								break;
							}
							break;
						default:
							break;
						}

						tokenCnt++;
					}
					cgfx_stat.conds.push_back(pc);


				}
				else if (tagName == "background")
				{
					pack_background bg;
					bg.brightness = 1;
					bg.hscroll = 0;
					bg.vscroll = 0;
					bg.priority = 10;
					bg.offsetX = 0;
					bg.offsetY = 0;

					std::string token;
					u8 tokenCnt = 0;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
						{
							u16 parsedIdx = (u16)stoi(token);
							bg.imgIdx = (parsedIdx < (u16)imgIdxOffset.size())
								? (s16)imgIdxOffset[parsedIdx]
								: (s16)-1;
							break;
						}
						case 1:
							bg.brightness = stof(token);
							break;
						case 2:
							bg.hscroll = stof(token);
							break;
						case 3:
							bg.vscroll = stof(token);
							break;
						case 4:
							bg.priority = stoi(token);
							break;
						case 5:
							bg.offsetX = stoi(token);
							break;
						case 6:
							bg.offsetY = stoi(token);
							break;

						default:
							break;
						}

						tokenCnt++;
					}
					if (condNames != "") 
					{
						processConditionNames(condNames, &(bg.condApps));
					}
					if (bg.brightness != 1)
					{
						// Defer brightness modification to background thread.
						// Pre-allocate a new slot for the modified image.
						u16 srcIdx = bg.imgIdx;
						u16 dstIdx = (u16)cgfx_stat.imgs.size();
						cgfx_stat.imgs.push_back({});  // empty placeholder
						cgfx_stat.himgs.push_back({}); // empty placeholder
						bg.imgIdx = dstIdx;
						img_brightness_task bt;
						bt.srcImgIdx = srcIdx;
						bt.dstImgIdx = dstIdx;
						bt.brightness = bg.brightness;
						img_brightness_tasks.push_back(bt);
					}
					cgfx_stat.bgs[bg.priority].push_back(bg);
				}
				else if (tagName == "midi") 
				{
					std::string token;
					u8 tokenCnt = 0;
					u8 sq;
					u8 duty;
					u8 instID;
					bool useHarmonic;
					u8 vol = 100;

					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							sq = stoi(token);
							break;
						case 1:
							duty = stoi(token);
							break;
						case 2:
							instID = stoi(token);
							break;
						case 3:
							useHarmonic = (token == "Y" || token == "y");
							break;
						case 4:
							vol = stoi(token);
							break;
						default:
							break;
						}

						tokenCnt++;
					}
					dmg_midi_driver::midi->addReplacement(sq, duty, instID, useHarmonic, vol);
				}
				else if (tagName == "noise")
				{
					std::string token;
					u8 tokenCnt = 0;
					u8 nr43v;
					u8 instID;
					u8 vol = 100;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							nr43v = std::stoul(token, nullptr, 16);
							break;
						case 1:
							instID = stoi(token);
							break;
						case 2:
							vol = stoi(token);
							break;
						default:
							break;
						}

						tokenCnt++;
					}
					dmg_midi_driver::midi->addNoiseReplacement(nr43v, instID, vol);
				}
				else if (tagName == "wave")
				{
					std::string token;
					u8 tokenCnt = 0;
					u8 waveForm[16];
					u8 instID;
					u8 vol = 100;
					bool useHarmonic;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							for(u8 i = 0; i < 16; i++)
								waveForm[i] = std::stoul(token.substr(i * 2, 2), nullptr, 16);
							break;
						case 1:
							instID = stoi(token);
							break;
						case 2:
							useHarmonic = (token == "Y" || token == "y");
							break;
						case 3:
							vol = stoi(token);
							break;
						default:
							break;
						}

						tokenCnt++;
					}
					dmg_midi_driver::midi->addWaveReplacement(waveForm, instID, useHarmonic, vol);
				}
				else if (tagName == "writeaddress")
				{
					std::string token;
					u8 tokenCnt = 0;
					u16 address = 0;
					u8 function = 0;
					u8 type = 0;
					u8 value = 0;
					u8 valueMask = 0xFF;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							address = std::stoul(token, nullptr, 16);
							break;
						case 1:
							function = stoi(token);
							break;
						case 2:
							type = stoi(token);
							break;
						case 3:
							value = std::stoul(token, nullptr, 16);
							break;
						case 4:
							valueMask = std::stoul(token, nullptr, 16);
							break;
						default:
							break;
						}
						tokenCnt++;
					}
					dmg_custom_sound::soundex->addWAddress(address, function, type, value, valueMask);
				}
				else if (tagName == "readaddress")
				{
					std::string token;
					u8 tokenCnt = 0;
					u16 address = 0;
					u8 function = 0;
					u8 type = 0;
					u8 value = 0;
					u8 valueMask = 0xFF;
					u32 loopPoint = 0;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							address = std::stoul(token, nullptr, 16);
							break;
						case 1:
							function = stoi(token);
							break;
						case 2:
							type = stoi(token);
							break;
						case 3:
							value = std::stoul(token, nullptr, 16);
							break;
						case 4:
							valueMask = std::stoul(token, nullptr, 16);
							break;
						default:
							break;
						}
						tokenCnt++;
					}
					dmg_custom_sound::soundex->addRAddress(address, function, type, value, valueMask);
				}
				else if (tagName == "bgm")
				{
					std::string token;
					u8 tokenCnt = 0;
					u8 id;
					std::string musicFileName;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							id = std::stoul(token, nullptr, 16);
							break;
						case 1:
							musicFileName = token;
							break;
						}
						tokenCnt++;
					}
					dmg_custom_sound::soundex->addBgm(id, get_game_cgfx_folder() + musicFileName);
				}
				else if (tagName == "sfx")
				{
					std::string token;
					u8 tokenCnt = 0;
					u8 id;
					std::string soundFileName;
					while (strRest.length() > 0)
					{
						pos = strRest.find(",");
						if (pos != std::string::npos)
						{
							token = util::trimfnc(strRest.substr(0, pos));
							strRest = strRest.substr(pos + 1, std::string::npos);
						}
						else
						{
							token = util::trimfnc(strRest);
							strRest = "";
						}

						switch (tokenCnt)
						{
						case 0:
							id = std::stoul(token, nullptr, 16);
							break;
						case 1:
							soundFileName = token;
							break;
						}
						tokenCnt++;
					}
					dmg_custom_sound::soundex->addSfx(id, get_game_cgfx_folder() + soundFileName);
				}
			}
		}
		file.close();
		result = true;
	}

	//Initialize HD buffer for CGFX greater that 1:1
	config::sys_width *= cgfx::scaling_factor;
	config::sys_height *= cgfx::scaling_factor;

	reset_buffers();

	cgfx_stat.brightnessMod = SDL_CreateRGBSurfaceWithFormat(0, cgfx::scaling_factor * 8, cgfx::scaling_factor, 32, SDL_PIXELFORMAT_ARGB8888);
	cgfx_stat.alphaCpy = SDL_CreateRGBSurfaceWithFormat(0, cgfx::scaling_factor * 8, cgfx::scaling_factor, 32, SDL_PIXELFORMAT_ARGB8888);
	cgfx_stat.tempStrip = SDL_CreateRGBSurfaceWithFormat(0, cgfx::scaling_factor * 8, cgfx::scaling_factor, 32, SDL_PIXELFORMAT_ARGB8888);

	srcrect.w = cgfx::scaling_factor;
	srcrect.h = cgfx::scaling_factor;
	hdrect.w = cgfx::scaling_factor * 8;
	hdrect.h = cgfx::scaling_factor;
	hdsrcrect = hdrect;

	// Launch background thread to load images (avoids blocking main thread)
	int total_tasks = (int)(img_file_tasks.size() + img_brightness_tasks.size());
	cgfx_stat.imgs_total_count.store(total_tasks);
	cgfx_stat.imgs_loaded_count.store(0);
	cgfx_stat.loading_complete.store(total_tasks == 0);

	if (total_tasks > 0)
	{
		std::cout << "[HD Pack] Starting async load: " << img_file_tasks.size()
			<< " image file(s), " << img_brightness_tasks.size()
			<< " brightness mod(s).\n";
		std::string folder = get_game_cgfx_folder();
		cgfx_stat.thread_finished.store(false);
		cgfx_stat.img_loader_thread = std::thread(
			&GB_LCD::load_images_background, this,
			folder, img_file_tasks, img_brightness_tasks);
	}

	return result;
}

SDL_Surface* GB_LCD::load_pack_image(std::string filename)
{
	std::string path = get_game_cgfx_folder() + filename;
	SDL_Surface* s = IMG_Load(path.c_str());
	SDL_Surface* r = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(s);
	return r;
}

SDL_Surface* GB_LCD::h_flip_image(SDL_Surface* s)
{
	SDL_Surface* r = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_LockSurface(s);
	SDL_LockSurface(r);
	u32* srcP = (u32*)(s->pixels);
	u32* dstP = (u32*)(r->pixels) + s->w;
	for (u16 y = 0; y < s->h; y++) {
		for (u16 x = 0; x < s->w; x++) {
			dstP--;
			*dstP = *srcP;
			srcP++;
		}
		dstP += s->w * 2;
	}
	SDL_UnlockSurface(r);
	SDL_UnlockSurface(s);

	return r;
}

/****** Apply brightness modification to a surface, return modified copy ******/
static SDL_Surface* apply_brightness_mod(SDL_Surface* src, float brightness)
{
	if (!src) return nullptr;

	u8 c;
	u32 brightnessC;
	SDL_Surface* brightnessMod = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface* alphaCpy = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface* img = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_ARGB8888);

	if (!brightnessMod || !alphaCpy || !img)
	{
		SDL_FreeSurface(brightnessMod);
		SDL_FreeSurface(alphaCpy);
		SDL_FreeSurface(img);
		return nullptr;
	}

	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	//make a copy of the alpha values with all pixels white
	SDL_BlitSurface(src, NULL, alphaCpy, NULL);
	SDL_FillRect(brightnessMod, NULL, 0xFFFFFFFF);
	SDL_SetSurfaceBlendMode(brightnessMod, SDL_BLENDMODE_ADD);
	SDL_BlitSurface(brightnessMod, NULL, alphaCpy, NULL);

	//create pixel values
	if (brightness < 1.0f)
	{
		//create a darken copy
		c = (u8)(255 * (1.0f - brightness));
		brightnessC = 0x00000000u | ((u32)c << 24);
	}
	else
	{
		//create a brighten copy
		c = (u8)(255 * (brightness - 1.0f));
		brightnessC = 0x00FFFFFFu | ((u32)c << 24);
	}
	SDL_FillRect(brightnessMod, NULL, brightnessC);
	SDL_SetSurfaceBlendMode(brightnessMod, SDL_BLENDMODE_BLEND);
	SDL_BlitSurface(src, NULL, img, NULL);
	SDL_BlitSurface(brightnessMod, NULL, img, NULL);

	//merge pixel values to alpha
	SDL_SetSurfaceBlendMode(img, SDL_BLENDMODE_MOD);
	SDL_BlitSurface(img, NULL, alphaCpy, NULL);

	//restore blend mode
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
	SDL_FreeSurface(img);
	SDL_FreeSurface(brightnessMod);

	return alphaCpy; // caller owns this surface
}

/****** Install any images loaded by the background thread (call from main thread each frame) ******/
void GB_LCD::process_pending_imgs()
{
	if (cgfx_stat.pending_imgs.empty()) return;

	std::lock_guard<std::mutex> lock(cgfx_stat.pending_imgs_mutex);

	// Process up to a bounded number per frame to avoid stalling
	const int max_per_frame = 16;
	int processed = 0;

	while (!cgfx_stat.pending_imgs.empty() && processed < max_per_frame)
	{
		pending_img_result& item = cgfx_stat.pending_imgs.front();

		if (item.imgIdx < (u16)cgfx_stat.imgs.size())
		{
			// Slots are always empty placeholders; just install the loaded surfaces
			cgfx_stat.imgs[item.imgIdx] = std::move(item.imgs);
			cgfx_stat.himgs[item.imgIdx] = std::move(item.himgs);
		}
		else
		{
			// Index out of range: discard to avoid memory leak
			for (SDL_Surface* s : item.imgs) SDL_FreeSurface(s);
			for (SDL_Surface* s : item.himgs) SDL_FreeSurface(s);
		}

		cgfx_stat.pending_imgs.pop();
		processed++;
	}
}

/****** Background thread: loads image files and processes brightness mods ******/
void GB_LCD::load_images_background(
	std::string folder,
	std::vector<img_file_task> file_tasks,
	std::vector<img_brightness_task> bright_tasks)
{
	// Return early if there is nothing to do
	if (file_tasks.empty() && bright_tasks.empty())
	{
		cgfx_stat.loading_complete.store(true);
		cgfx_stat.thread_finished.store(true);
		return;
	}

	// Determine max index across all tasks
	size_t max_idx = 0;
	for (auto& t : file_tasks) if (t.imgIdx > max_idx) max_idx = t.imgIdx;
	for (auto& t : bright_tasks) if (t.dstImgIdx > max_idx) max_idx = t.dstImgIdx;

	// Local staging: indices are always sequential (0..max_idx) since they are
	// assigned as cgfx_stat.imgs.size() incremented by 1 per image during parsing.
	std::vector<std::vector<SDL_Surface*>> staging_imgs(max_idx + 1);
	std::vector<std::vector<SDL_Surface*>> staging_himgs(max_idx + 1);

	bool stopped = false;

	// Stage 1: Load image files from disk
	for (auto& task : file_tasks)
	{
		if (cgfx_stat.stop_loading.load()) { stopped = true; break; }

		std::string path1 = folder + task.filename1;
		SDL_Surface* raw = IMG_Load(path1.c_str());
		if (!raw)
		{
			std::cerr << "[HD Pack] Failed to load: " << path1 << "\n";
			cgfx_stat.imgs_loaded_count.fetch_add(1);
			continue;
		}
		SDL_Surface* img = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
		SDL_FreeSurface(raw);
		if (!img)
		{
			std::cerr << "[HD Pack] Format conversion failed: " << path1 << "\n";
			cgfx_stat.imgs_loaded_count.fetch_add(1);
			continue;
		}

		staging_imgs[task.imgIdx].push_back(img);
		staging_himgs[task.imgIdx].push_back(h_flip_image(img));

		// Load optional second image (two-surface <img> entries)
		if (!task.filename2.empty())
		{
			std::string path2 = folder + task.filename2;
			raw = IMG_Load(path2.c_str());
			if (!raw)
			{
				std::cerr << "[HD Pack] Failed to load: " << path2 << "\n";
			}
			else
			{
				SDL_Surface* img2 = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
				SDL_FreeSurface(raw);
				if (img2)
				{
					staging_imgs[task.imgIdx].push_back(img2);
					staging_himgs[task.imgIdx].push_back(h_flip_image(img2));
				}
			}
		}

		cgfx_stat.imgs_loaded_count.fetch_add(1);
	}

	// Stage 2: Apply brightness modifications using already-loaded images
	if (!stopped)
	{
		for (auto& task : bright_tasks)
		{
			if (cgfx_stat.stop_loading.load()) { stopped = true; break; }

			if (task.srcImgIdx >= (u16)staging_imgs.size() || staging_imgs[task.srcImgIdx].empty())
			{
				std::cerr << "[HD Pack] Brightness mod skipped: source index "
					<< task.srcImgIdx << " not loaded.\n";
				cgfx_stat.imgs_loaded_count.fetch_add(1);
				continue;
			}

			SDL_Surface* modded = apply_brightness_mod(staging_imgs[task.srcImgIdx][0], task.brightness);
			if (modded)
			{
				staging_imgs[task.dstImgIdx].push_back(modded);
				staging_himgs[task.dstImgIdx].push_back(h_flip_image(modded));
			}

			cgfx_stat.imgs_loaded_count.fetch_add(1);
		}
	}

	// Stage 3: Push all results to shared queue for main thread to install
	if (!stopped)
	{
		std::lock_guard<std::mutex> lock(cgfx_stat.pending_imgs_mutex);
		for (size_t i = 0; i <= max_idx; i++)
		{
			if (!staging_imgs[i].empty())
			{
				pending_img_result r;
				r.imgIdx = (u16)i;
				r.imgs = std::move(staging_imgs[i]);
				r.himgs = std::move(staging_himgs[i]);
				cgfx_stat.pending_imgs.push(std::move(r));
			}
		}
		cgfx_stat.loading_complete.store(true);
		std::cout << "[HD Pack] Async load complete: "
			<< cgfx_stat.imgs_loaded_count.load() << "/"
			<< cgfx_stat.imgs_total_count.load() << " item(s) loaded.\n";
	}

	// Cleanup: free staging surfaces only if we stopped early (Stage 3 didn't run).
	// When !stopped, all non-empty vectors were moved into the queue above.
	if (stopped)
	{
		for (auto& v : staging_imgs) for (SDL_Surface* s : v) SDL_FreeSurface(s);
		for (auto& v : staging_himgs) for (SDL_Surface* s : v) SDL_FreeSurface(s);
	}

	// Signal that this thread is done regardless of whether we stopped early
	cgfx_stat.thread_finished.store(true);
}

void GB_LCD::new_rendered_screen_data() {

	//remove old, set new to old, update list id in vram list
	u16 i = 0;
	while (i < cgfx_stat.screen_data.rendered_tile.size())
	{
		if (cgfx_stat.screen_data.rendered_tile[i].isOld)
		{
			cgfx_stat.vram_tile_used[cgfx_stat.screen_data.rendered_tile[i].address] = 0;
			cgfx_stat.vram_tile_idx[cgfx_stat.screen_data.rendered_tile[i].address] = 0xFFFF;
			cgfx_stat.screen_data.rendered_tile.erase(cgfx_stat.screen_data.rendered_tile.begin() + i);
		}
		else
		{
			cgfx_stat.screen_data.rendered_tile[i].isOld = true;
			cgfx_stat.vram_tile_idx[cgfx_stat.screen_data.rendered_tile[i].address] = i;
			i++;
		}
	}

	cgfx_stat.screen_data.rendered_palette.clear();
	cgfx_stat.frameCnt++;
}

void GB_LCD::new_rendered_scanline_data(u8 lineNo)
{
	cgfx_stat.screen_data.scanline[lineNo].lcdc = mem->read_u8(REG_LCDC);
	cgfx_stat.screen_data.scanline[lineNo].rendered_bg.clear();
	cgfx_stat.screen_data.scanline[lineNo].rendered_win.clear();
	cgfx_stat.screen_data.scanline[lineNo].rendered_obj.clear();
}