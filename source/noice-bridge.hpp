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
#include "scene-tracker.hpp"
#include "game.hpp"

namespace noice {
class bridge {
public:
	virtual ~bridge();
	bridge();

	virtual std::shared_ptr<noice::configuration> configuration_instance();
	virtual std::shared_ptr<noice::source::scene_tracker> scene_tracker_instance();
	virtual std::shared_ptr<noice::game_manager> game_manager_instance();

	virtual std::string get_web_endpoint(std::string_view const args = "");
	virtual std::string_view get_unique_identifier();

	// Singleton
private:
	static std::shared_ptr<noice::bridge> _instance;

public:
	static void initialize();
	static void finalize();

	static std::shared_ptr<noice::bridge> instance();
};

noice::bridge *get_bridge();
} // namespace noice
