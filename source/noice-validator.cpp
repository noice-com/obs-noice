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

#include "noice-validator.hpp"
#include "scene-tracker.hpp"
#include "game.hpp"
#include <algorithm>
#include <obs-module.h>

#define DLOG_CTX_(x, level, format, ...) blog(level, DLOG_PREFIX " [%p] id: %d %s: " format, x->_source, x->_id, __FUNCTION__, __VA_ARGS__)
#define DLOG_CTX_ERROR(x, format, ...) DLOG_CTX_(x, LOG_ERROR, format, __VA_ARGS__)
#define DLOG_CTX_WARNING(x, format, ...) DLOG_CTX_(x, LOG_WARNING, format, __VA_ARGS__)
#define DLOG_CTX_INFO(x, format, ...) DLOG_CTX_(x, LOG_INFO, format, __VA_ARGS__)
#define DLOG_CTX_DEBUG(x, format, ...) DLOG_CTX_(x, LOG_DEBUG, format, __VA_ARGS__)

// Default for now
#ifndef TRACE_CALLS
#define TRACE_CALLS 1
#endif

#if TRACE_CALLS == 1
#define CALL_ENTRY2(x, msg) DLOG_CTX_INFO(x, "%s", msg)
#else
#define CALL_ENTRY2(x, msg) (void)
#endif
#define CALL_ENTRY(x) CALL_ENTRY2(x, "called")

#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC 1000000
#endif

static int noice_validator_uniq_rt_count = 0;
static int refresh_game_list_counter = 0;

static constexpr std::string_view NOICE_VALIDATOR_SOURCE_NAME_PREFIX = "Noice Validator";

const char *NOICE_VALIDATOR_PLUGIN_ID = "noice_validator";

#pragma mark Utilities from OBS at UI/window-basic-preview.cpp

#define HANDLE_RADIUS 12.0f

#if 0
static void DrawSquareAtPos(float x, float y)
{
	struct vec3 pos;
	vec3_set(&pos, x, y, 0.0f);

	struct matrix4 matrix;
	gs_matrix_get(&matrix);
	vec3_transform(&pos, &pos, &matrix);

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_translate3f(-HANDLE_RADIUS, -HANDLE_RADIUS, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * 2, HANDLE_RADIUS * 2, 1.0f);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
}

static void DrawLine(float x1, float y1, float x2, float y2, float thickness, vec2 scale)
{
	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	gs_render_start(true);

	gs_vertex2f(x1, y1);
	gs_vertex2f(x1 + (xSide * (thickness / scale.x)), y1 + (ySide * (thickness / scale.y)));
	gs_vertex2f(x2, y2);
	gs_vertex2f(x2 + (xSide * (thickness / scale.x)), y2 + (ySide * (thickness / scale.y)));

	gs_vertbuffer_t *line = gs_render_save();

	gs_load_vertexbuffer(line);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(line);
}
#endif

static void DrawRect(float thickness, vec2 scale)
{
	gs_render_start(true);

	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 0.0f);
	gs_vertex2f(0.0f, 1.0f);
	gs_vertex2f(0.0f + (thickness / scale.x), 1.0f);
	gs_vertex2f(0.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f, 1.0f);
	gs_vertex2f(1.0f, 1.0f - (thickness / scale.y));
	gs_vertex2f(1.0f - (thickness / scale.x), 1.0f);
	gs_vertex2f(1.0f, 0.0f);
	gs_vertex2f(1.0f - (thickness / scale.x), 0.0f);
	gs_vertex2f(1.0f, 0.0f + (thickness / scale.y));
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(0.0f, 0.0f + (thickness / scale.y));

	gs_vertbuffer_t *rect = gs_render_save();

	gs_load_vertexbuffer(rect);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_vertexbuffer_destroy(rect);
}

static bool CloseFloat(float a, float b, float epsilon = 0.01)
{
	using std::abs;
	return abs(a - b) <= epsilon;
}

static bool CounterClockwise(float x1, float x2, float x3, float y1, float y2, float y3)
{
	return (y3 - y1) * (x2 - x1) > (y2 - y1) * (x3 - x1);
}

static bool IntersectLine(float x1, float x2, float x3, float x4, float y1, float y2, float y3, float y4)
{
	bool a = CounterClockwise(x1, x2, x3, y1, y2, y3);
	bool b = CounterClockwise(x1, x2, x4, y1, y2, y4);
	bool c = CounterClockwise(x3, x4, x1, y3, y4, y1);
	bool d = CounterClockwise(x3, x4, x2, y3, y4, y2);

	return (a != b) && (c != d);
}

