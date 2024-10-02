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

#include "scene-tracker.hpp"
#include "common.hpp"
#include "auth.hpp"
#include "noice-validator.hpp"
#include "game.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <obs-module.h>
#include <util/util-curl.hpp>

#define DMON_IMPL
COMPILER_WARNINGS_PUSH
#if !defined(COMPILER_MSVC)
COMPILER_WARNINGS_DISABLE("-Wmissing-field-initializers")
#endif
#include <dmon/dmon.h>
COMPILER_WARNINGS_POP

#if ENABLE_SINGLETON_SOURCE
static constexpr std::string_view NOICE_VALIDATOR_SOURCE_NAME_SINGLETON = "Noice Validator (Singleton)";
#endif

constexpr float UPDATE_SELECTED_GAME_INTERVAL = 30.0f;
constexpr float SEND_DIAGNOSTICS_INTERVAL = 10.0f;
constexpr float SCENE_CHECK_INTERVAL = 1.0f;

noice::source::scene_tracker::~scene_tracker()
{
	os_task_queue_wait(_task_queue);
	os_task_queue_destroy(_task_queue);
	os_task_queue_wait(_diagnostics_task_queue);
	os_task_queue_destroy(_diagnostics_task_queue);
	obs_remove_tick_callback(obs_tick_handler, this);

	release_sources();
	if (_dmon_initialized)
		dmon_deinit();
}

noice::source::scene_tracker::scene_tracker()
	: _time_elapsed(0.0f),
	  _time_elapsed_diagnostics(0.0f),
	  _time_elapsed_selected_game(UPDATE_SELECTED_GAME_INTERVAL),
#if ENABLE_SINGLETON_SOURCE
	  _current_scene(nullptr),
	  _current_source(nullptr),
#endif
	  _frontend_preview_scene(nullptr),
	  _frontend_current_scene(nullptr),
	  _frontend_scene_reset(false),
	  _current_output_source(nullptr),
	  _current_enum_scene(nullptr),
	  _startup_complete(false),
	  _has_finished_loading(false),
	  _task_queue(nullptr),
	  _dmon_initialized(false),
	  _current_scene_has_noice_validator(false)
{
	_task_queue = os_task_queue_create();
	queue_task([](void *param) { os_set_thread_name("noice thread"); }, (void *)this, false);
	_diagnostics_task_queue = os_task_queue_create();
	queue_task([](void *param) { os_set_thread_name("noice diagnostics thread"); }, nullptr, false, _diagnostics_task_queue);

	obs_add_tick_callback(obs_tick_handler, this);

	auto cfg = noice::configuration::instance();
	if (cfg && cfg->is_slobs()) {
		scenecollection_watch();
	}
}

#if ENABLE_SINGLETON_SOURCE

extern const char *NOICE_VALIDATOR_PLUGIN_ID;

static obs_source_t *get_noice_validator_source()
{
	auto bdata = std::shared_ptr<obs_data_t>(obs_data_create(), [](obs_data_t *v) {
		auto data = noice::configuration::instance()->get();
		obs_data_set_obj(data.get(), "singleton", v);
		obs_data_release(v);
	});

	auto data = noice::configuration::instance()->get();
	obs_data_t *old = obs_data_get_obj(data.get(), "singleton");
	obs_data_apply(bdata.get(), old);
	obs_data_release(old);

	obs_source_t *source =
		obs_source_create_private(NOICE_VALIDATOR_PLUGIN_ID, NOICE_VALIDATOR_SOURCE_NAME_SINGLETON.data(), bdata.get());
	return source;
}

void noice::source::scene_tracker::validator_track_scene(obs_source_t *source)
{
	obs_sceneitem_t *sceneitem = obs_scene_sceneitem_from_source(_current_scene, _current_source);
	if (sceneitem) {
		obs_sceneitem_remove(sceneitem);
		obs_sceneitem_release(sceneitem);
	}
	obs_scene_release(_current_scene);
	_current_scene = nullptr;

	if (source == nullptr)
		return;

	_current_scene = obs_scene_from_source(source);
	if (_current_source == nullptr)
		_current_source = get_noice_validator_source();
	obs_scene_add(_current_scene, _current_source);
}

