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

#include "noice-bridge.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <stdexcept>

#if NOICE_CORE

noice::bridge::~bridge() {}

noice::bridge::bridge() {}

std::shared_ptr<noice::configuration> noice::bridge::configuration_instance()
{
	return noice::configuration::instance();
}

std::shared_ptr<noice::source::scene_tracker> noice::bridge::scene_tracker_instance()
{
	return noice::source::scene_tracker::instance();
}

std::shared_ptr<noice::game_manager> noice::bridge::game_manager_instance()
{
	return noice::game_manager::instance();
}

std::shared_ptr<noice::auth> noice::bridge::auth_instance()
{
	return noice::auth::instance();
}

std::string noice::bridge::get_web_endpoint(std::string_view const args)
{
	return noice::get_web_endpoint(args);
}

std::string_view noice::bridge::get_unique_identifier()
{
	return noice::get_unique_identifier();
}

std::shared_ptr<noice::bridge> noice::bridge::_instance = nullptr;

void noice::bridge::initialize()
{
	if (!noice::bridge::_instance)
		noice::bridge::_instance = std::make_shared<noice::bridge>();
}

void noice::bridge::finalize()
{
	noice::bridge::_instance = nullptr;
}

std::shared_ptr<noice::bridge> noice::bridge::instance()
{
	return noice::bridge::_instance;
}

extern "C" EXPORT noice::bridge *noice_get_bridge(void)
{
	return noice::bridge::instance().get();
}

noice::bridge *noice::get_bridge()
{
	return noice_get_bridge();
}

#else

static void *get_noice_lib()
{
	obs_module_t *noiceModule = obs_get_module("noice");

	if (!noiceModule) {
		throw std::runtime_error("Noice: Cannot get noice module.");
		return nullptr;
	}

	return obs_get_module_lib(noiceModule);
}

noice::bridge *noice::get_bridge()
{
	void *lib = get_noice_lib();
	noice::bridge *(*get_bridge)(void) = nullptr;

	if (!lib) {
		throw std::runtime_error("Noice: Cannot get noice lib for instance.");
		return nullptr;
	}

	get_bridge = (decltype(get_bridge))os_dlsym(lib, "noice_get_bridge");
	if (!get_bridge) {
		throw std::runtime_error("Noice: Cannot find bridge instance.");
		return nullptr;
	}

	return get_bridge();
}

#endif