static bool IntersectBox(matrix4 transform, float x1, float x2, float y1, float y2)
{
	float x3, x4, y3, y4;

	x3 = transform.t.x;
	y3 = transform.t.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.x.x;
	y3 = transform.t.y + transform.x.y;
	x4 = x3 + transform.y.x;
	y4 = y3 + transform.y.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	x3 = transform.t.x + transform.y.x;
	y3 = transform.t.y + transform.y.y;
	x4 = x3 + transform.x.x;
	y4 = y3 + transform.x.y;

	if (IntersectLine(x1, x1, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y1, y1, y3, y4) ||
	    IntersectLine(x2, x2, x3, x4, y1, y2, y3, y4) || IntersectLine(x1, x2, x3, x4, y2, y2, y3, y4))
		return true;

	return false;
}

static bool FindItemsInBox(obs_sceneitem_t *item, vec2 startPos, vec2 pos, struct matrix4 *parent_transform = nullptr)
{
	matrix4 transform;
	matrix4 invTransform;
	vec3 transformedPos;
	vec3 pos3;
	vec3 pos3_;

	vec2 pos_min, pos_max;
	vec2_min(&pos_min, &startPos, &pos);
	vec2_max(&pos_max, &startPos, &pos);

	const float x1 = pos_min.x;
	const float x2 = pos_max.x;
	const float y1 = pos_min.y;
	const float y2 = pos_max.y;

	vec3_set(&pos3, pos.x, pos.y, 0.0f);
	obs_sceneitem_get_box_transform(item, &transform);

	if (parent_transform)
		matrix4_mul(&transform, &transform, parent_transform);

	matrix4_inv(&invTransform, &transform);
	vec3_transform(&transformedPos, &pos3, &invTransform);
	vec3_transform(&pos3_, &transformedPos, &transform);

	if (CloseFloat(pos3.x, pos3_.x) && CloseFloat(pos3.y, pos3_.y) && transformedPos.x >= 0.0f && transformedPos.x <= 1.0f &&
	    transformedPos.y >= 0.0f && transformedPos.y <= 1.0f) {
		return true;
	}

	if (transform.t.x > x1 && transform.t.x < x2 && transform.t.y > y1 && transform.t.y < y2) {
		return true;
	}

	if (transform.t.x + transform.x.x > x1 && transform.t.x + transform.x.x < x2 && transform.t.y + transform.x.y > y1 &&
	    transform.t.y + transform.x.y < y2) {
		return true;
	}

	if (transform.t.x + transform.y.x > x1 && transform.t.x + transform.y.x < x2 && transform.t.y + transform.y.y > y1 &&
	    transform.t.y + transform.y.y < y2) {
		return true;
	}

	if (transform.t.x + transform.x.x + transform.y.x > x1 && transform.t.x + transform.x.x + transform.y.x < x2 &&
	    transform.t.y + transform.x.y + transform.y.y > y1 && transform.t.y + transform.x.y + transform.y.y < y2) {
		return true;
	}

	if (transform.t.x + 0.5 * (transform.x.x + transform.y.x) > x1 && transform.t.x + 0.5 * (transform.x.x + transform.y.x) < x2 &&
	    transform.t.y + 0.5 * (transform.x.y + transform.y.y) > y1 && transform.t.y + 0.5 * (transform.x.y + transform.y.y) < y2) {
		return true;
	}

	if (IntersectBox(transform, x1, x2, y1, y2)) {
		return true;
	}

	return false;
}

static bool SceneItemHasVideo(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_VIDEO) != 0;
}

static vec2 GetItemSize(obs_sceneitem_t *item)
{
	obs_bounds_type boundsType = obs_sceneitem_get_bounds_type(item);
	vec2 size;

	if (boundsType != OBS_BOUNDS_NONE) {
		obs_sceneitem_get_bounds(item, &size);
	} else {
		obs_source_t *source = obs_sceneitem_get_source(item);
		obs_sceneitem_crop crop;
		vec2 scale;

		obs_sceneitem_get_scale(item, &scale);
		obs_sceneitem_get_crop(item, &crop);
		size.x = float(obs_source_get_width(source) - crop.left - crop.right) * scale.x;
		size.y = float(obs_source_get_height(source) - crop.top - crop.bottom) * scale.y;
	}

	return size;
}

#pragma mark Utilities

static void GetItemSizeRotated(vec2 &pos, vec2 &size, float rot)
{
	float ang = rot * (M_PI / 180.0f);

	float sinA = sin(ang);
	float cosA = cos(ang);

	float sinAA = fabs(sinA);
	float cosAA = fabs(cosA);

	float bbH = size.x * sinAA + size.y * cosAA;
	float bbW = size.x * cosAA + size.y * sinAA;

	float cx = pos.x + size.x / 2 * cosA - size.y / 2 * sinA;
	float cy = pos.y + size.x / 2 * sinA + size.y / 2 * cosA;

	float bbX = cx - bbW / 2;
	float bbY = cy - bbH / 2;

	vec2_set(&pos, bbX, bbY);
	vec2_set(&size, bbW, bbH);
}