#endif

// #define DEBUG_SCENE_NAMES

#ifdef DEBUG_SCENE_NAMES
static void print_weak_source(const char *label, obs_weak_source_t *weak_source)
{
	obs_source *source = obs_weak_source_get_source(weak_source);
	DLOG_INFO("tick_handler: %s: %p / %s", label, source, obs_source_get_name(source));
	obs_source_release(source);
}
#endif

void noice::source::scene_tracker::tick_handler()
{
	// DLOG_INFO("tick_handler: --");
	{
		std::unique_lock<std::mutex> lock(_lock);
		size_t prev_scene_count = _current_tick_scenes.size();

		obs_weak_source_release(_current_enum_scene);
		_current_enum_scene = nullptr;
		_current_tick_scenes.clear();

		auto cb = [](void *param, obs_source_t *source) {
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE) {
				obs_scene_t *scene = obs_scene_from_source(source);

				// We don't care about groups
				if (!scene)
					return true;

				noice::source::scene_tracker *self = reinterpret_cast<noice::source::scene_tracker *>(param);
				auto sceneptr = std::shared_ptr<obs_weak_source_t>(
					obs_source_get_weak_source(source), [](obs_weak_source_t *v) { obs_weak_source_release(v); });
				self->_current_tick_scenes.push_back(sceneptr);
			}
			return true;
		};
		obs_enum_all_sources(cb, this);
		size_t scene_count = _current_tick_scenes.size();

		if (prev_scene_count != scene_count) {
			_frontend_scene_reset = true;
			DLOG_INFO("tick_handler: TOTAL SCENES: %zu", scene_count);
		}

#ifdef DEBUG_SCENE_NAMES
		if (_frontend_scene_reset) {
			print_weak_source("frontend_preview_scene", _frontend_preview_scene);
			print_weak_source("frontend_current_scene", _frontend_current_scene);
			for (auto sceneptr : _current_tick_scenes)
				print_weak_source("scene", sceneptr.get());
		}
#endif

		// If we have no scenes,  might as well skip the rest (shutdown phase? scene collection changed?)
		if (scene_count == 0)
			return;
	}

	if (_startup_complete == false) {
		// Make sure all dependencies we care about are really loaded.
		// There might be some differences depending on where the the plugin is installed
		if (!obs_get_module("rtmp-services") || !obs_get_module("obs-outputs"))
			return;

		// first OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP trigger equivalent
		_startup_complete = true;
		DLOG_INFO("tick_handler: STARTUP COMPLETE");

		noice::configuration::instance()->probe_service_changed();
	}

	// Good enough to query program scene, but not preview
	obs_source_t *transition = obs_get_output_source(0);
	obs_source_t *source = transition ? obs_transition_get_active_source(transition) : nullptr;
	obs_weak_source_t *wsource = obs_source_get_weak_source(source);
	obs_source_release(source);
	obs_source_release(transition);

	if (_current_output_source != wsource) {
		// OBS_FRONTEND_EVENT_SCENE_CHANGED equivalent
		//
		// Supposedly there's no need to try to workaround:
		// OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED
		// OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED
		// OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED

		if (_current_output_source == nullptr && wsource != nullptr) {
			// OBS_FRONTEND_EVENT_FINISHED_LOADING equivalent
			DLOG_INFO("tick_handler: FINISHED LOADING");
			load();
		} else if (_current_output_source != nullptr && wsource == nullptr) {
			// OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN equivalent
			DLOG_INFO("tick_handler: SCRIPTING SHUTDOWN");
			unload();
		}

		// cache for get_current_scene, obs_frontend_get_current_scene equivalent
		obs_weak_source_release(_current_output_source);
		_current_output_source = wsource;
		_frontend_scene_reset = true;
	} else {
		obs_weak_source_release(wsource);
	}

	if (_frontend_scene_reset) {
		DLOG_INFO("tick_handler: SCENE CHANGED");
		_frontend_scene_reset = false;
		_hit_source_names.clear();

		obs_source_t *src = obs_weak_source_get_source(_current_output_source);
		if (src) {
			obs_scene_t *scene = obs_scene_from_source(src);
			auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *param) -> bool {
				obs_source_t *item_source = obs_sceneitem_get_source(item);
				const char *src_id = obs_source_get_id(item_source);

				if (src_id && !strcmp(src_id, "noice_validator")) {
					auto st = reinterpret_cast<noice::source::scene_tracker *>(param);
					st->set_current_scene_has_noice_validator(true);
					return false;
				}

				return true;
			};
			set_current_scene_has_noice_validator(false);
			obs_scene_enum_items(scene, cb, this);
			obs_source_release(src);
		}

