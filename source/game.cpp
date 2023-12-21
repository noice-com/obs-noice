// Copyright (C) 2023 Noice Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "game.hpp"
#include "common.hpp"
#include <math.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <obs-module.h>

#define VERBOSE_DEBUG 0

static noice::box_tuple convert_box(noice::box_tuple box, noice::box_format in_fmt, noice::box_format out_fmt)
{
	float a1, a2, a3, a4;
	std::tie(a1, a2, a3, a4) = box;
	float x1, y1, x2, y2;
	float w, h, cx, cy;
	noice::box_tuple result;

	if (in_fmt == noice::XYXY) {
		x1 = a1;
		y1 = a2;
		x2 = a3;
		y2 = a4;
		w = x2 - x1;
		h = y2 - y1;
		cx = (x1 + x2) * 0.5f;
		cy = (y1 + y2) * 0.5f;
	} else if (in_fmt == noice::XYWH) {
		x1 = a1;
		y1 = a2;
		w = a3;
		h = a4;
		x2 = x1 + w;
		y2 = y1 + h;
		cx = (x1 + x2) * 0.5f;
		cy = (y1 + y2) * 0.5f;
	} else if (in_fmt == noice::CXCYWH) {
		cx = a1;
		cy = a2;
		w = a3;
		h = a4;
		x1 = cx - w * 0.5f;
		y1 = cy - h * 0.5f;
		x2 = cx + w * 0.5f;
		y2 = cy + h * 0.5f;
	} else
		throw std::invalid_argument("in_fmt");

	if (out_fmt == noice::XYXY)
		result = std::make_tuple(x1, y1, x2, y2);
	else if (out_fmt == noice::XYWH)
		result = std::make_tuple(x1, y1, w, h);
	else if (out_fmt == noice::CXCYWH)
		result = std::make_tuple(cx, cy, w, h);
	else
		throw std::invalid_argument("out_fmt");

	return result;
}

static float align_1d(noice::anchor alignment, float img_size, float ref_size, float ref_point, float scale, float norm_offset,
		      noice::align_axis axis)
{
	float img_mid = img_size * 0.5f;
	float ref_mid = ref_size * 0.5f;
	float scaled_offset = img_size * norm_offset;
	std::vector<noice::anchor> group1, group2, group3;

	if (axis == noice::X) {
		group1 = {noice::anchor::TOP_LEFT, noice::anchor::MIDDLE_LEFT, noice::anchor::BOTTOM_LEFT, noice::anchor::LEFT};
		group2 = {noice::anchor::TOP_RIGHT, noice::anchor::MIDDLE_RIGHT, noice::anchor::BOTTOM_RIGHT, noice::anchor::RIGHT};
		group3 = {noice::anchor::TOP_MIDDLE, noice::anchor::CENTER, noice::anchor::BOTTOM_MIDDLE, noice::anchor::MIDDLE_X};
	} else if (axis == noice::Y) {
		group1 = {noice::anchor::TOP_LEFT, noice::anchor::TOP_MIDDLE, noice::anchor::TOP_RIGHT, noice::anchor::TOP};
		group2 = {noice::anchor::BOTTOM_LEFT, noice::anchor::BOTTOM_MIDDLE, noice::anchor::BOTTOM_RIGHT, noice::anchor::BOTTOM};
		group3 = {noice::anchor::MIDDLE_LEFT, noice::anchor::CENTER, noice::anchor::MIDDLE_RIGHT, noice::anchor::MIDDLE_Y};
	} else
		throw std::invalid_argument("axis");

	if (std::find(group1.begin(), group1.end(), alignment) != group1.end()) {
		return ref_point * scale + scaled_offset;
	} else if (std::find(group2.begin(), group2.end(), alignment) != group2.end()) {
		float dist_from_edge = (ref_size - ref_point) * scale;
		return img_size - dist_from_edge - scaled_offset;
	} else if (std::find(group3.begin(), group3.end(), alignment) != group3.end()) {
		float dist_from_mid = (ref_mid - ref_point) * scale;
		float p = img_mid - dist_from_mid;

		// Check offset sign. And don't add offset unless it brings element closer to center.
		if (p < img_mid && abs(p + scaled_offset - img_mid) < abs(p - img_mid))
			p += scaled_offset;
		else if (p > img_mid && abs(p - scaled_offset - img_mid) < abs(p - img_mid))
			p -= scaled_offset;
		return p;
	} else
		return ref_point;
}

