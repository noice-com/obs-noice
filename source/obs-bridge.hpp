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
#include <string>
#include <memory>
#include "common.hpp"

namespace obs {
class bridge {
	void *_module;
	bool _is_slobs;
	bool _has_create_signals;
	const struct obs_output_info *(*_find_output)(const char *id);
	video_t *(*_obs_view_add)(obs_view_t *view);
	video_t *(*_obs_view_add2)(obs_view_t *view, struct obs_video_info *ovi);
	void (*_obs_view_remove)(obs_view_t *view);

public:
	virtual ~bridge();
	bridge();

	bool is_slobs() { return _is_slobs; }

	bool has_create_signals() { return _has_create_signals; }

	bool has_find_output() { return _find_output != nullptr; }
	const struct obs_output_info *find_output(const char *id) { return (_find_output == nullptr ? nullptr : _find_output(id)); };

	bool has_obs_view_add() { return _obs_view_add != nullptr; }
	video_t *obs_view_add(obs_view_t *view) { return _obs_view_add(view); }

	bool has_obs_view_add2() { return _obs_view_add2 != nullptr; }
	video_t *obs_view_add2(obs_view_t *view, struct obs_video_info *ovi) { return _obs_view_add2(view, ovi); }

	bool has_obs_view_remove() { return _obs_view_remove != nullptr; }
	void obs_view_remove(obs_view_t *view) { return _obs_view_remove(view); }

	bool has_double_wide_capability() { return _has_create_signals && _obs_view_add2 != nullptr; }

	// Singleton
private:
	static std::shared_ptr<obs::bridge> _instance;

public:
	static void initialize();
	static void finalize();

	static std::shared_ptr<obs::bridge> instance();
};

} // namespace obs