bool noice::source::validator_instance::sceneitem_has_canvas_coverage(obs_sceneitem_t *item, int coverage_requirement)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	std::string id = obs_source_get_unversioned_id(source);

	// Base canvas size
	vec2 cpos;
	vec2_set(&cpos, 0.0f, 0.0f);
	vec2 csize;
	vec2_set(&csize, (float)_ovi.base_width, (float)_ovi.base_height);

	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	vec2 size = GetItemSize(item);

	float rot = obs_sceneitem_get_rot(item);
	if (rot != 0.0f)
		GetItemSizeRotated(pos, size, rot);

	// Flipped
	if (size.x < 0) {
		size.x = abs(size.x);
		pos.x -= size.x;
	}
	if (size.y < 0) {
		size.y = abs(size.y);
		pos.y -= size.y;
	}

	// Clamp outside canvas
	if (pos.x < 0.0f) {
		size.x += pos.x;
		pos.x = 0.0f;
	}
	if (pos.y < 0.0f) {
		size.y += pos.y;
		pos.y = 0.0f;
	}
	if (size.x > csize.x)
		size.x = csize.x;
	if (size.y > csize.y)
		size.y = csize.y;

	float dist_x = (fminf(size.x, csize.x) - fmaxf(size.x + pos.x, csize.x + cpos.x)) + csize.x;
	float dist_y = (fminf(size.y, csize.y) - fmaxf(size.y + pos.y, csize.y + cpos.y)) + csize.y;
	int coverage = (int)((dist_x * dist_y) / (csize.x * csize.y) * 100.0f);

#if 0
	DLOG_CTX_INFO(this, "%s pos: %d/%d item: %dx%d <> %dx%d dist: %dx%d coverage: %d%%", id.c_str(),
		(int)pos.x, (int)pos.y,
		(int)size.x, (int)size.y,
		(int)csize.x, (int)csize.y,
		(int)dist_x, (int)dist_y,
		coverage);
#endif

	if (coverage > coverage_requirement)
		return true;

	return false;
}

bool noice::source::validator_instance::sceneitem_is_main_video_source(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	std::string id = obs_source_get_unversioned_id(source);

#if 0
	DLOG_CTX_INFO(this, "%s", id.c_str());
#endif
	auto &map = *(_main_video_sources.get());
	if (map.find(id) != map.end())
		return true;
	return false;
}

void noice::source::validator_instance::region_validate(noice::region &region, obs_sceneitem_t *item, int &hits)
{
	vec2 startPos;
	vec2 pos;
	vec2_set(&startPos, region.box.x, region.box.y);
	vec2_set(&pos, region.box.x + region.box.w, region.box.y + region.box.h);

	bool hit = FindItemsInBox(item, startPos, pos, &_parent_transform);
	if (hit) {
		hits++;
		region.hits++;
	}
}

void noice::source::validator_instance::region_draw(noice::region &region)
{
	if (_draw_all_regions == false && region.hits == 0)
		return;

	matrix4 boxTransform;
	matrix4_identity(&boxTransform);
	matrix4_scale3f(&boxTransform, &boxTransform, region.box.w, region.box.h, 1.0f);
	matrix4_translate3f(&boxTransform, &boxTransform, region.box.x, region.box.y, 0.0f);

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "region_draw");

	matrix4 curTransform;
	vec2 boxScale;

	gs_matrix_get(&curTransform);
	boxScale.x = region.box.w;
	boxScale.y = region.box.h;

	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	gs_effect_t *eff = gs_get_effect();
	gs_eparam_t *colParam = gs_effect_get_param_by_name(eff, "color");

	gs_effect_set_vec4(colParam, &_color_region[0]);
	DrawRect(HANDLE_RADIUS / 2, boxScale);

	gs_matrix_pop();
	GS_DEBUG_MARKER_END();

	region.hits = 0;
}

void noice::source::validator_instance::source_draw(obs_sceneitem_t *item, bool collect_hit_source_names)
{
	if (!obs_sceneitem_visible(item))
		return;

	if (obs_sceneitem_is_group(item)) {
		matrix4 mat;
		obs_sceneitem_get_draw_transform(item, &mat);

		gs_matrix_push();
		gs_matrix_mul(&mat);

		matrix4_copy(&_parent_transform, &mat);

		auto source_draw_item = [this, collect_hit_source_names](obs_sceneitem_t *item) {
			source_draw(item, collect_hit_source_names);
		};
		using source_draw_item_t = decltype(source_draw_item);

		obs_sceneitem_group_enum_items(
			item,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				source_draw_item_t *func;
				func = reinterpret_cast<source_draw_item_t *>(param);
				(*func)(item);
				return true;
			},
			&source_draw_item);

		matrix4_identity(&_parent_transform);
		gs_matrix_pop();

		// Do not validate/hilight the group itself, because the grouped items could be miles apart
		return;
	}

	if (!SceneItemHasVideo(item))
		return;
	if (sceneitem_is_main_video_source(item))
		return;
	if (sceneitem_has_canvas_coverage(item, 98))
		return;

	int hits = 0;
	for (noice::region &region : *_game->regions()) {
		region_validate(region, item, hits);
	}

	if (_debug_sources == false && hits == 0)
		return;

	if (collect_hit_source_names) {
		obs_source_t *item_source = obs_sceneitem_get_source(item);
		const char *src_name = obs_source_get_name(item_source);

		this->_hit_source_names.push_back(std::string(src_name));
	}

	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "source_draw");

	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	matrix4 curTransform;
	vec2 boxScale;
	gs_matrix_get(&curTransform);
	obs_sceneitem_get_box_scale(item, &boxScale);

	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	gs_effect_t *eff = gs_get_effect();
	gs_eparam_t *colParam = gs_effect_get_param_by_name(eff, "color");

	if (hits == 0)
		gs_effect_set_vec4(colParam, &_color_source[0]);
	else
		gs_effect_set_vec4(colParam, &_color_source_collides[0]);

	DrawRect(HANDLE_RADIUS / 2, boxScale);

	gs_matrix_pop();
	GS_DEBUG_MARKER_END();
}

