// Copyright (C) 2023 Noice Inc.
//
// Taken and modified from own3dpro-obs-plugin, where the code is
// based on plugins/obs-browser/panel/browser-panel.hpp @ OBS
//
// Copyright (C) 2021 own3d media GmbH <support@own3d.tv>
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

#include "obs-browser.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <stdexcept>

#if defined(__APPLE__)
#include <dlfcn.h>
#endif

#ifdef ENABLE_WAYLAND
#include <obs-nix-platform.h>
#endif

void *get_browser_lib()
{
	// Disable panels on Wayland for now
	bool isWayland = false;
#ifdef ENABLE_WAYLAND
	isWayland = obs_get_nix_platform() == OBS_NIX_PLATFORM_WAYLAND;
#endif
	if (isWayland) {
		throw std::runtime_error("Noice: Wayland is not supported.");
		return nullptr;
	}

	obs_module_t *browserModule = obs_get_module("obs-browser");

	if (!browserModule) {
		throw std::runtime_error("Noice: Cannot get obs-browser module.");
		return nullptr;
	}

	return obs_get_module_lib(browserModule);
}

QCef *obs::browser::instance()
{
	void *lib = get_browser_lib();
	QCef *(*create_qcef)(void) = nullptr;

	if (!lib) {
		throw std::runtime_error("Noice: Cannot get obs-browser lib for instance.");
		return nullptr;
	}

	create_qcef = (decltype(create_qcef))os_dlsym(lib, "obs_browser_create_qcef");

	if (!create_qcef) {
		throw std::runtime_error("Noice: Cannot create qcef.");
		return nullptr;
	}

	return create_qcef();
}

int obs::browser::version()
{
	void *lib = get_browser_lib();
	int (*qcef_version)(void) = nullptr;

	if (!lib) {
		throw std::runtime_error("Noice: Cannot get obs-browser lib for version.");
		return 0;
	}

	qcef_version = (decltype(qcef_version))os_dlsym(lib, "obs_browser_qcef_version_export");

	if (!qcef_version) {
		throw std::runtime_error("Noice: Cannot get qcef version.");
		return 0;
	}

	return qcef_version();
}
