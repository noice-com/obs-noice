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
#include <memory>
#include <mutex>
#include <vector>
#include <map>
#include <obs.h>
#include <util/task.h>
#include <util/threading.h>
#include <obs-scene.h>

#define ENABLE_SINGLETON_SOURCE 0

namespace noice::source {
class scene_tracker {
private:
	float _time_elapsed;

#if ENABLE_SINGLETON_SOURCE
	obs_scene_t *_current_scene;
	obs_source_t *_current_source;
#endif

	obs_weak_source_t *_frontend_preview_scene;
	obs_weak_source_t *_frontend_current_scene;
	bool _frontend_scene_reset;

	obs_weak_source_t *_current_output_source;
	obs_weak_source_t *_current_enum_scene;
	bool _startup_complete;
	bool _has_finished_loading;

	os_task_queue_t *_task_queue;
	std::vector<std::shared_ptr<obs_weak_source_t>> _current_tick_scenes;
	std::mutex _lock;

	std::string _sc_root_dir;
	bool _sc_collection_changed;
	std::map<std::string, std::string> _sc_guid2source;
	std::map<std::string, std::string> _sc_source2guid;
	std::mutex _sc_lock;

	bool _dmon_initialized;

public:
	virtual ~scene_tracker();
	scene_tracker();

private:
	static void obs_tick_handler(void *private_data, float seconds);

	void tick_handler();

#if ENABLE_SINGLETON_SOURCE
	void validator_track_scene(obs_source_t *source);
#endif

	void load();

	void unload();

	void release_sources();

	void probe_current_enum_scene_source();

	void scenecollection_watch();

	void scenecollection_update();

	bool scenecollection_parse(std::istream &input);

public:
	virtual void queue_task(os_task_t task, void *param, bool wait);

	virtual void set_preview_scene(obs_source_t *source);

	virtual void set_current_scene(obs_source_t *source);

	virtual obs_source_t *get_current_scene(bool preview = false);

	virtual obs_weak_source_t *get_current_enum_scene();

	virtual bool has_finished_loading() { return _has_finished_loading; };

private /* Singleton */:
	static std::shared_ptr<noice::source::scene_tracker> _instance;

public /* Singleton */:
	static void initialize();

	static void finalize();

	static std::shared_ptr<noice::source::scene_tracker> instance();
};
} // namespace noice::source