static bool compare_ovi_state(struct obs_video_info &a, struct obs_video_info &b)
{
	return !(a.base_width == b.base_width && a.base_height == b.base_height && a.output_width == b.output_width &&
		 a.output_height == b.output_height);
}

bool noice::source::validator_instance::try_source_candidate_name(std::string candidate)
{
	std::string current_name = obs_source_get_name(_source);

	if (current_name == candidate) {
		// Nothing to do
#if 0
		DLOG_CTX_INFO(this, "nothing to do: %s", candidate.c_str());
#endif
		return true;
	}

	obs_source_t *source_probe = obs_get_source_by_name(candidate.c_str());
	bool source_exists = source_probe != nullptr;
	obs_source_release(source_probe);

	if (source_exists == false) {
		_ignore_next_renamed_trigger = true;
		obs_source_set_name(_source, candidate.c_str());
#if 0
		DLOG_CTX_INFO(this, "new name: %s", candidate.c_str());
#endif
		return true;
	}
	return false;
}

void noice::source::validator_instance::sceneitem_set_name(bool deferred)
{
	if (deferred == true) {
		if (_ignore_next_renamed_trigger == true) {
			_ignore_next_renamed_trigger = false;
			return;
		}
		_refresh_sceneitem = true;
		return;
	}

	if (!noice::source::scene_tracker::instance()->has_finished_loading()) {
		// Try again later so loading the existing JSON won't fail and remove the invalid sceneitem
		return;
	}

	CALL_ENTRY(this);
	_refresh_sceneitem = false;

	bool can_update_source_names = noice::configuration::instance()->can_update_source_names();
	std::string verbose_name = _game ? _game->name_verbose : _game_name;

	// SLOBS uses plugin_id_GUID as a source name, expect instabilities if you rename things
	if (can_update_source_names) {
		if (try_source_candidate_name(
			    noice::string_format("%s: %s", NOICE_VALIDATOR_SOURCE_NAME_PREFIX.data(), verbose_name.c_str())))
			return;

		if (try_source_candidate_name(
			    noice::string_format("%s: %s (%d)", NOICE_VALIDATOR_SOURCE_NAME_PREFIX.data(), verbose_name.c_str(), _id)))
			return;
	}
}

void noice::source::validator_instance::sceneitem_set_transform(obs_sceneitem_t *item)
{
	obs_sceneitem_defer_update_begin(item);

	// set_{visible,locked} checks the existing state early internally,
	// do manual state check with with set_{info,crop}.
	//
	// Some obs_sceneitem_set operations can also trigger a JSON serialization to disk
	// so it's better to avoid doing things repeatedly and also check existing state

	// obs_sceneitem_set_visible(item, true);

	// The source is supposed to be a fullscreen canvas and it doesn't provide any value
	// to allow selecting and moving it around
	obs_sceneitem_set_locked(item, true);

	struct obs_transform_info cur_info;

	COMPILER_WARNINGS_PUSH
#if COMPILER_MSVC
	COMPILER_WARNINGS_DISABLE(4996)
#else
	COMPILER_WARNINGS_DISABLE("-Wdeprecated-declarations")
#endif
	obs_sceneitem_get_info(item, &cur_info);
	if (memcmp(&cur_info, &_info, sizeof(struct obs_transform_info)))
		obs_sceneitem_set_info(item, &_info);
	COMPILER_WARNINGS_POP

	struct obs_sceneitem_crop cur_crop;
	obs_sceneitem_get_crop(item, &cur_crop);
	if (memcmp(&cur_crop, &_crop, sizeof(struct obs_sceneitem_crop)))
		obs_sceneitem_set_crop(item, &_crop);

	obs_sceneitem_defer_update_end(item);
}

