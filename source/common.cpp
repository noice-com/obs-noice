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

#include "common.hpp"
#include <obs-module.h>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <random>
#include <obs-module.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include "file-updater/file-updater.hpp"
#include "obs-bridge.hpp"
#include "game.hpp"
#include "version.h"

#define NOICE_DEPLOYMENT_PRD "prd"
#define NOICE_DEPLOYMENT_STG "stg"
#define NOICE_DEPLOYMENT_DEV "dev"

constexpr std::string_view CFG_UNIQUE_ID = "unique_id";
constexpr std::string_view CFG_DEPLOYMENT = "deployment";

static const char *configuration_signals[] = {
	"void service(bool deployment_changed)",
	NULL,
};

noice::configuration::~configuration()
{
	// Forcefully rejoin with the thread if any is active.
	if (_task.joinable())
		_task.join();

	signal_handler_destroy(_signal_handler);
	save();
}

noice::configuration::configuration()
	: _lock(),
	  _services_lock(),
	  _streaming_active_lock(),
	  _task(),
	  _noice_service_selected(false),
	  _deployment(NOICE_DEPLOYMENT_PRD),
	  _is_slobs(obs::bridge::instance()->is_slobs()),
	  _current_service_obj(nullptr),
	  _signal_handler(signal_handler_create()),
	  _rtmp_services_json_ts(-1),
	  _services_json_ts(-1),
	  _regions_json_ts(-1),
	  _streaming_active(false)
{
	DLOG_INFO("Loading. Plugin version %s, %sobs version: %s", PROJECT_VERSION, _is_slobs ? "sl" : "", obs_get_version_string());

	const char *conf_path = obs_module_config_path("");
	os_mkdir(conf_path);
	bfree((void *)conf_path);

	const char *conf = obs_module_config_path("config.json");
	if (os_file_exists(conf)) {
		_data = std::shared_ptr<obs_data_t>(obs_data_create_from_json_file_safe(conf, ".bk"), ::noice::data_deleter);
	} else {
		_data = std::shared_ptr<obs_data_t>(obs_data_create(), ::noice::data_deleter);
	}
	bfree((void *)conf);

	obs_data_set_default_string(_data.get(), CFG_DEPLOYMENT.data(), NOICE_DEPLOYMENT_PRD);
	_deployment = obs_data_get_string(_data.get(), CFG_DEPLOYMENT.data());

	signal_handler_add_array(_signal_handler, configuration_signals);
}

std::shared_ptr<obs_data_t> noice::configuration::get()
{
	obs_data_addref(_data.get());
	return std::shared_ptr<obs_data_t>(_data.get(), ::noice::data_deleter);
}

static std::string ValueOrEmpty(const char *s)
{
	return s == nullptr ? std::string() : s;
}

void noice::configuration::set_streaming_active(bool active)
{
	std::unique_lock<std::mutex> lock(_streaming_active_lock);

	_streaming_active = active;
}

bool noice::configuration::streaming_active()
{
	std::unique_lock<std::mutex> lock(_streaming_active_lock);

	return _streaming_active;
}

void noice::configuration::probe_service_changed()
{
	// obs_frontend_get_streaming_service equivalent
	obs_service_t *service_obj = obs_get_service_by_name("default_service");
	obs_service_release(service_obj);

	if (!service_obj || (service_obj && _current_service_obj == service_obj))
		return;

	bool initializing = _current_service_obj == nullptr;
	_current_service_obj = service_obj;
	obs_data_t *settings = obs_service_get_settings(service_obj);
	std::string service = ValueOrEmpty(obs_data_get_string(settings, "service"));
	std::string url = ValueOrEmpty(obs_service_get_connect_info(service_obj, OBS_SERVICE_CONNECT_INFO_SERVER_URL));

	std::string prev_deployment = _deployment;
	_noice_service_selected = (url.find(".noice.com") != std::string::npos) ? true : service.find("Noice") == 0;

	if (_noice_service_selected) {
		if (url.find(".dev.") != std::string::npos)
			_deployment = NOICE_DEPLOYMENT_DEV;
		else if (url.find(".stg.") != std::string::npos)
			_deployment = NOICE_DEPLOYMENT_STG;
		else
			_deployment = NOICE_DEPLOYMENT_PRD;

		const char *svc_key = obs_service_get_connect_info(service_obj, OBS_SERVICE_CONNECT_INFO_STREAM_ID);
		_stream_key = std::string(svc_key);
	} else {
		_stream_key = std::string("");
	}
	bool deployment_changed = prev_deployment != _deployment;
	obs_data_set_string(_data.get(), CFG_DEPLOYMENT.data(), _deployment.c_str());

	DLOG_INFO("Service changed: %s / %s", service.c_str(), url.c_str());
	if (_noice_service_selected) {
		const char *config_path = noice::deployment_config_path("");
		DLOG_INFO("Deployment: %s (deployment_changed: %d)", _deployment.c_str(), deployment_changed);
		DLOG_INFO("Config path: %s", config_path);
		bfree((void *)config_path);
	}
	obs_data_release(settings);

	if (deployment_changed || initializing || (_services_json_ts < 0 || _regions_json_ts < 0)) {
		_rtmp_services_json_ts = -1;
		refresh(true);
	}

	struct calldata data;
	calldata_init(&data);
	calldata_set_bool(&data, "deployment_changed", deployment_changed);
	signal_handler_signal(_signal_handler, "service", &data);
	calldata_free(&data);
}