void noice::region::align_box(struct obs_video_info ovi, float hud_scale)
{
	float img_w = (float)ovi.base_width, img_h = (float)ovi.base_height;
	float refimg_w = (float)base->width, refimg_h = (float)base->height;

	float scale = fmin(img_w / refimg_w, img_h / refimg_h);
	if (hud_scale_locked == false)
		scale *= hud_scale;

	box_tuple in_box = std::make_tuple(rect.x, rect.y, rect.w, rect.h);

	float refbox_cx, refbox_cy, refbox_w, refbox_h;
	std::tie(refbox_cx, refbox_cy, refbox_w, refbox_h) = convert_box(in_box, noice::XYWH, noice::CXCYWH);

	float _w = (refbox_w * scale);
	float _h = (refbox_h * scale);

	// TODO?
	float xoffset = 0.0f, yoffset = 0.0f;

	float _cx = align_1d(alignment, img_w, refimg_w, refbox_cx, scale, xoffset, noice::X);
	float _cy = align_1d(alignment, img_h, refimg_h, refbox_cy, scale, yoffset, noice::Y);

	std::tie(box.x, box.y, box.w, box.h) = convert_box(std::make_tuple(_cx, _cy, _w, _h), noice::CXCYWH, noice::XYWH);
#if VERBOSE_DEBUG
	DLOG_INFO("region: game_state: %s region: %s x: %.3f y: %.3f w: %.3f h: %.3f", game_state.c_str(), region_name.c_str(), box.x,
		  box.y, box.w, box.h);
#endif
}

float noice::in_game_hud_scale::clamp_value()
{
	float ret = std::clamp(value, min, max);
	if (fmodf(ret, step) >= (step / 2))
		ret = ret + (step - fmodf(ret, step));
	else
		ret = ret - fmodf(ret, step);
	return ret;
}

noice::anchor_map::anchor_map()
{
	this->operator[]("top_left") = TOP_LEFT;
	this->operator[]("top_middle") = TOP_MIDDLE;
	this->operator[]("top_right") = TOP_RIGHT;
	this->operator[]("middle_left") = MIDDLE_LEFT;
	this->operator[]("center") = CENTER;
	this->operator[]("middle_right") = MIDDLE_RIGHT;
	this->operator[]("bottom_left") = BOTTOM_LEFT;
	this->operator[]("bottom_middle") = BOTTOM_MIDDLE;
	this->operator[]("bottom_right") = BOTTOM_RIGHT;
	this->operator[]("left") = LEFT;
	this->operator[]("middle_x") = MIDDLE_X;
	this->operator[]("right") = RIGHT;
	this->operator[]("top") = TOP;
	this->operator[]("middle_y") = MIDDLE_Y;
	this->operator[]("bottom") = BOTTOM;
};

noice::game::~game() {}

noice::game::game() : current_resolution(nullptr), reset_regions(true), disabled(false) {}

noice::game_manager::~game_manager() {}

noice::game_manager::game_manager() {}

std::vector<std::string> noice::game_manager::get_games()
{
	std::unique_lock<std::mutex> lock(_lock);
	return _games;
}

std::shared_ptr<noice::game> noice::game_manager::get_game(std::string name)
{
	std::unique_lock<std::mutex> lock(_lock);

	auto search = _game_map.find(name);
	if (search != _game_map.end()) {
		return search->second;
	}
	return nullptr;
}

bool noice::game_manager::is_game_acquired(std::string name, std::string instance)
{
	return is_game_acquired(get_game(name), instance);
}

bool noice::game_manager::is_game_acquired(std::shared_ptr<noice::game> game, std::string instance)
{
	if (game == nullptr || game->disabled == true || game->name.empty())
		return false;

	std::unique_lock<std::mutex> lock(_lock_active);
	auto search = _game_active.find(game->name);
	if (search != _game_active.end()) {
		if (!instance.empty() && search->second == instance) {
			return false;
		}
		return true;
	}
	return false;
}

void noice::game_manager::acquire_game(std::shared_ptr<noice::game> game, std::string instance)
{
	if (game == nullptr || game->disabled == true || game->name.empty())
		return;

	std::unique_lock<std::mutex> lock(_lock_active);
	auto search = _game_active.find(game->name);
	if (search == _game_active.end()) {
		_game_active[game->name] = instance;
	}
}

void noice::game_manager::release_game(std::shared_ptr<noice::game> game, std::string instance)
{
	if (game == nullptr || game->disabled == true || game->name.empty())
		return;

	std::unique_lock<std::mutex> lock(_lock_active);
	auto search = _game_active.find(game->name);
	if (search != _game_active.end() && search->second == instance) {
		_game_active.erase(search);
	}
}

static void parse_resolution(std::string input, noice::video_resolution &res)
{
	size_t x_index = input.find("x");
	if (x_index == std::string::npos)
		return;
	res.width = std::stoi(input.substr(0, x_index));
	res.height = std::stoi(input.substr(x_index + 1, input.length()));
}

void noice::game_manager::refresh()
{
	std::unique_lock<std::mutex> lock(_lock);
	const char *conf = noice::deployment_config_path("regions.json");
	std::ifstream regions_json(conf, std::ios::in);
	refresh_main(regions_json);
	regions_json.close();
	bfree((void *)conf);
}