void noice::source::validator_instance::sceneitem_set_position(obs_sceneitem_t *item, int pass)
{
	obs_data_t *settings = obs_sceneitem_get_private_settings(item);

	if (pass == 1) {
		obs_data_set_default_int(settings, "noice-order-position", -1);
		int prev_order_position = (int)obs_data_get_int(settings, "noice-order-position");
		int order_position = obs_sceneitem_get_order_position(item);

		if (_sorted == false && prev_order_position != order_position) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			obs_data_set_int(settings, "noice-order-position", -1);
			_sorted = true;
		}
	} else if (pass == 2) {
		int prev_order_position = (int)obs_data_get_int(settings, "noice-order-position");
		int order_position = obs_sceneitem_get_order_position(item);
		_sorted = false;

		if (prev_order_position != order_position) {
			obs_data_set_int(settings, "noice-order-position", order_position);
		}
	}
	obs_data_release(settings);
}

static bool enum_validator_position(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	int pass = (int)(intptr_t)param;

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, enum_validator_position, param);
		return true;
	}

	// TODO: obs_source_showing?
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!strcmp(obs_source_get_id(source), NOICE_VALIDATOR_PLUGIN_ID)) {
		noice::source::validator_instance *instance =
			reinterpret_cast<noice::source::validator_instance *>(obs_obj_get_data(source));
		if (instance != nullptr) {
			instance->sceneitem_set_position(item, pass);
			if (pass == 2)
				instance->sceneitem_set_transform(item);
		}
	}
	return true;
}

// We need to be on top in order to hilight issues by drawing on top of other sources
// Handle sorting in two passes to avoid position fighting between scene items
void noice::source::validator_instance::sort_sceneitems(obs_scene_t *scene)
{
	obs_scene_enum_items(scene, enum_validator_position, (void *)(intptr_t)1);
	obs_scene_enum_items(scene, enum_validator_position, (void *)(intptr_t)2);
}

static void sceneitem_renamed(void *param, calldata_t *data)
{
	noice::source::validator_instance *self = reinterpret_cast<noice::source::validator_instance *>(param);
	self->sceneitem_set_name(true);
}

static void service_changed(void *param, calldata_t *data)
{
	noice::source::validator_instance *self = reinterpret_cast<noice::source::validator_instance *>(param);
	self->sceneitem_set_name(true);
	obs_source_update_properties(self->get());
}

noice::source::validator_factory::validator_factory()
{
	_info.id = NOICE_VALIDATOR_PLUGIN_ID;
	_info.type = OBS_SOURCE_TYPE_INPUT;
	// TODO: OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES?
	_info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE;
	_info.icon_type = OBS_ICON_TYPE_CUSTOM;

	support_size(true);
	support_activity_tracking(true);
	support_visibility_tracking(true);
	finish_setup();

	// register_proxy(NOICE_VALIDATOR_PLUGIN_ID);
}

noice::source::validator_factory::~validator_factory() {}

const char *noice::source::validator_factory::get_name()
{
	return obs_module_text("Noice.Validator");
}

#define CSWAP32(x) (((x)&0xff) << 24 | ((x)&0xff00) << 8 | ((x)&0xff0000) >> 8 | (((x) >> 24) & 0xff))

void noice::source::validator_factory::get_defaults2(obs_data_t *data)
{
	obs_data_set_default_string(data, "game", NOICE_PLACEHOLDER_GAME_NAME);
	obs_data_set_default_string(data, "prev_game", NOICE_PLACEHOLDER_GAME_NAME);

	obs_data_set_default_double(data, "hud_scale", 1.0f);
	obs_data_set_default_bool(data, "draw_all_regions", false);
	obs_data_set_default_bool(data, "debug_sources", false);

	// rgba
	obs_data_set_default_int(data, "color_region", CSWAP32(0xff8f1eff));          // orange: HSV: 30, 225, 255
	obs_data_set_default_int(data, "color_source", CSWAP32(0xffff1eff));          // yellow: HSV: 60, 225, 255
	obs_data_set_default_int(data, "color_source_collides", CSWAP32(0xffc71eff)); // meet between orange - yellow: HSV: 45, 225, 255
}

void noice::source::validator_instance::update_game_prop(obs_property_t *list)
{
	auto gm = noice::game_manager::instance();
	auto cfg = noice::configuration::instance();

	// Don't go fancy with updating existing property lists, as it seems like
	// SLOBS doesn't really support use of obs_property_list_item_disable
	obs_property_list_clear(list);

	if (cfg->is_slobs()) {
		// SLOBS workaround trigger for property settings being out of date
		std::string refresh_label = obs_module_text("NoiceValidator.RefreshGameList");
		std::string refresh_value = noice::string_format("__refresh_list__%d", refresh_game_list_counter++);
		obs_property_list_add_string(list, refresh_label.c_str(), refresh_value.c_str());
	}

	for (auto game_it : gm->get_games()) {
		const char *name = game_it.c_str();
		auto game = gm->get_game(name);

		if (game == nullptr)
			continue;

		bool available = !gm->is_game_acquired(game, _source_guid);

#if 0
		DLOG_CTX_INFO(this, "--- game: %s available: %d", name, available);
#endif
		if (available)
			obs_property_list_add_string(list, game->name_verbose.c_str(), name);
	}
}

