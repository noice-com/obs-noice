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
#include "common.hpp"
#include <obs.h>
#include <graphics/matrix4.h>
#include <string>
#include <utility>
#include "obs/obs-source-factory.hpp"

namespace noice {
class game;
class region;
}

namespace noice::source {
class validator_instance;

class validator_factory : public obs::source_factory<noice::source::validator_factory, noice::source::validator_instance> {
public:
	validator_factory();
	virtual ~validator_factory();

	const char *get_name() override;

	void get_defaults2(obs_data_t *data) override;

	obs_properties_t *get_properties2(noice::source::validator_instance *data) override;
};

class validator_instance : public obs::source_instance {
	int _id;
	bool _refresh_sceneitem;
	bool _ignore_next_renamed_trigger;
	bool _sorted;

	uint64_t _last_time;

	std::shared_ptr<std::map<std::string, bool>> _main_video_sources;

	std::string _game_name;
	std::shared_ptr<noice::game> _game;

	obs_video_info _ovi;

	bool _draw_all_regions;
	bool _debug_sources;

	struct vec4 _color_region[2];
	struct vec4 _color_source[2];
	struct vec4 _color_source_collides[2];
	bool _linear_srgb;

	struct obs_transform_info _info;
	struct obs_sceneitem_crop _crop;
	struct matrix4 _parent_transform;

	obs_source_t *_source;
	std::string _source_guid;
	obs_weak_source_t *_current_enum_scene;

	friend class validator_factory;

private:
	void update_current_enum_scene();

	obs_scene_t *current_enum_scene();

	bool content_settings_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings);

	bool sceneitem_has_canvas_coverage(obs_sceneitem_t *item, int coverage_requirement);

	bool sceneitem_is_main_video_source(obs_sceneitem_t *item);

	void region_validate(noice::region &region, obs_sceneitem_t *item, int &hits);

	void region_draw(noice::region &region);

	void source_draw(obs_sceneitem_t *item);

	void update_game_prop(obs_property_t *prop);

	bool update_hud_scale_prop(obs_property_t *prop);

	bool validate_game_name_availability(obs_data_t *data);

	bool try_source_candidate_name(std::string candidate);

public:
	validator_instance(obs_data_t *, obs_source_t *);
	virtual ~validator_instance();

	void load(obs_data_t *data) override;

	void migrate(obs_data_t *data, std::uint64_t version) override;

	void update(obs_data_t *data) override;

	void save(obs_data_t *data) override;

	void activate() override;

	void deactivate() override;

	void show() override;

	void hide() override;

	std::uint32_t get_width() override;

	std::uint32_t get_height() override;

	void video_tick(float_t seconds) override;

	void video_render(gs_effect_t *effect) override;

	void sceneitem_set_name(bool deferred = false);

	void sceneitem_set_transform(obs_sceneitem_t *item);

	void sceneitem_set_position(obs_sceneitem_t *item, int pass);

public:
	static void sort_sceneitems(obs_scene_t *scene);
};
} // namespace noice::source