#if ENABLE_SINGLETON_SOURCE
		validator_track_scene(get_current_scene(true));
#endif
	}

	if (has_finished_loading() == false)
		return;

	if (_time_elapsed >= SCENE_CHECK_INTERVAL) {
		_time_elapsed = 0.0f;

		// Not sure if it's worth it to be more signal aware to trigger this
		auto cb = [](void *, obs_source_t *source) {
			noice::source::validator_instance::sort_sceneitems(obs_scene_from_source(source));
			return true;
		};
		obs_enum_scenes(cb, nullptr);

		noice::configuration::instance()->probe_service_changed();

		{
			std::unique_lock<std::mutex> lock(_selected_game_lock, std::try_to_lock);
			if (lock.owns_lock()) {
				if (_fetched_selected_game != "" && _fetched_selected_game != _last_selected_game) {
					update_selected_game();
					_last_selected_game = _fetched_selected_game;
				}

				_fetched_selected_game = "";
			}
		}
	}

	diagnostics_tick();
	send_diagnostics_if_ready();

	update_selected_game_tick();
}

bool noice::source::scene_tracker::current_scene_has_noice_validator()
{
	return _current_scene_has_noice_validator;
}

void noice::source::scene_tracker::set_current_scene_has_noice_validator(bool has)
{
	std::unique_lock<std::mutex> lock(_diagnostics_lock);

	_current_scene_has_noice_validator = has;
}

void noice::source::scene_tracker::add_hit_item_source_names(std::vector<std::string> &names)
{
	std::unique_lock<std::mutex> lock(_diagnostics_lock);

	_hit_source_names = std::move(names);
	_waiting_diagnostics[diagnostics_type::hit_source_names] = false;
}

void noice::source::scene_tracker::send_diagnostics(void *param)
{
	noice::source::scene_tracker *st = reinterpret_cast<noice::source::scene_tracker *>(param);

	auto auth = noice::auth::instance();
	auto access_token = auth->get_access_token();

	if (!access_token) {
		DLOG_WARNING("failed to get access token");
		return;
	}

	std::ostringstream auth_header;
	auth_header << "Bearer " << (*access_token);

	std::unique_lock<std::mutex> lock(st->_diagnostics_lock);

	bool missingValidator = !st->current_scene_has_noice_validator();

	std::vector<std::string> hit_item_source_names = std::move(st->_hit_source_names);

	nlohmann::json payload = {
		{"event",
		 {
			 {"obsPluginInfo",
			  {
				  {"obsVersion", obs_get_version_string()},
				  {"pluginVersion", PROJECT_VERSION},
			  }},
			 {"obsNoiceValidator",
			  {
				  {"missingValidator", missingValidator},
				  {"occludingSourceNames", hit_item_source_names},
			  }},
		 }},
	};

	lock.unlock();

	std::ostringstream response_stream;

	auto cb = [&response_stream](void *data, size_t size, size_t nmemb) -> size_t {
		const char *res = reinterpret_cast<char *>(data);
		response_stream.write(res, size * nmemb);

		return size * nmemb;
	};

	std::string json = payload.dump();

	std::string endpoint = noice::get_api_endpoint("v1/streamer/diagnostics");

	noice::util::curl c;
	c.set_option(CURLOPT_URL, endpoint);
	c.set_option(CURLOPT_POST, true);
	c.set_header("Content-Type", "application/json");
	c.set_header("Authorization", auth_header.str());
	c.set_option(CURLOPT_POSTFIELDS, json.c_str());
	c.set_write_callback(cb);

	CURLcode code = c.perform();

	lock.lock();
	st->_queued_diagnostics = false;
	st->clear_diagnostics();
	lock.unlock();

	if (code != CURLE_OK) {
		DLOG_WARNING("diagnostics request failed.");
		return;
	}

	long response_code = -1;
	c.get_info(CURLINFO_RESPONSE_CODE, response_code);

	if (response_code != 200) {
		DLOG_WARNING("diagnostics request failed with code: %ld, response: %s", response_code, response_stream.str().c_str());
		return;
	}
}

