// Copyright (C) 2023 Noice Inc.
//
// Taken and modified from own3dpro-obs-plugin
// Copyright (C) 2020 own3d media GmbH <support@own3d.tv>
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
#include <thread>
#include <mutex>
#include "version.h"

#include <obs.h>

#undef strtoll

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

// Common Global defines
/// Log Functionality
#if UI_ENABLED
#define DLOG_PREFIX "[NoiceUI]"
#else
#define DLOG_PREFIX "[Noice]"
#endif
#define DLOG_(level, ...) blog(level, DLOG_PREFIX " " __VA_ARGS__)
#define DLOG_ERROR(...) DLOG_(LOG_ERROR, __VA_ARGS__)
#define DLOG_WARNING(...) DLOG_(LOG_WARNING, __VA_ARGS__)
#define DLOG_INFO(...) DLOG_(LOG_INFO, __VA_ARGS__)
#define DLOG_DEBUG(...) DLOG_(LOG_DEBUG, __VA_ARGS__)

#if defined(_WIN32)
#define NOICE_USER_AGENT "Noice OBS Plugin/" PROJECT_VERSION " (Windows)"
#elif defined(__APPLE__)
#define NOICE_USER_AGENT "Noice OBS Plugin/" PROJECT_VERSION " (macOS)"
#elif defined(__linux__)
#define NOICE_USER_AGENT "Noice OBS Plugin/" PROJECT_VERSION " (Linux)"
#else
#define NOICE_USER_AGENT "Noice OBS Plugin/" PROJECT_VERSION " (Other)"
#endif

#define PP_STRINGIZE(ARG_) #ARG_

#if defined(__clang__)
#define COMPILER_WARNINGS_PUSH _Pragma(PP_STRINGIZE(clang diagnostic push))
#define COMPILER_WARNINGS_POP _Pragma(PP_STRINGIZE(clang diagnostic pop))
#define COMPILER_WARNINGS_DISABLE(Warn) _Pragma(PP_STRINGIZE(clang diagnostic ignored Warn))
#define COMPILER_CLANG 1
#elif defined(__GNUC__)
#define COMPILER_WARNINGS_PUSH _Pragma("GCC diagnostic push")
#define COMPILER_WARNINGS_POP _Pragma("GCC diagnostic pop")
#define COMPILER_WARNINGS_DISABLE(Warn) _Pragma(PP_STRINGIZE(GCC diagnostic ignored Warn))
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_WARNINGS_PUSH __pragma(warning(push))
#define COMPILER_WARNINGS_POP __pragma(warning(pop))
#define COMPILER_WARNINGS_DISABLE(Warn) __pragma(warning(disable : Warn))
#define COMPILER_MSVC 1
#endif

namespace noice {
inline void source_deleter(obs_source_t *v)
{
	obs_source_release(v);
}

inline void sceneitem_deleter(obs_sceneitem_t *v)
{
	obs_sceneitem_remove(v);
}

inline void data_deleter(obs_data_t *v)
{
	obs_data_release(v);
}

inline void data_item_deleter(obs_data_item_t *v)
{
	obs_data_item_release(&v);
}

inline void data_array_deleter(obs_data_array_t *v)
{
	obs_data_array_release(v);
}

class configuration {
	std::shared_ptr<obs_data_t> _data;
	std::mutex _lock;
	std::mutex _services_lock;
	std::mutex _streaming_active_lock;
	std::thread _task;
	bool _noice_service_selected;
	std::string _deployment;
	std::string _stream_key;
	bool _is_slobs;
	obs_service_t *_current_service_obj;
	signal_handler_t *_signal_handler;
	time_t _rtmp_services_json_ts;
	time_t _services_json_ts;
	time_t _regions_json_ts;
	bool _streaming_active;

public:
	virtual ~configuration();
	configuration();

	virtual std::shared_ptr<obs_data_t> get();

	virtual bool noice_service_selected() { return _noice_service_selected; };
	virtual bool streaming_active();
	virtual void set_streaming_active(bool active);
	virtual std::string deployment() { return _deployment; }
	virtual std::string stream_key() { return _stream_key; }
	virtual bool is_slobs() { return _is_slobs; }
	virtual bool can_update_source_names() { return _is_slobs == false; }

	virtual void probe_service_changed();

	virtual signal_handler_t *get_signal_handler() { return _signal_handler; };

	virtual void save();

	virtual void refresh(bool blocking = false);

	virtual bool patch_services_json();

private:
	void refresh_main(bool check);

	// Singleton
private:
	static std::shared_ptr<noice::configuration> _instance;

public:
	static void initialize();
	static void finalize();

	static std::shared_ptr<noice::configuration> instance();
};

std::string string_format(const std::string fmt, ...);

bool is_production();

const char *deployment_config_path_env(const char *file, const char *env);
const char *deployment_config_path(const char *file);
time_t deployment_config_ts(const char *file);
void deployment_config_unlink(const char *file);

std::string get_deployment_base_url(bool check_interface = true);

std::string get_api_endpoint(std::string_view const args = "");

std::string get_package_endpoint(std::string_view const args = "");

std::string get_web_endpoint(std::string_view const args = "");

std::string generate_uuid_v4();

std::string_view get_unique_identifier();

void reset_unique_identifier();
} // namespace noice
