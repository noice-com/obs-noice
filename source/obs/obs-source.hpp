/*
 * Taken and modified from obs-StreamFX
 * Copyright (C) 2018 Michael Fabian Dirks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once
#include <stdexcept>

namespace noice::obs {
class source {
	obs_source_t *_ref;
	bool _is_owner;

public:
	FORCE_INLINE ~source() { release(); };

	/** Empty/Invalid hard reference.
		 *
		 */
	FORCE_INLINE source() : _ref(nullptr), _is_owner(false){};

	/** Create a new hard reference from an existing pointer.
		 *
		 * @param source The source object to reference.
		 * @param add_reference Should we increment the reference counter (duplicate ownership) or leave as it is (transfer ownership)?
		 */
	FORCE_INLINE source(obs_source_t *source, bool duplicate_reference = false, bool take_ownership = true) : _is_owner(take_ownership)
	{
		if (duplicate_reference) {
			_ref = obs_source_get_ref(source);
		} else {
			_ref = source;
		}
	};

	/** Create a new hard reference for a given source by name.
		 *
		 * Attention: May fail if the name does not exactly match.
		 */
	FORCE_INLINE source(std::string_view name) : _is_owner(true) { _ref = obs_get_source_by_name(name.data()); };

	/** Create a new hard reference for a new source.
		 *
		 * Attention: May fail.
		 */
	FORCE_INLINE source(std::string_view id, std::string_view name, obs_data_t *settings, obs_data_t *hotkeys) : _is_owner(true)
	{
		_ref = obs_source_create(id.data(), name.data(), settings, hotkeys);
		if (!_ref) {
			throw std::runtime_error("Failed to create source with given parameters.");
		}
	};

	/** Create a new hard reference for a new private source.
		 *
		 * Attention: May fail.
		 */
	FORCE_INLINE source(std::string_view id, std::string_view name, obs_data_t *settings) : _is_owner(true)
	{
		_ref = obs_source_create_private(id.data(), name.data(), settings);
		if (!_ref) {
			throw std::runtime_error("Failed to create source with given parameters.");
		}
	};

	FORCE_INLINE source(source &&move) noexcept
	{
		_ref = move._ref;
		_is_owner = move._is_owner;
		move._ref = nullptr;
	};

	FORCE_INLINE ::noice::obs::source &operator=(source &&move) noexcept
	{
		release();
		_ref = move._ref;
		_is_owner = move._is_owner;
		move._ref = nullptr;
		return *this;
	};

	FORCE_INLINE source(const source &copy)
	{
		if (copy._is_owner) {
			_ref = obs_source_get_ref(copy._ref);
		} else {
			_ref = copy._ref;
		}
		_is_owner = copy._is_owner;
	};

	FORCE_INLINE ::noice::obs::source &operator=(const source &copy)
	{
		release();
		if (copy._is_owner) {
			_ref = obs_source_get_ref(copy._ref);
		} else {
			_ref = copy._ref;
		}
		_is_owner = copy._is_owner;
		return *this;
	};

public:
	/** Retrieve the underlying pointer for manual manipulation.
		 *
		 * Attention: Ownership remains with the class instance.
		 */
	FORCE_INLINE obs_source_t *get() const { return _ref; };

	/** Release the underlying pointer.
		 *
		 * Useful if you need to respond to the "source_remove" or "remove" signals.
		 *
		 * EXPORT void obs_source_release(obs_source_t *source);
		 */
	FORCE_INLINE void release()
	{
		if (_ref && _is_owner) {
			obs_source_release(_ref);
			_ref = nullptr;
			_is_owner = false;
		}
	};

	/** Duplicate the source if possible.
		 *
		 * Will create a duplicate the source entirely unless forbidden. If forbidden, will instead just return a reference.
		 *
		 * EXPORT obs_source_t *obs_source_duplicate(obs_source_t *source, const char *desired_name, bool create_private);
		 */
	FORCE_INLINE ::noice::obs::source duplicate(std::string_view name, bool is_private)
	{
		return obs_source_duplicate(_ref, name.data(), is_private);
	};