bool noice::source::scene_tracker::needs_diagnostics(diagnostics_type type)
{
	std::unique_lock<std::mutex> lock(_diagnostics_lock);

	auto it = _waiting_diagnostics.find(type);

	return it != _waiting_diagnostics.end() && it->second;
}

void noice::source::scene_tracker::clear_diagnostics()
{
	_waiting_diagnostics.clear();
}

void noice::source::scene_tracker::diagnostics_tick()
{
	if (_time_elapsed_diagnostics <= SEND_DIAGNOSTICS_INTERVAL) {
		return;
	}

	_time_elapsed_diagnostics = 0.0f;

	auto cfg = noice::configuration::instance();

	std::unique_lock<std::mutex> lock(_diagnostics_lock);

	clear_diagnostics();

	if (!cfg->streaming_active() || !cfg->noice_service_selected()) {
		return;
	}

	_waiting_diagnostics[diagnostics_type::hit_source_names] = true;
}

void noice::source::scene_tracker::update_selected_game_tick()
{
	if (_time_elapsed_selected_game <= UPDATE_SELECTED_GAME_INTERVAL) {
		return;
	}

	_time_elapsed_selected_game = 0.0f;

	auto cfg = noice::configuration::instance();

	if (cfg->streaming_active() || !cfg->noice_service_selected()) {
		return;
	}

	trigger_fetch_selected_game();
}

void noice::source::scene_tracker::send_diagnostics_if_ready()
{
	std::unique_lock<std::mutex> lock(_diagnostics_lock);

	if (_queued_diagnostics || _waiting_diagnostics.size() == 0) {
		return;
	}

	for (const auto &pair : _waiting_diagnostics) {
		if (pair.second) {
			return;
		}
	}

	_queued_diagnostics = true;
	queue_task(send_diagnostics, this, false, _diagnostics_task_queue);
}

bool noice::source::scene_tracker::update_selected_game_enum_item(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	noice::source::scene_tracker *st = reinterpret_cast<noice::source::scene_tracker *>(param);

	obs_source_t *src = obs_sceneitem_get_source(item);
	const char *src_id = obs_source_get_id(src);

	if (!strcmp(src_id, "noice_validator")) {
		obs_data_t *data = obs_source_get_settings(src);
		obs_data_set_string(data, "game", st->_fetched_selected_game.c_str());
		obs_source_update(src, data);
		obs_data_release(data);
	}

	return true;
}

void noice::source::scene_tracker::update_selected_game()
{
	DLOG_INFO("updating selected game, %s", _fetched_selected_game.c_str());
	if (!_fetched_selected_game_needs_validator) {
		_fetched_selected_game = "no_game_selected";
	}

	auto gm = noice::game_manager::instance();
	auto game = gm->get_game(_fetched_selected_game);

	if (!game) {
		DLOG_WARNING("failed to find config for selected game: %s", _fetched_selected_game.c_str());
		return;
	}

	auto cb = [](void *param, obs_source_t *source) {
		obs_scene_t *scene = obs_scene_from_source(source);
		obs_scene_enum_items(scene, update_selected_game_enum_item, param);

		return true;
	};
	obs_enum_scenes(cb, (void *)this);
}