bool noice::source::validator_instance::update_hud_scale_prop(obs_property_t *prop)
{
	bool game_selected = false;
	bool hud_available = false;
	noice::in_game_hud_scale hud;

	if (_game && _game->disabled == false) {
		game_selected = true;
		hud = _game->in_game_hud;
	}

	// Not all games support in-game user setting for UI HUD scaling
	if (hud.min != hud.max)
		hud_available = true;
	obs_property_float_set_limits(prop, hud.min, hud.max, hud.step);
	obs_property_set_visible(prop, game_selected && hud_available);

	return game_selected;
}

obs_properties_t *noice::source::validator_factory::get_properties2(noice::source::validator_instance *instance)
{
	CALL_ENTRY(instance);
	obs_properties_t *props = obs_properties_create();
	std::string game_label = noice::string_format("%s", obs_module_text("Noice.Game"));

	auto cfg = noice::configuration::instance();
	if (!cfg->noice_service_selected())
		game_label.insert(0, noice::string_format("[%s] ", obs_module_text("Noice.ServiceInactive")));

	obs_property_t *list = obs_properties_add_list(props, "game", game_label.c_str(), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	instance->update_game_prop(list);
	auto cb = [](void *data, obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
		noice::source::validator_instance *self = reinterpret_cast<noice::source::validator_instance *>(data);
		return self->content_settings_changed(props, list, settings);
	};
	obs_property_set_modified_callback2(list, cb, instance);

	obs_property_t *p = nullptr;
	p = obs_properties_add_float_slider(props, "hud_scale", obs_module_text("NoiceValidator.HudScale"), 1.0f, 1.0f, 0.25f);
	bool game_selected = instance->update_hud_scale_prop(p);

	p = obs_properties_add_bool(props, "draw_all_regions", obs_module_text("NoiceValidator.DrawAllRegions"));
	obs_property_set_visible(p, game_selected);
	p = obs_properties_add_bool(props, "debug_sources", obs_module_text("NoiceValidator.DebugSources"));
	obs_property_set_visible(p, game_selected);

	p = obs_properties_add_color_alpha(props, "color_region", obs_module_text("NoiceValidator.ColorRegion"));
	obs_property_set_visible(p, game_selected);
	p = obs_properties_add_color_alpha(props, "color_source", obs_module_text("NoiceValidator.ColorSource"));
	obs_property_set_visible(p, game_selected);
	p = obs_properties_add_color_alpha(props, "color_source_collides", obs_module_text("NoiceValidator.ColorSourceCollides"));
	obs_property_set_visible(p, game_selected);

	return props;
}

noice::source::validator_instance::validator_instance(obs_data_t *data, obs_source_t *self) : obs::source_instance(data, self)
{
	_id = ++noice_validator_uniq_rt_count;
	_source = self;
	CALL_ENTRY(this);

	_main_video_sources = std::make_shared<std::map<std::string, bool>>();
	auto &map = *(_main_video_sources.get());

	map[NOICE_VALIDATOR_PLUGIN_ID] = true;
	// Win
	map["monitor_capture"] = true;
	map["game_capture"] = true;
	// Mac
	map["display_capture"] = true;
	// Win & Mac
	map["window_capture"] = true;
	// Linux
	map["pipewire-desktop-capture-source"] = true;
	map["pipewire-window-capture-source"] = true;
	map["xcomposite_input"] = true;
	map["xshm_input"] = true;

	vec2_set(&_info.pos, 0.0f, 0.0f);
	_info.rot = 0.0f;
	vec2_set(&_info.scale, 1.0f, 1.0f);
	_info.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	_info.bounds_type = OBS_BOUNDS_NONE;
	_info.bounds_alignment = OBS_ALIGN_CENTER;
	vec2_set(&_info.bounds, 0.0f, 0.0f);

	_crop.left = 0;
	_crop.top = 0;
	_crop.right = 0;
	_crop.bottom = 0;

	matrix4_identity(&_parent_transform);

	_ovi = {};
	_ovi.base_width = 1;
	_ovi.base_height = 1;

	_current_enum_scene = nullptr;

	auto cfg = noice::configuration::instance();
	if (cfg->can_update_source_names())
		_source_guid = noice::string_format("%p", _source);
	else
		_source_guid = obs_source_get_name(_source);

	validate_game_name_availability(data);

	signal_handler_connect(obs_source_get_signal_handler(_source), "rename", sceneitem_renamed, this);
	signal_handler_connect(cfg->get_signal_handler(), "service", service_changed, this);
	update(data);
}

noice::source::validator_instance::~validator_instance()
{
	CALL_ENTRY(this);

	obs_weak_source_release(_current_enum_scene);
	_current_enum_scene = nullptr;

	auto cfg = noice::configuration::instance();
	auto gm = noice::game_manager::instance();

	signal_handler_disconnect(obs_source_get_signal_handler(_source), "rename", sceneitem_renamed, this);
	if (cfg != nullptr) {
		signal_handler_disconnect(cfg->get_signal_handler(), "service", service_changed, this);
	}

	if (_game) {
		if (gm != nullptr) {
			gm->release_game(_game, _source_guid);
		}
		_game = nullptr;
	}
	if (_main_video_sources) {
		_main_video_sources = nullptr;
	}
}

void noice::source::validator_instance::update_current_enum_scene()
{
	// Plugin source can be shared between multiple scenes / sceneitems. We typically only care
	// what's the current when the rendering happens, but something like properties view could
	// trigger direct rendering with obs_source_video_render, so hold on to last result.
	obs_weak_source_t *scene_candidate = noice::source::scene_tracker::instance()->get_current_enum_scene();
	if (scene_candidate != nullptr) {
		obs_weak_source_release(_current_enum_scene);
		_current_enum_scene = scene_candidate;
		obs_weak_source_addref(_current_enum_scene);
	}
}

obs_scene_t *noice::source::validator_instance::current_enum_scene()
{
	obs_source_t *source = obs_weak_source_get_source(_current_enum_scene);
#if 0
	DLOG_CTX_INFO(this, "%s", obs_source_get_name(source));
#endif
	return obs_scene_from_source(source);
}

void noice::source::validator_instance::load(obs_data_t *data)
{
	update(data);
}

void noice::source::validator_instance::migrate(obs_data_t *, std::uint64_t) {}

void noice::source::validator_instance::update(obs_data_t *data)
{
	CALL_ENTRY(this);

	std::string game_name = obs_data_get_string(data, "game");
	float hud_scale = (float)obs_data_get_double(data, "hud_scale");
	bool draw_all_regions = (float)obs_data_get_bool(data, "draw_all_regions");
	bool debug_sources = (float)obs_data_get_bool(data, "debug_sources");
	uint32_t color_region = (uint32_t)obs_data_get_int(data, "color_region");
	uint32_t color_source = (uint32_t)obs_data_get_int(data, "color_source");
	uint32_t color_source_collides = (uint32_t)obs_data_get_int(data, "color_source_collides");

	if (_game_name != game_name) {
		sceneitem_set_name(true);

		auto gm = noice::game_manager::instance();

		// hud_scale is serialized into game specific sources, but saving the last
		// known game specific value during runtime helps when rapidly changing
		// between games at the properties window
		if (_game != nullptr) {
			_game->in_game_hud.value = hud_scale;
			gm->release_game(_game, _source_guid);
		}
		if (!_game_name.empty())
			DLOG_CTX_INFO(this, "current game: %s -> %s", _game_name.c_str(), game_name.c_str());

		_game_name = game_name;
		_game = gm->get_game(_game_name);

		if (_game != nullptr) {
			gm->acquire_game(_game, _source_guid);
			_game->reset_regions = true;

			// We might have inherited scale from another game with different min/max/step values
			float new_scale = _game->in_game_hud.clamp_value();
			if (hud_scale != new_scale) {
				hud_scale = new_scale;
				obs_data_set_double(data, "hud_scale", hud_scale);
			}
		}

		obs_data_set_string(data, "prev_game", game_name.c_str());
	}

	if (_game) {
		if (_game->in_game_hud.value != hud_scale) {
			_game->in_game_hud.value = hud_scale;
			_game->reset_regions = true;
		}
	}

	_draw_all_regions = draw_all_regions;
	_debug_sources = debug_sources;

	vec4_from_rgba(&_color_region[1], color_region);
	vec4_from_rgba(&_color_source[1], color_source);
	vec4_from_rgba(&_color_source_collides[1], color_source_collides);

	/* need linear path for correct alpha blending */
	_linear_srgb = gs_get_linear_srgb() ||
		       ((_color_region[1].w < 1.0f) || (_color_source[1].w < 1.0f) || (_color_source_collides[1].w < 1.0f));

	vec4_copy(&_color_region[0], &_color_region[1]);
	vec4_copy(&_color_source[0], &_color_source[1]);
	vec4_copy(&_color_source_collides[0], &_color_source_collides[1]);

	if (_linear_srgb) {
		gs_float3_srgb_nonlinear_to_linear(_color_region[0].ptr);
		gs_float3_srgb_nonlinear_to_linear(_color_source[0].ptr);
		gs_float3_srgb_nonlinear_to_linear(_color_source_collides[0].ptr);
	}
}

bool noice::source::validator_instance::validate_game_name_availability(obs_data_t *data)
{
	auto gm = noice::game_manager::instance();
	std::string game_name = obs_data_get_string(data, "game");
	std::string new_game_name;
	bool refresh = false;

	if (game_name.rfind("__refresh_list__", 0) == 0) {
		// SLOBS workaround trigger for property settings being out of date
		if (_game_name.empty()) {
			new_game_name = obs_data_get_string(data, "prev_game");
		} else {
			new_game_name = _game_name;
			obs_data_set_string(data, "prev_game", new_game_name.c_str());
			refresh = true;
		}
	} else if (gm->is_game_acquired(game_name, _source_guid)) {
		// We tried to select a game that was already acquired by someone else, property list out of date?
		new_game_name = _game_name;
		if (!new_game_name.empty() && gm->is_game_acquired(new_game_name, _source_guid)) {
			new_game_name = NOICE_PLACEHOLDER_GAME_NAME;
		}
	}

	if (!new_game_name.empty()) {
		DLOG_CTX_INFO(this, "Reset game: %s (was: %s, attempted to set: %s) Refresh: %d", new_game_name.c_str(), _game_name.c_str(),
			      game_name.c_str(), refresh);
		obs_data_set_string(data, "game", new_game_name.c_str());
	}
	return refresh;
}

bool noice::source::validator_instance::content_settings_changed(obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	UNUSED_PARAMETER(list);
	CALL_ENTRY(this);

	bool refresh = validate_game_name_availability(settings);
	if (refresh)
		return true;

	update(settings);

	obs_property_t *p = nullptr;
	p = obs_properties_get(props, "game");
	update_game_prop(p);

	p = obs_properties_get(props, "hud_scale");
	bool game_selected = update_hud_scale_prop(p);

	p = obs_properties_get(props, "draw_all_regions");
	obs_property_set_visible(p, game_selected);
	p = obs_properties_get(props, "debug_sources");
	obs_property_set_visible(p, game_selected);

	p = obs_properties_get(props, "color_region");
	obs_property_set_visible(p, game_selected);
	p = obs_properties_get(props, "color_source");
	obs_property_set_visible(p, game_selected);
	p = obs_properties_get(props, "color_source_collides");
	obs_property_set_visible(p, game_selected);

	return true;
}

void noice::source::validator_instance::save(obs_data_t *) {}

void noice::source::validator_instance::activate()
{
	CALL_ENTRY(this);
	sceneitem_set_name(true);
}

void noice::source::validator_instance::deactivate()
{
	CALL_ENTRY(this);
}

void noice::source::validator_instance::show()
{
	CALL_ENTRY(this);
}

void noice::source::validator_instance::hide()
{
	CALL_ENTRY(this);
}

std::uint32_t noice::source::validator_instance::get_width()
{
	return _ovi.base_width;
}

std::uint32_t noice::source::validator_instance::get_height()
{
	return _ovi.base_height;
}

void noice::source::validator_instance::video_tick(float_t seconds)
{
	uint64_t frame_time = obs_get_video_frame_time();

	if (_refresh_sceneitem) {
		_game = noice::game_manager::instance()->get_game(_game_name);
		sceneitem_set_name();
	}

	_last_time = frame_time;
}

void noice::source::validator_instance::video_render(gs_effect_t *)
{
	update_current_enum_scene();

	bool collect_hit_source_names = scene_tracker::instance()->needs_diagnostics(diagnostics_type::hit_source_names);

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi))
		return;

	if (!_game || (_game && _game->disabled)) {
		_ovi = ovi;
		return;
	}

	_game->reset_regions |= compare_ovi_state(_ovi, ovi);
	_ovi = ovi;

	if (_game->reset_regions) {
		_game->reset_regions = false;

		DLOG_CTX_INFO(this, "ovi: base %dx%d output %dx%d scale: %f", _ovi.base_width, _ovi.base_height, _ovi.output_width,
			      _ovi.output_height, _game->in_game_hud.value);
		for (noice::region &region : *_game->regions())
			region.align_box(_ovi, _game->in_game_hud.value);
	}