public:
	/** Get the source info identifier for this reference.
		 *
		 * May have a version appended to the end.
		 *
		 * EXPORT const char *obs_source_get_id(const obs_source_t *source);
		 */
	FORCE_INLINE std::string_view id() const { return obs_source_get_id(_ref); };

	/** Get the source info identifier for this reference.
		 *
		 * EXPORT const char *obs_source_get_unversioned_id(const obs_source_t *source);
		 */
	FORCE_INLINE std::string_view unversioned_id() const { return obs_source_get_unversioned_id(_ref); };

	/** What type is this source?
		 *
		 */
	FORCE_INLINE obs_source_type type() const { return obs_source_get_type(_ref); };

	/** Get the output flags.
		 * 
		 * EXPORT uint32_t obs_source_get_output_flags(const obs_source_t *source);
		 */
	FORCE_INLINE uint32_t output_flags() const { return obs_source_get_output_flags(_ref); };

	/** Get the flags
		 * 
		 * EXPORT uint32_t obs_source_get_flags(const obs_source_t *source);
		 */
	FORCE_INLINE uint32_t flags() const { return obs_source_get_flags(_ref); };

	/** Set the flags
		 * 
		 * EXPORT void obs_source_set_default_flags(obs_source_t* source, uint32_t flags);
		 */
	FORCE_INLINE void default_flags(uint32_t flags) { obs_source_set_default_flags(_ref, flags); };

	/** Set the flags
		 * 
		 * EXPORT void obs_source_set_flags(obs_source_t *source, uint32_t flags);
		 */
	FORCE_INLINE void flags(uint32_t flags) { obs_source_set_flags(_ref, flags); };

	/** What is the source type called?
		 *
		 * EXPORT const char *obs_source_get_display_name(const char *id);
		 */
	FORCE_INLINE std::string_view display_name() const { return obs_source_get_display_name(id().data()); };

	/** What is this source called?
		 *
		 * EXPORT const char *obs_source_get_name(const obs_source_t *source);
		 */
	FORCE_INLINE std::string_view name() const { return obs_source_get_name(_ref); };

	/** Change the name of the source.
		 *
		 * Triggers 'rename' on the source itself, as well as 'source_rename' globally if not private.
		 *
		 * EXPORT void obs_source_set_name(obs_source_t *source, const char *name);
		 */
	FORCE_INLINE void name(std::string_view new_name) { obs_source_set_name(_ref, new_name.data()); };

	/** 
		 *
		 * EXPORT bool obs_source_enabled(const obs_source_t *source);
		 */
	FORCE_INLINE bool enabled() const { return obs_source_enabled(_ref); };

	/** 
		 *
		 * EXPORT void obs_source_set_enabled(obs_source_t *source, bool enabled);
		 */
	FORCE_INLINE void enabled(bool enabled) { obs_source_set_enabled(_ref, enabled); };

	/** 
		 *
		 * EXPORT bool obs_source_is_hidden(obs_source_t *source);
		 */
	FORCE_INLINE bool hidden() const { return obs_source_is_hidden(_ref); };

	/** 
		 *
		 * EXPORT void obs_source_set_hidden(obs_source_t *source, bool hidden);
		 */
	FORCE_INLINE void hidden(bool v) { obs_source_set_hidden(_ref, v); };

public /* Size */:
	/** Get the base width of the source, if supported.
		 *
		 * This will be the size without any other scaling factors applied.
		 * 
		 * EXPORT uint32_t obs_source_get_base_width(obs_source_t *source);
		 */
	FORCE_INLINE uint32_t base_width() const { return obs_source_get_base_width(_ref); };

	/** Get the base height of the source, if supported.
		 *
		 * This will be the size without any other scaling factors applied.
		 * 
		 * EXPORT uint32_t obs_source_get_base_height(obs_source_t *source);
		 */
	FORCE_INLINE uint32_t base_height() const { return obs_source_get_base_height(_ref); };

	/** Get the reported width of the source, if supported.
		 *
		 * EXPORT uint32_t obs_source_get_width(obs_source_t *source);
		 */
	FORCE_INLINE uint32_t width() const { return obs_source_get_width(_ref); };

	/** Get the reported height of the source, if supported.
		 *
		 * EXPORT uint32_t obs_source_get_height(obs_source_t *source);
		 */
	FORCE_INLINE uint32_t height() const { return obs_source_get_height(_ref); };

	/** Get the reported size of the source, if supported.
		 *
		 * EXPORT uint32_t obs_source_get_width(obs_source_t *source);
		 * EXPORT uint32_t obs_source_get_height(obs_source_t *source);
		 */
	FORCE_INLINE std::pair<uint32_t, uint32_t> size() const { return {width(), height()}; };