void noice::source::scene_tracker::fetch_selected_game(void *param)
{
	noice::source::scene_tracker *st = reinterpret_cast<noice::source::scene_tracker *>(param);

	std::unique_lock<std::mutex> lock(st->_selected_game_lock);

	if (st->_fetched_selected_game != "") {
		return;
	}

	auto auth = noice::auth::instance();
	auto access_token = auth->get_access_token();

	if (!access_token) {
		DLOG_WARNING("failed to get access token");
		return;
	}

	std::ostringstream auth_header;
	auth_header << "Bearer " << (*access_token);

	std::string endpoint = noice::get_api_endpoint("v1/streamer/selected_game");

	std::ostringstream response_stream;
	auto write_cb = [&response_stream](void *data, size_t size, size_t nmemb) -> size_t {
		const char *res = reinterpret_cast<char *>(data);
		response_stream.write(res, size * nmemb);

		return size * nmemb;
	};

	noice::util::curl c;
	c.set_option(CURLOPT_URL, endpoint);
	c.set_header("Authorization", auth_header.str());
	c.set_write_callback(write_cb);

	CURLcode code = c.perform();
	if (code != CURLE_OK) {
		DLOG_WARNING("get selected game request failed. %s", curl_easy_strerror(code));
		return;
	}

	long response_code = -1;
	c.get_info(CURLINFO_RESPONSE_CODE, response_code);

	if (response_code != 200) {
		DLOG_WARNING("get selected game request failed with response code: %ld %s", response_code, response_stream.str().c_str());
		return;
	}

	nlohmann::json selected_game_response;

	try {
		selected_game_response = nlohmann::json::parse(response_stream.str());
	} catch (...) {
		DLOG_WARNING("failed to parse response for get selected game request");
		return;
	}

	if (!selected_game_response.contains("gameId")) {
		DLOG_WARNING("response does not contain game id");
		return;
	}

	st->_fetched_selected_game = selected_game_response["gameId"].template get<std::string>();

	if (selected_game_response.contains("needsValidator")) {
		st->_fetched_selected_game_needs_validator = selected_game_response["needsValidator"].template get<bool>();
	} else {
		st->_fetched_selected_game_needs_validator = false;
	}

	if (st->_fetched_selected_game != st->_last_selected_game) {
		DLOG_INFO("got new selected game: %s, needs validator: %d", st->_fetched_selected_game.c_str(),
			  st->_fetched_selected_game_needs_validator);
	}
}

void noice::source::scene_tracker::trigger_fetch_selected_game()
{
	queue_task([](void *param) { fetch_selected_game(param); }, (void *)this, false, _diagnostics_task_queue);
}

void noice::source::scene_tracker::obs_tick_handler(void *private_data, float seconds)
{
	noice::source::scene_tracker *self = reinterpret_cast<noice::source::scene_tracker *>(private_data);
	self->_time_elapsed += seconds;
	self->_time_elapsed_diagnostics += seconds;
	self->_time_elapsed_selected_game += seconds;
	self->tick_handler();
}

void noice::source::scene_tracker::load()
{
	_has_finished_loading = true;
}

void noice::source::scene_tracker::unload()
{
	release_sources();

	_has_finished_loading = false;
}

void noice::source::scene_tracker::scene_tracker::release_sources()
{
#if ENABLE_SINGLETON_SOURCE
	obs_sceneitem_t *sceneitem = obs_scene_sceneitem_from_source(_current_scene, _current_source);
	if (sceneitem) {
		obs_sceneitem_remove(sceneitem);
		obs_sceneitem_release(sceneitem);
	}
	obs_scene_release(_current_scene);
	_current_scene = nullptr;
	obs_source_remove(_current_source);
	obs_source_release(_current_source);
	_current_source = nullptr;
#endif

	obs_weak_source_release(_current_enum_scene);
	_current_enum_scene = nullptr;
	_current_tick_scenes.clear();

	obs_weak_source_release(_frontend_preview_scene);
	_frontend_preview_scene = nullptr;
	obs_weak_source_release(_frontend_current_scene);
	_frontend_current_scene = nullptr;

	obs_weak_source_release(_current_output_source);
	_current_output_source = nullptr;
}