#if 1
	// Add some pulse for the collision color used
	const int step_range = 30;
	uint64_t rot = (_last_time / NSEC_PER_MSEC / 3) % 360;
	int step = (int)(sin(rot * (M_PI / 180.0f)) * step_range);

	noice::hsv_util color(_color_source_collides[1]);
	{
		color.hue = (color.hue + step) % 360;
		color.value = std::clamp(color.value, step_range, 255 - step_range) + step;
	}
	color.to_vec4(_color_source_collides[0]);

	if (_linear_srgb) {
		gs_float3_srgb_nonlinear_to_linear(_color_source_collides[0].ptr);
	}
#endif

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(_linear_srgb);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	obs_scene_t *scene = current_enum_scene();
	if (scene) {
		gs_matrix_push();

		if (collect_hit_source_names) {
			_hit_source_names.clear();
		}

		auto source_draw_item = [this, collect_hit_source_names](obs_sceneitem_t *item) {
			source_draw(item, collect_hit_source_names);
		};
		using source_draw_item_t = decltype(source_draw_item);

		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				source_draw_item_t *func;
				func = reinterpret_cast<source_draw_item_t *>(param);
				(*func)(item);
				return true;
			},
			&source_draw_item);

		if (collect_hit_source_names) {
			scene_tracker::instance()->add_hit_item_source_names(_hit_source_names);
		}

		for (noice::region &region : *_game->regions()) {
			region_draw(region);
		}
		gs_matrix_pop();
	}
	obs_scene_release(scene);

	gs_load_vertexbuffer(nullptr);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);

	gs_enable_framebuffer_srgb(previous);
}
