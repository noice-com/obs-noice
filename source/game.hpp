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

#pragma once

#include <cinttypes>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <istream>
#include <obs.h>

#define NOICE_PLACEHOLDER_GAME_NAME "no_game_selected"

namespace noice {

enum box_format { XYXY = 0, XYWH = 1, CXCYWH = 2 };

typedef std::tuple<float, float, float, float> box_tuple;

enum anchor {
	TOP_LEFT = 0,
	TOP_MIDDLE = 1,
	TOP_RIGHT = 2,
	MIDDLE_LEFT = 3,
	CENTER = 4,
	MIDDLE_RIGHT = 5,
	BOTTOM_LEFT = 6,
	BOTTOM_MIDDLE = 7,
	BOTTOM_RIGHT = 8,
	LEFT = 9,
	MIDDLE_X = 10,
	RIGHT = 11,
	TOP = 12,
	MIDDLE_Y = 13,
	BOTTOM = 14
};

struct anchor_map : public std::map<std::string, anchor> {
	anchor_map();
	~anchor_map() {}
};

enum align_axis {
	X = 0,
	Y = 1,
};

struct video_resolution {
	int width;
	int height;
	std::string resolution;

	video_resolution() : width(0), height(0) { resolution = std::to_string(width) + "x" + std::to_string(height); }

	bool operator==(const video_resolution &o) const { return width == o.width && height == o.height; }

	bool operator<(const video_resolution &o) const { return std::tie(width, height) < std::tie(o.width, o.height); }
};

struct region_rect {
	float x;
	float y;
	float w;
	float h;

	region_rect() : x(0.0f), y(0.0f), w(0.0f), h(0.0f) {}
};

class region {
public:
	~region(){};

	region(std::shared_ptr<video_resolution> base, std::string state, std::string name, anchor alignment, bool hud_scale_locked,
	       region_rect rect)
		: base(base),
		  game_state(state),
		  region_name(name),
		  alignment(alignment),
		  hud_scale_locked(hud_scale_locked),
		  rect(rect),
		  hits(0)
	{
	}

	std::shared_ptr<video_resolution> base;
	std::string game_state;
	std::string region_name;
	anchor alignment;
	bool hud_scale_locked;
	region_rect rect;
	int hits;

	region_rect box;

	void align_box(struct obs_video_info ovi, float hud_scale);
};

struct in_game_hud_scale {
	float min, max;
	float step;
	float value;

	in_game_hud_scale() : min(1.0f), max(1.0f), step(0.25f), value(1.0) {}

	float clamp_value();
};

// Taken and modified from https://gist.github.com/yoggy/8999625
#define min_f(a, b, c) (fminf(a, fminf(b, c)))
#define max_f(a, b, c) (fmaxf(a, fmaxf(b, c)))

struct hsv_util {
	int hue;        // 0-360
	int saturation; // 0-255
	int value;      // 0-255
	float alpha;    // 0.0-1.0

	//rgba2hsv
	hsv_util(vec4 val)
	{
		float r = val.x;
		float g = val.y;
		float b = val.z;
		alpha = val.w;

		float h, s, v; // h:0-360.0, s:0.0-1.0, v:0.0-1.0

		float max = max_f(r, g, b);
		float min = min_f(r, g, b);

		v = max;

		if (max == 0.0f) {
			s = 0;
			h = 0;
		} else if (max - min == 0.0f) {
			s = 0;
			h = 0;
		} else {
			s = (max - min) / max;

			if (max == r) {
				h = 60 * ((g - b) / (max - min)) + 0;
			} else if (max == g) {
				h = 60 * ((b - r) / (max - min)) + 120;
			} else {
				h = 60 * ((r - g) / (max - min)) + 240;
			}
		}

		if (h < 0)
			h += 360.0f;

		hue = (int)(h);
		saturation = (int)(s * 255);
		value = (int)(v * 255);
	}

	// hsv2rgba
	void to_vec4(vec4 &out)
	{
		float h = (float)hue;                 // 0-360
		float s = (float)saturation / 255.0f; // 0.0-1.0
		float v = (float)value / 255.0f;      // 0.0-1.0

		float r = 0.0f, g = 0.0f, b = 0.0f; // 0.0-1.0

		int hi = (int)(h / 60.0f) % 6;
		float f = (h / 60.0f) - hi;
		float p = v * (1.0f - s);
		float q = v * (1.0f - s * f);
		float t = v * (1.0f - s * (1.0f - f));

		switch (hi) {
		case 0:
			r = v, g = t, b = p;
			break;
		case 1:
			r = q, g = v, b = p;
			break;
		case 2:
			r = p, g = v, b = t;
			break;
		case 3:
			r = p, g = q, b = v;
			break;
		case 4:
			r = t, g = p, b = v;
			break;
		case 5:
			r = v, g = p, b = q;
			break;
		}

		out.x = r;
		out.y = g;
		out.z = b;
		out.w = alpha;
	}
};

class game {
public:
	~game();
	game();

	std::string name;
	std::string name_verbose;
	std::vector<std::shared_ptr<video_resolution>> resolutions;
	std::map<std::shared_ptr<video_resolution>, std::shared_ptr<std::vector<noice::region>>> map;
	in_game_hud_scale in_game_hud;

	std::shared_ptr<video_resolution> current_resolution;

	bool reset_regions;
	bool disabled;

	std::shared_ptr<std::vector<noice::region>> regions() { return map[current_resolution]; }
};

class game_manager {
	std::mutex _lock;
	std::mutex _lock_active;
	std::vector<std::string> _games;
	std::map<std::string, std::shared_ptr<noice::game>> _game_map;
	std::map<std::string, std::string> _game_active;

public:
	virtual ~game_manager();
	game_manager();

	std::vector<std::string> get_games();
	std::shared_ptr<noice::game> get_game(std::string name);

	bool is_game_acquired(std::string name, std::string instance);
	bool is_game_acquired(std::shared_ptr<noice::game> game, std::string instance);
	void acquire_game(std::shared_ptr<noice::game> game, std::string instance);
	void release_game(std::shared_ptr<noice::game> game, std::string instance);

	void refresh();

private:
	bool refresh_main(std::istream &input);

	// Singleton
private:
	static std::shared_ptr<noice::game_manager> _instance;

public:
	static void initialize();
	static void finalize();

	static std::shared_ptr<noice::game_manager> instance();
};

} // namespace noice