struct task_wait_info {
	os_task_t task;
	void *param;
	os_event_t *event;
};

static void task_wait_callback(void *param)
{
	struct task_wait_info *info = (struct task_wait_info *)param;
	if (info->task)
		info->task(info->param);
	os_event_signal(info->event);
}

void noice::source::scene_tracker::queue_task(os_task_t task, void *param, bool wait, os_task_queue_t *queue)
{
	if (!queue) {
		queue = _task_queue;
	}

	if (os_task_queue_inside(queue)) {
		task(param);
	} else if (wait) {
		struct task_wait_info info = {};
		info.task = task;
		info.param = param;

		os_event_init(&info.event, OS_EVENT_TYPE_MANUAL);
		queue_task(task_wait_callback, &info, false, queue);
		os_event_wait(info.event);
		os_event_destroy(info.event);
	} else {
		os_task_queue_queue_task(queue, task, param);
	}
}

void noice::source::scene_tracker::set_preview_scene(obs_source_t *source)
{
	if (_frontend_preview_scene != nullptr) {
		obs_weak_source_release(_frontend_preview_scene);
	}
	_frontend_preview_scene = obs_source_get_weak_source(source);
	_frontend_scene_reset = true;
}

void noice::source::scene_tracker::set_current_scene(obs_source_t *source)
{
	if (_frontend_current_scene != nullptr) {
		obs_weak_source_release(_frontend_current_scene);
	}
	_frontend_current_scene = obs_source_get_weak_source(source);
	_frontend_scene_reset = true;
}

obs_source_t *noice::source::scene_tracker::get_current_scene(bool preview)
{
	// Use frontend UI provided scene information if available
	obs_weak_source_t *source = _frontend_current_scene ? _frontend_current_scene : _current_output_source;
	source = preview && _frontend_preview_scene ? _frontend_preview_scene : source;

	return obs_weak_source_get_source(source);
}

// Obviously a hack for now. It'd be nice to have an official API method to query
// the current scene related to rendering. obs_frontend_get_current_scene depends
// frontend library and is not accurate for all use cases (Multiview etc)
void noice::source::scene_tracker::probe_current_enum_scene_source()
{
	std::unique_lock<std::mutex> lock(_lock);
	bool found = false;

	for (auto sceneptr : _current_tick_scenes) {
		obs_weak_source_t *wsource = sceneptr.get();
		obs_source_t *source = obs_weak_source_get_source(wsource);
		obs_scene_t *scene = obs_scene_from_source(source);

		if (!scene)
			continue;

		// Ehh, we want to find the active scene instance that's already locked for rendering
		// and that's fun with PTHREAD_MUTEX_RECURSIVE if you're in the same thread.
		int ret = pthread_mutex_trylock(&scene->video_mutex);
		if (ret == 0)
			pthread_mutex_unlock(&scene->video_mutex);
		// DLOG_INFO("probe_current_enum_scene_source: %p / %s: ret: %d", scene, obs_source_get_name(obs_scene_get_source(scene)), ret);

		obs_source_release(source);
		if (ret != 0) {
			obs_weak_source_release(_current_enum_scene);
			_current_enum_scene = wsource;
			obs_weak_source_addref(_current_enum_scene);
			found = true;
			break;
		}
	}

	if (found == false) {
		obs_weak_source_release(_current_enum_scene);
		_current_enum_scene = nullptr;
	}
}