bool noice::game_manager::refresh_main(std::istream &input)
{
	try {
		nlohmann::json data;
		data = nlohmann::json::parse(input);

		nlohmann::json games_arr = data["games"];
		if (!games_arr.is_array()) {
			DLOG_ERROR("JSON response is malformed, no games listed");
			return false;
		}

		_games.clear();
		_game_map.clear();

		std::string name_suffix = "";
		auto cfg = noice::configuration::instance();
		// TODO: Could use cfg->noice_service_selected() to hilight when service is inactive though source labels, but..
		if (!noice::is_production())
			name_suffix = noice::string_format(" (%s)", cfg->deployment().c_str());

		// Ensure a placeholder game always exists to make life easier
		{
			_games.push_back(NOICE_PLACEHOLDER_GAME_NAME);

			std::shared_ptr<noice::game> game_entry = std::make_shared<noice::game>();
			game_entry->name = NOICE_PLACEHOLDER_GAME_NAME;
			game_entry->name_verbose = obs_module_text("Noice.NoGameSelected");
			game_entry->disabled = true;

			std::shared_ptr<video_resolution> res = std::make_shared<video_resolution>();
			if (game_entry->current_resolution.get() == nullptr)
				game_entry->current_resolution = res;

			std::shared_ptr<std::vector<noice::region>> regions_vec = std::make_shared<std::vector<noice::region>>();
			game_entry->resolutions.push_back(res);
			game_entry->map[res] = regions_vec;

			_game_map[game_entry->name] = game_entry;
		}

		anchor_map map_to_enum;

		for (auto games_it : games_arr) {
			std::string game = games_it.get<std::string>();
#if VERBOSE_DEBUG
			DLOG_INFO("game: %s", game.c_str());
#endif
			_games.push_back(game);

			auto game_obj = data[game];
			if (!game_obj.is_object()) {
				DLOG_ERROR("JSON response is malformed, no game object");
				continue;
			}
			auto resolutions_obj = game_obj["resolutions"];
			if (!resolutions_obj.is_array()) {
				DLOG_ERROR("JSON response is malformed, no resolution array");
				continue;
			}

			std::shared_ptr<noice::game> game_entry = std::make_shared<noice::game>();
			game_entry->name = game;
			game_entry->name_verbose = game_obj["name_verbose"].get<std::string>();
			if (!name_suffix.empty())
				game_entry->name_verbose += name_suffix;

			noice::in_game_hud_scale &hud = game_entry->in_game_hud;
			std::vector<float> hud_scale_range = game_obj["hud_scale"].get<std::vector<float>>();
			hud.min = hud_scale_range.at(0);
			hud.max = hud_scale_range.at(1);
			hud.step = hud_scale_range.at(2);
#if VERBOSE_DEBUG
			DLOG_INFO("in_game_hud_scale: min: %f max: %f step: %f", hud.min, hud.max, hud.step);
#endif

			for (auto resolutions_it : resolutions_obj) {
				std::string resolution_str = resolutions_it.get<std::string>();
				std::shared_ptr<video_resolution> res = std::make_shared<video_resolution>();

				if (game_entry->current_resolution.get() == nullptr)
					game_entry->current_resolution = res;

				parse_resolution(resolution_str, *res.get());
#if VERBOSE_DEBUG
				DLOG_INFO("resolution: %s width: %d height: %d", resolution_str.c_str(), res->width, res->height);
#endif

				auto regions = game_obj[resolution_str];
				if (!regions.is_array()) {
					DLOG_ERROR("JSON response is malformed, no regions array");
					continue;
				}

				std::shared_ptr<std::vector<noice::region>> regions_vec = std::make_shared<std::vector<noice::region>>();

				for (auto regions_it : regions) {
					auto region_obj = regions_it.get<nlohmann::json::object_t>();
					std::string game_state = region_obj["game_state"].get<std::string>();
					std::string region_name = region_obj["region"].get<std::string>();
					std::string alignment_str = region_obj["alignment"].get<std::string>();
					anchor alignment = map_to_enum[alignment_str];

					bool hud_scale_locked = false;
					if (region_obj.find("hud_scale_locked") != region_obj.end())
						hud_scale_locked = region_obj["hud_scale_locked"].get<bool>();

					region_rect rect;
					rect.x = region_obj["x"].get<float>();
					rect.y = region_obj["y"].get<float>();
					rect.w = region_obj["w"].get<float>();
					rect.h = region_obj["h"].get<float>();

					noice::region region_entry(res, game_state, region_name, alignment, hud_scale_locked, rect);
					regions_vec->push_back(region_entry);
				}

				game_entry->resolutions.push_back(res);
				game_entry->map[res] = regions_vec;
			}
			_game_map[game] = game_entry;
		}
	} catch (std::exception const &ex) {
		DLOG_ERROR("JSON parse error: %s", ex.what());
		return false;
	}
	return true;
}

std::shared_ptr<noice::game_manager> noice::game_manager::_instance = nullptr;

void noice::game_manager::initialize()
{
	if (!noice::game_manager::_instance)
		noice::game_manager::_instance = std::make_shared<noice::game_manager>();
}

void noice::game_manager::finalize()
{
	noice::game_manager::_instance = nullptr;
}

std::shared_ptr<noice::game_manager> noice::game_manager::instance()
{
	return noice::game_manager::_instance;
}