public /* Configuration */:
	/** Is the source configurable?
		 *
		 * EXPORT bool obs_source_configurable(const obs_source_t *source);
		 */
	FORCE_INLINE bool configurable() { return obs_source_configurable(_ref); };

	/** Retrieve the properties for the source.
		 *
		 * EXPORT obs_properties_t *obs_get_source_properties(const char *id);
		 */
	FORCE_INLINE obs_properties_t *properties() { return obs_source_properties(_ref); };

	/** Signal for properties to be updated.
		 *
		 * EXPORT void obs_source_update_properties(obs_source_t *source);
		 */
	FORCE_INLINE void update_properties() { obs_source_update_properties(_ref); };

	/** Retrieve the default values for the settings.
		 *
		 * EXPORT obs_data_t *obs_get_source_defaults(const char *id);
		 */
	FORCE_INLINE obs_data_t *defaults() { return obs_get_source_defaults(id().data()); };

	/** Retrieve the private settings.
		 *
		 * EXPORT obs_data_t *obs_source_get_private_settings(obs_source_t *item);
		 */
	FORCE_INLINE obs_data_t *private_settings() { return obs_source_get_private_settings(_ref); };

	/** Retrieve the current settings.
		 *
		 * EXPORT obs_data_t *obs_source_get_settings(const obs_source_t *source);
		 */
	FORCE_INLINE obs_data_t *settings() { return obs_source_get_settings(_ref); };

	/** Update the settings with new ones.
		 *
		 * Does not remove previously existing entries.
		 *
		 * EXPORT void obs_source_update(obs_source_t *source, obs_data_t *settings);
		 */
	FORCE_INLINE void update(obs_data_t *settings) { obs_source_update(_ref, settings); };

	/** Reset the settings, then update with new settings.
		 *
		 * EXPORT void obs_source_reset_settings(obs_source_t *source, obs_data_t *settings);
		 */
	FORCE_INLINE void reset_settings(obs_data_t *settings = nullptr) { obs_source_reset_settings(_ref, settings); };

	/** Signal the source to load.
		 *
		 * EXPORT void obs_source_load(obs_source_t *source);
		 */
	FORCE_INLINE void load() { obs_source_load(_ref); };

	/** Signal the source and all its filters to load.
		 *
		 * EXPORT void obs_source_load2(obs_source_t *source);
		 */
	FORCE_INLINE void load2() { obs_source_load2(_ref); };

	/** Signal the source to save.
		 *
		 * EXPORT void obs_source_save(obs_source_t *source);
		 */
	FORCE_INLINE void save() { obs_source_save(_ref); };

public /* Interaction */:
	/** 
		 *
		 * EXPORT void obs_source_send_mouse_click(obs_source_t *source, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count);
		 */
	FORCE_INLINE void send_mouse_press(const obs_mouse_event *event, int32_t type, bool released, uint32_t count)
	{
		return obs_source_send_mouse_click(_ref, event, type, released, count);
	};

	/** 
		 *
		 * EXPORT void obs_source_send_mouse_move(obs_source_t *source, const struct obs_mouse_event *event, bool mouse_leave);
		 */
	FORCE_INLINE void send_mouse_move(const obs_mouse_event *event, bool leave)
	{
		return obs_source_send_mouse_move(_ref, event, leave);
	};

	/** 
		 *
		 * EXPORT void obs_source_send_mouse_wheel(obs_source_t *source, const struct obs_mouse_event *event, int x_delta, int y_delta);
		 */
	FORCE_INLINE void send_mouse_wheel(const obs_mouse_event *event, int32_t x_delta, int32_t y_delta)
	{
		return obs_source_send_mouse_wheel(_ref, event, x_delta, y_delta);
	};

	/** 
		 *
		 * EXPORT void obs_source_send_key_click(obs_source_t *source, const struct obs_key_event *event, bool key_up);
		 */
	FORCE_INLINE void send_key_press(const obs_key_event *event, bool released)
	{
		return obs_source_send_key_click(_ref, event, released);
	};

	/** 
		 *
		 * EXPORT void obs_source_send_focus(obs_source_t *source, bool focus);
		 */
	FORCE_INLINE void send_focus(bool in_focus) { return obs_source_send_focus(_ref, in_focus); };

