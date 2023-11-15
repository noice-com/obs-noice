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

#include "obs-bridge.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <stdexcept>

#if defined(_WIN32)
#define LIBOBS_NAME "obs.dll"
#elif defined(__APPLE__)
#define LIBOBS_NAME "libobs.dylib"
#else
#define LIBOBS_NAME "libobs.so"
#endif

obs::bridge::~bridge()
{
	if (_module) {
		os_dlclose(_module);
		_module = nullptr;
	}
}

obs::bridge::bridge() : _module(nullptr), _is_slobs(false), _obs_view_add2(nullptr)
{
	_module = os_dlopen(LIBOBS_NAME);

	void *stream_probe = os_dlsym(_module, "obs_render_streaming_texture");
	void *record_probe = os_dlsym(_module, "obs_render_recording_texture");
	_is_slobs = (stream_probe != nullptr && record_probe != nullptr);

	// Probe if the active OBS build already has a signal created
	_has_create_signals = !signal_handler_add(obs_get_signal_handler(), "void output_create(ptr output)");

	_find_output = (decltype(_find_output))os_dlsym(_module, "find_output");

	_obs_view_add = (decltype(_obs_view_add))os_dlsym(_module, "obs_view_add");
	_obs_view_add2 = (decltype(_obs_view_add2))os_dlsym(_module, "obs_view_add2");
	_obs_view_remove = (decltype(_obs_view_remove))os_dlsym(_module, "obs_view_remove");
}

std::shared_ptr<obs::bridge> obs::bridge::_instance = nullptr;

void obs::bridge::initialize()
{
	if (!obs::bridge::_instance)
		obs::bridge::_instance = std::make_shared<obs::bridge>();
}

void obs::bridge::finalize()
{
	obs::bridge::_instance = nullptr;
}

std::shared_ptr<obs::bridge> obs::bridge::instance()
{
	return obs::bridge::_instance;
}