void noice::configuration::save()
{
	const char *file = obs_module_config_path("config.json");
	obs_data_save_json_safe(_data.get(), file, ".tmp", ".bk");
	bfree((void *)file);
}

void noice::configuration::refresh(bool blocking)
{
	std::unique_lock<std::mutex> lock(_lock);
	bool check = true;

	// Forcefully rejoin with the thread if any is active.
	if (_task.joinable()) {
		_task.join();
		check = false;
	}

	time_t services_ts = noice::deployment_config_ts("services.json");
	time_t regions_ts = noice::deployment_config_ts("regions.json");

	if (services_ts < 0 || regions_ts < 0) {
		// Eh, ensure download if someone has partially removed files
		noice::deployment_config_unlink("package.json");
		noice::deployment_config_unlink("meta.json");
		blocking = true;
		check = true;
	}

	// Spawn a new task to check for updates.
	_task = std::thread{std::bind(&noice::configuration::refresh_main, this, check)};

	if (blocking) {
		if (_task.joinable())
			_task.join();
	}
}

static bool verify_download_file(void *param, struct file_download_data *file)
{
	// Only do basic verification for input
	if (astrcmpi(file->name, "services.json") == 0) {
		std::stringstream stream((char *)file->buffer.array);

		try {
			nlohmann::json data = nlohmann::json::parse(stream);

			nlohmann::json services = data["services"];
			if (!services.is_array())
				return false;
		} catch (std::exception const &ex) {
			DLOG_ERROR("%s", ex.what());
			return false;
		} catch (...) {
			DLOG_ERROR("unknown error occurred");
			return false;
		}
	} else if (astrcmpi(file->name, "regions.json") == 0) {
		std::stringstream stream((char *)file->buffer.array);

		try {
			nlohmann::json data = nlohmann::json::parse(stream);

			nlohmann::json games = data["games"];
			if (!games.is_array())
				return false;
		} catch (std::exception const &ex) {
			DLOG_ERROR("%s", ex.what());
			return false;
		} catch (...) {
			DLOG_ERROR("unknown error occurred");
			return false;
		}
	}

	UNUSED_PARAMETER(param);
	return true;
}

void noice::configuration::refresh_main(bool check)
{
	const char *local_dir = obs_module_file("");
	const char *cache_dir = noice::deployment_config_path("");
	std::string update_url = noice::get_package_endpoint("");

	if (cache_dir && check) {
		update_info_t *update_info = update_info_create(DLOG_PREFIX " ", NOICE_USER_AGENT, update_url.c_str(), local_dir, cache_dir,
								verify_download_file, nullptr);
		update_info_destroy(update_info);
	}

	bfree((void *)local_dir);
	bfree((void *)cache_dir);

	time_t services_ts = noice::deployment_config_ts("services.json");
	if (_services_json_ts != services_ts) {
		if (patch_services_json())
			_services_json_ts = services_ts;
	}

	time_t regions_ts = noice::deployment_config_ts("regions.json");
	if (_regions_json_ts != regions_ts) {
		_regions_json_ts = regions_ts;
		noice::game_manager::instance()->refresh();
	}
}