public /* Filters */:
	/** 
		 *
		 * EXPORT void obs_source_filter_add(obs_source_t *source, obs_source_t *filter);
		 */
	FORCE_INLINE void add_filter(::noice::obs::source &filter) { return obs_source_filter_add(_ref, filter.get()); };

	/** 
		 *
		 * EXPORT void obs_source_filter_remove(obs_source_t *source, obs_source_t *filter);
		 */
	FORCE_INLINE void remove_filter(::noice::obs::source &filter) { return obs_source_filter_remove(_ref, filter.get()); };

	/** 
		 *
		 * EXPORT obs_source_t *obs_filter_get_parent(const obs_source_t *filter);
		 */
	FORCE_INLINE ::noice::obs::source get_filter_parent() { return obs_filter_get_parent(_ref); };

	/** 
		 *
		 * EXPORT obs_source_t *obs_filter_get_target(const obs_source_t *filter);
		 */
	FORCE_INLINE ::noice::obs::source get_filter_target() { return obs_filter_get_target(_ref); };

	/** 
		 *
		 * EXPORT void obs_source_skip_video_filter(obs_source_t *filter);
		 */
	FORCE_INLINE void skip_video_filter() { return obs_source_skip_video_filter(_ref); };

	/** 
		 *
		 * EXPORT bool obs_source_process_filter_begin(obs_source_t *filter, enum gs_color_format format, enum obs_allow_direct_render allow_direct);
		 */
	FORCE_INLINE bool process_filter_begin(gs_color_format format, obs_allow_direct_render allow_direct)
	{
		return obs_source_process_filter_begin(_ref, format, allow_direct);
	};

	/** 
		 *
		 * EXPORT void obs_source_process_filter_end(obs_source_t *filter, gs_effect_t *effect, uint32_t width, uint32_t height);
		 */
	FORCE_INLINE void process_filter_end(gs_effect_t *effect, uint32_t width, uint32_t height)
	{
		obs_source_process_filter_end(_ref, effect, width, height);
	};

	/** 
		 *
		 * EXPORT void obs_source_process_filter_tech_end(obs_source_t *filter, gs_effect_t *effect, uint32_t width, uint32_t height, const char *tech_name);
		 */
	FORCE_INLINE void process_filter_tech_end(gs_effect_t *effect, uint32_t width, uint32_t height, std::string_view tech_name)
	{
		obs_source_process_filter_tech_end(_ref, effect, width, height, tech_name.data());
	};

public /* Active/Showing References */:
	/** Is the source visible in main view?
		 *
		 * EXPORT bool obs_source_active(const obs_source_t *source);
		 */
	FORCE_INLINE bool active() const { return obs_source_active(_ref); }

	/** Add a active reference (visible in main view).
		 *
		 * EXPORT void obs_source_inc_active(obs_source_t *source);
		 */
	FORCE_INLINE void increment_active() { obs_source_inc_active(_ref); }

	/** Remove a active reference (visible in main view).
		 *
		 * EXPORT void obs_source_dec_active(obs_source_t *source);
		 */
	FORCE_INLINE void decrement_active() { obs_source_dec_active(_ref); }

	/** Is the source visible in auxiliary views?
		 *
		 * EXPORT bool obs_source_showing(const obs_source_t *source);
		 */
	FORCE_INLINE bool showing() const { return obs_source_showing(_ref); }

	/** Add a showing reference (visible in auxiliary view).
		 *
		 * EXPORT void obs_source_inc_showing(obs_source_t *source);
		 * EXPORT void obs_source_dec_showing(obs_source_t *source);
		 */
	FORCE_INLINE void increment_showing() { obs_source_inc_showing(_ref); }

	/** Add a showing reference (visible in auxiliary view).
		 *
		 * EXPORT void obs_source_inc_showing(obs_source_t *source);
		 * EXPORT void obs_source_dec_showing(obs_source_t *source);
		 */
	FORCE_INLINE void decrement_showing() { obs_source_dec_showing(_ref); }

public /* ToDo */:

	/** 
		 *
		 * EXPORT obs_missing_files_t* obs_source_get_missing_files(const obs_source_t *source);
		 */
	obs_missing_files_t *get_missing_files();

	/** 
		 *
		 * EXPORT void obs_source_replace_missing_file(obs_missing_file_cb cb, obs_source_t *source, const char *new_path, void *data);
		 */
	void replace_missing_file(obs_missing_file_cb cb, std::string_view path, void *data);

public:
	FORCE_INLINE operator obs_source_t *() const { return _ref; }

	FORCE_INLINE obs_source_t *operator*() const { return _ref; }

	FORCE_INLINE operator bool() const { return _ref != nullptr; };

	FORCE_INLINE bool operator==(source const &rhs) const { return _ref == rhs._ref; };

	FORCE_INLINE bool operator<(source const &rhs) const { return _ref < rhs._ref; };

	FORCE_INLINE bool operator==(obs_source_t *const &rhs) const { return _ref == rhs; };
};
} // namespace noice::obs
