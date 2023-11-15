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

#include <obs-module.h>
#include <stdexcept>
#include "version.h"
#include "common.hpp"
#include "game.hpp"
#include "noice-validator.hpp"
#include "scene-tracker.hpp"
#include "noice-bridge.hpp"
#include "obs-bridge.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Noice");
OBS_MODULE_USE_DEFAULT_LOCALE("noice", "en-US")

MODULE_EXPORT bool obs_module_load()
{
	// Dumb way to avoid loading plugin from multiple install locations
	bool added = signal_handler_add(obs_get_signal_handler(), "void noice_loaded()");
	if (!added)
		return false;

	try {
		obs::bridge::initialize();
		noice::bridge::initialize();
		noice::game_manager::initialize();
		noice::configuration::initialize();
		noice::configuration::instance()->refresh();

		// Retrieve unique Machine Id.
		noice::get_unique_identifier();

		noice::source::scene_tracker::initialize();

		{
			static auto validators = std::make_shared<noice::source::validator_factory>();
		}
		return true;
	} catch (const std::exception &ex) {
		DLOG_ERROR("Failed to load plugin due to error: %s", ex.what());
		return false;
	} catch (...) {
		DLOG_ERROR("Failed to load plugin.");
		return false;
	}

	return true;
}

MODULE_EXPORT void obs_module_unload()
{
	DLOG_INFO("Unloading");

	try {
		noice::source::scene_tracker::finalize();
		noice::configuration::finalize();
		noice::game_manager::finalize();
		noice::bridge::finalize();
		obs::bridge::finalize();
	} catch (...) {
		DLOG_ERROR("Failed to unload plugin.");
	}
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Plugin.Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("Plugin.Name");
}

#ifdef _WIN32 // Windows Only
#include <Windows.h>

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		std::string mtx_name = obs_module_name();
		std::string mtx_name2 = "Global\\" + mtx_name;
		CreateMutexA(NULL, FALSE, mtx_name.c_str());
		CreateMutexA(NULL, FALSE, mtx_name2.c_str());
	}

	return TRUE;
}
#endif