static time_t get_modified_timestamp(const char *filename)
{
	struct stat stats;
	if (os_stat(filename, &stats) != 0)
		return -1;
	return stats.st_mtime;
}

bool noice::configuration::patch_services_json()
{
	std::unique_lock<std::mutex> lock(_services_lock);

	obs_module_t *rtmp = obs_get_module("rtmp-services");
	if (rtmp == nullptr)
		return false;

	const char *rtmp_services_json = obs_module_get_config_path(rtmp, "services.json");
	time_t rtmp_ts = get_modified_timestamp(rtmp_services_json);

	// Something might have modified the file after our last visit?
	if (_rtmp_services_json_ts == rtmp_ts) {
		bfree((void *)rtmp_services_json);
		return false;
	}

	std::vector<std::string> deployments{NOICE_DEPLOYMENT_DEV, NOICE_DEPLOYMENT_STG, NOICE_DEPLOYMENT_PRD};
	bool ret = false;

	try {
		std::ifstream rtmp_stream(rtmp_services_json, std::ios::in);
		nlohmann::ordered_json rtmp_data = nlohmann::ordered_json::parse(rtmp_stream);
		rtmp_stream.close();

		nlohmann::ordered_json &rtmp_services = rtmp_data["services"];
		if (!rtmp_services.is_array())
			throw std::runtime_error("No services array");

		nlohmann::ordered_json rtmp_services_copy;
		rtmp_services_copy.merge_patch(rtmp_services);

		for (auto deployment : deployments) {
			const char *services_json = noice::deployment_config_path_env("services.json", deployment.c_str());
			if (!os_file_exists(services_json)) {
				bfree((void *)services_json);
				continue;
			}

			std::ifstream stream(services_json, std::ios::in);
			bfree((void *)services_json);
			nlohmann::ordered_json data = nlohmann::ordered_json::parse(stream);
			stream.close();

			nlohmann::ordered_json services = data["services"];
			if (!services.is_array())
				throw std::runtime_error("No services array");

			std::vector<std::string> names;
			for (const auto &service : services) {
				std::string name = service["name"].get<std::string>();
				names.push_back(name);

				if (service.contains("alt_names")) {
					auto alt_names = service.at("alt_names").get<std::vector<std::string>>();
					names.insert(names.end(), alt_names.begin(), alt_names.end());
				}
			}

			// Remove any existing services from our input, including alt names
			for (auto it = rtmp_services.begin(); it != rtmp_services.end();) {
				auto name = it->at("name").get<std::string>();
				bool match = std::find(names.begin(), names.end(), name) != names.end();

				if (match) {
					it = rtmp_services.erase(it);
				} else {
					++it;
				}
			}

			// Push to front of the list
			for (const auto &service : services)
				rtmp_services.insert(rtmp_services.begin(), service);
		}

		// Save if modified
		if (rtmp_services != rtmp_services_copy) {
			std::ofstream output_file(rtmp_services_json);
			output_file << rtmp_data;
			DLOG_INFO("Successfully updated services.json");
		}
		ret = true;
	} catch (std::exception const &ex) {
		DLOG_ERROR("%s", ex.what());
	} catch (...) {
		DLOG_ERROR("unknown error occurred");
	}

	_rtmp_services_json_ts = get_modified_timestamp(rtmp_services_json);

	bfree((void *)rtmp_services_json);
	return ret;
}

std::shared_ptr<noice::configuration> noice::configuration::_instance = nullptr;

void noice::configuration::initialize()
{
	if (!noice::configuration::_instance)
		noice::configuration::_instance = std::make_shared<noice::configuration>();
}

void noice::configuration::finalize()
{
	noice::configuration::_instance = nullptr;
}

std::shared_ptr<noice::configuration> noice::configuration::instance()
{
	return noice::configuration::_instance;
}

std::string noice::string_format(const std::string fmt, ...)
{
	int size = ((int)fmt.size()) * 2 + 50;
	std::string str;
	va_list ap;
	while (1) {
		str.resize(size);
		va_start(ap, fmt);

		COMPILER_WARNINGS_PUSH
#if COMPILER_MSVC
		COMPILER_WARNINGS_DISABLE(4774)
#else
		COMPILER_WARNINGS_DISABLE("-Wformat-nonliteral")
#endif
		int n = std::vsnprintf((char *)str.data(), size, fmt.c_str(), ap);
		COMPILER_WARNINGS_POP
		va_end(ap);
		if (n > -1 && n < size) {
			str.resize(n);
			return str;
		}
		if (n > -1)
			size = n + 1;
		else
			size *= 2;
	}
	return str;
}

