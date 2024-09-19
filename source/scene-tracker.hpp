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

enum class diagnostics_type {
	hit_source_names,
};

class scene_tracker {
private:
	float _time_elapsed;
	float _time_elapsed_diagnostics;

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
	os_task_queue_t *_diagnostics_task_queue;
	std::vector<std::shared_ptr<obs_weak_source_t>> _current_tick_scenes;
	std::mutex _lock;

	std::string _sc_root_dir;
	bool _sc_collection_changed;
	std::map<std::string, std::string> _sc_guid2source;
	std::map<std::string, std::string> _sc_source2guid;
	std::mutex _sc_lock;

	bool _dmon_initialized;

	std::vector<std::string> _hit_source_names;
	bool _current_scene_has_noice_validator;
	std::map<diagnostics_type, bool> _waiting_diagnostics;
	bool _queued_diagnostics;
	std::mutex _diagnostics_lock;

	std::mutex _selected_game_lock;
	std::string _fetched_selected_game;
	bool _fetched_selected_game_needs_validator;

public:
	virtual ~scene_tracker();
	scene_tracker();

private:
	static void obs_tick_handler(void *private_data, float seconds);
	static void send_diagnostics(void *param);
	static void fetch_selected_game(void *param);
	static bool update_selected_game_enum_item(obs_scene_t *scene, obs_sceneitem_t *item, void *param);

	void tick_handler();

#if ENABLE_SINGLETON_SOURCE
	void validator_track_scene(obs_source_t *source);
#endif

	void queue_task(os_task_t task, void *param, bool wait, os_task_queue_t *queue = nullptr);

	void load();

	void unload();

	void release_sources();

	void probe_current_enum_scene_source();

	void scenecollection_watch();

	void scenecollection_update();

	bool scenecollection_parse(std::istream &input);

	void diagnostics_tick();

	void set_current_scene_has_noice_validator(bool has);

	bool current_scene_has_noice_validator();

	void clear_diagnostics();

	void send_diagnostics_if_ready();

	void update_selected_game();

public:
	virtual void set_preview_scene(obs_source_t *source);

	virtual void set_current_scene(obs_source_t *source);

	virtual obs_source_t *get_current_scene(bool preview = false);

	virtual obs_weak_source_t *get_current_enum_scene();

	virtual bool has_finished_loading() { return _has_finished_loading; };

	virtual void add_hit_item_source_names(std::vector<std::string> &names);

	virtual bool needs_diagnostics(diagnostics_type type);

	virtual void trigger_fetch_selected_game();

private /* Singleton */:
	static std::shared_ptr<noice::source::scene_tracker> _instance;

public /* Singleton */:
	static void initialize();

	static void finalize();

	static std::shared_ptr<noice::source::scene_tracker> instance();
};
} // namespace noice::source