obs_weak_source_t *noice::source::scene_tracker::get_current_enum_scene()
{
	queue_task(
		[](void *param) {
			noice::source::scene_tracker *self = reinterpret_cast<noice::source::scene_tracker *>(param);
			self->probe_current_enum_scene_source();
		},
		(void *)this, true);
	return _current_enum_scene;
}

void noice::source::scene_tracker::scenecollection_watch()
{
	if (!_dmon_initialized) {
		_dmon_initialized = true;
		dmon_init();
	}

	auto cb = [](dmon_watch_id watch_id, dmon_action action, const char *rootdir, const char *filepath, const char *oldfilepath,
		     void *user) {
		noice::source::scene_tracker *self = reinterpret_cast<noice::source::scene_tracker *>(user);

		if (!strcmp(filepath, "manifest.json"))
			self->scenecollection_update();
	};

	const char *manifest = obs_module_config_path("../../SceneCollections");
	_sc_root_dir = std::string(manifest);
	bfree((void *)manifest);

	dmon_watch(_sc_root_dir.c_str(), cb, 0, this);

	scenecollection_update();
}

void noice::source::scene_tracker::scenecollection_update()
{
	std::unique_lock<std::mutex> lock(_sc_lock);
	std::string manifest = noice::string_format("%s/manifest.json", _sc_root_dir.c_str());
	std::string active_id;

	try {
		std::ifstream manifest_json(manifest, std::ios::in);
		nlohmann::json data = nlohmann::json::parse(std::ifstream(manifest, std::ios::in));
		manifest_json.close();

		if (data.find("activeId") != data.end())
			active_id = data["activeId"].get<std::string>();
	} catch (std::exception const &ex) {
		DLOG_ERROR("JSON parse error: %s", ex.what());
		return;
	}

	// I suppose there's no real reason to care if scene collection is changed
	if (active_id.empty())
		return;

	std::string scenecollection = noice::string_format("%s/%s.json", _sc_root_dir.c_str(), active_id.c_str());
	std::ifstream scenecollection_json(scenecollection, std::ios::in);
	scenecollection_parse(scenecollection_json);
	scenecollection_json.close();
}

bool noice::source::scene_tracker::scenecollection_parse(std::istream &input)
{
	std::map<std::string, std::string> guid2source;
	std::map<std::string, std::string> source2guid;

	try {
		nlohmann::json data;
		data = nlohmann::json::parse(input);

		nlohmann::json obj = data["sources"];
		for (auto items_it : obj["items"]) {
			auto item_obj = items_it.get<nlohmann::json::object_t>();
			std::string guid = item_obj["id"].get<std::string>();
			std::string name = item_obj["name"].get<std::string>();

			guid2source[guid] = name;
			source2guid[name] = guid;
		}

		obj = data["scenes"];
		for (auto items_it : obj["items"]) {
			auto item_obj = items_it.get<nlohmann::json::object_t>();
			std::string guid = item_obj["id"].get<std::string>();
			std::string name = item_obj["name"].get<std::string>();

			guid2source[guid] = name;
			source2guid[name] = guid;
		}
	} catch (std::exception const &ex) {
		DLOG_ERROR("JSON parse error: %s", ex.what());
		return false;
	}

	_sc_collection_changed = guid2source != _sc_guid2source;
	if (_sc_collection_changed) {
		_sc_guid2source = guid2source;
		_sc_source2guid = source2guid;

		for (const auto &p : guid2source) {
			DLOG_INFO("guid: %s source: %s", p.first.c_str(), p.second.c_str());
		}
	}

	return true;
}

std::shared_ptr<noice::source::scene_tracker> noice::source::scene_tracker::_instance = nullptr;

void noice::source::scene_tracker::initialize()
{
	if (!noice::source::scene_tracker::_instance)
		noice::source::scene_tracker::_instance = std::make_shared<noice::source::scene_tracker>();
}

void noice::source::scene_tracker::finalize()
{
	noice::source::scene_tracker::_instance.reset();
}

std::shared_ptr<noice::source::scene_tracker> noice::source::scene_tracker::instance()
{
	return noice::source::scene_tracker::_instance;
}