bool noice::is_production()
{
	auto cfg = noice::configuration::instance();
	return cfg->deployment() == NOICE_DEPLOYMENT_PRD;
}

const char *noice::deployment_config_path_env(const char *file, const char *env)
{
	const char *ret = nullptr;

	if (astrcmpi(env, NOICE_DEPLOYMENT_PRD) == 0)
		ret = obs_module_config_path(file);
	else {
		std::string path = string_format("%s/%s", env, file);
		ret = obs_module_config_path(path.c_str());
	}
	return ret;
}

const char *noice::deployment_config_path(const char *file)
{
	auto cfg = noice::configuration::instance();
	return noice::deployment_config_path_env(file, cfg->deployment().c_str());
}

time_t noice::deployment_config_ts(const char *file)
{
	const char *f = deployment_config_path(file);
	time_t ts = get_modified_timestamp(f);
	bfree((void *)f);
	return ts;
}

void noice::deployment_config_unlink(const char *file)
{
	const char *f = deployment_config_path(file);
	os_unlink(f);
	bfree((void *)f);
}

std::string noice::get_deployment_base_url(bool check_interface)
{
	constexpr std::string_view KEY = "deployment_base_url";
	auto cfg = noice::configuration::instance();
	auto data = cfg->get();
	std::string config_url = obs_data_get_string(data.get(), KEY.data());
	std::string iface_str = (check_interface == false || is_production()) ? "" : "int.";

	if (config_url.empty()) {
		return string_format("%s%s.%s", iface_str.c_str(), cfg->deployment().c_str(), "noice.com");
	}
	return config_url;
}

std::string noice::get_api_endpoint(std::string_view const args)
{
	std::string endpoint = get_deployment_base_url(true);
	return string_format("https://platform.%s/%s", endpoint.c_str(), args.data());
}

std::string noice::get_package_endpoint(std::string_view const args)
{
	std::string endpoint = get_deployment_base_url(false);
	return string_format("http://obs-config.%s/v1/%s", endpoint.c_str(), args.data());
}

std::string noice::get_web_endpoint(std::string_view const args /*= ""*/)
{
	std::string endpoint = get_deployment_base_url(true);
	return string_format("https://mvp.%s/%s", endpoint.c_str(), args.data());
}

static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_int_distribution<> dis(0, 15);
static std::uniform_int_distribution<> dis2(8, 11);

std::string noice::generate_uuid_v4()
{
	std::stringstream ss;
	int i;

	ss << std::hex;
	for (i = 0; i < 8; i++) {
		ss << dis(gen);
	}
	ss << "-";
	for (i = 0; i < 4; i++) {
		ss << dis(gen);
	}
	ss << "-4";
	for (i = 0; i < 3; i++) {
		ss << dis(gen);
	}
	ss << "-";
	ss << dis2(gen);
	for (i = 0; i < 3; i++) {
		ss << dis(gen);
	}
	ss << "-";
	for (i = 0; i < 12; i++) {
		ss << dis(gen);
	};
	return ss.str();
}

std::string_view noice::get_unique_identifier()
{
	auto data = noice::configuration::instance()->get();
	std::string_view id = obs_data_get_string(data.get(), CFG_UNIQUE_ID.data());

	if (id.length() == 0) { // Id is invalid, request a new one.
		std::string idv = generate_uuid_v4();
		if (idv.length() > 0) {
			obs_data_set_string(data.get(), CFG_UNIQUE_ID.data(), idv.c_str());
			id = obs_data_get_string(data.get(), CFG_UNIQUE_ID.data());
			noice::configuration::instance()->save();
			DLOG_INFO("Acquired unique machine token.");
		} else {
			DLOG_ERROR("Failed to acquire machine token.");
		}
	}

	return id;
}

void noice::reset_unique_identifier()
{
	obs_data_unset_user_value(noice::configuration::instance()->get().get(), CFG_UNIQUE_ID.data());
	noice::configuration::instance()->save();
}
