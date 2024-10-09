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

#include "ui.hpp"
#include <QDesktopServices>
#include <QMainWindow>
#include <QMenuBar>
#include <QTranslator>
#include <QCoreApplication>
#include <obs-module.h>
#include "common.hpp"
#include "scene-tracker.hpp"
#include "noice-bridge.hpp"
#include <obs.h>
#include "util/util-curl.hpp"

static constexpr std::string_view I18N_MENU = "Menu";
static constexpr std::string_view I18N_MENU_CHECKFORUPDATES = "Menu.CheckForUpdates";
static constexpr std::string_view I18N_MENU_ABOUT = "Menu.About";

class noice_translator : public QTranslator {
public:
	noice_translator(QObject *parent = nullptr) {}
	~noice_translator() {}

	virtual QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
				  int n = -1) const override
	{
		static constexpr std::string_view prefix = "Noice::";

		if (disambiguation) {
			std::string_view view{disambiguation};
			if (view.substr(0, prefix.length()) == prefix) {
				return QT_UTF8(obs_module_text(view.substr(prefix.length()).data()));
			}
		} else if (sourceText) {
			std::string_view view{sourceText};
			if (view.substr(0, prefix.length()) == prefix) {
				return QT_UTF8(obs_module_text(view.substr(prefix.length()).data()));
			}
		}
		return QString();
	}
};

noice::ui::ui::~ui()
{
	obs_frontend_remove_event_callback(obs_event_handler, this);
}

noice::ui::ui::ui()
	: _translator(),
	  _menu(),
	  _menu_action(),
	  _update_action(),
	  _about_action(),
	  _chat_dock(),
	  _chat_dock_action(),
	  _eventlist_dock(),
	  _eventlist_dock_action(),
	  _stats_dock(),
	  _stats_dock_action(),
	  _core_module_found(false)
{
	obs_frontend_add_event_callback(obs_event_handler, this);
}

void noice::ui::ui::obs_event_handler(obs_frontend_event event, void *private_data)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP", (int)event);
	else if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_SCENE_CHANGED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_FINISHED_LOADING", (int)event);
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_STREAMING_STARTING", (int)event);
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STARTED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_STREAMING_STARTED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_STREAMING_STOPPED", (int)event);
	else if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN", (int)event);
	else if (event == OBS_FRONTEND_EVENT_EXIT)
		DLOG_INFO("event: %d OBS_FRONTEND_EVENT_EXIT", (int)event);

	noice::ui::ui *ui = reinterpret_cast<noice::ui::ui *>(private_data);
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// Once all plugins are loaded, verify the bridge exists before trying anything else
		try {
			auto bridge = noice::get_bridge();
			if (bridge != nullptr)
				ui->_core_module_found = true;
		} catch (...) {
			return;
		}
		ui->load();
	}

	if (ui->_core_module_found == false)
		return;

	if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
		ui->unload();
	}

	auto scene_tracker = noice::get_bridge()->scene_tracker_instance();
	if (scene_tracker == nullptr || !scene_tracker->has_finished_loading())
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
		obs_source_t *preview_source = obs_frontend_get_current_preview_scene();
		scene_tracker->set_preview_scene(preview_source);
		obs_source_release(preview_source);

		obs_source_t *program_source = obs_frontend_get_current_scene();
		scene_tracker->set_current_scene(program_source);
		obs_source_release(program_source);
	} break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING: {
		auto cfg = noice::get_bridge()->configuration_instance();

		if (cfg->noice_service_selected()) {
			scene_tracker->trigger_fetch_selected_game();
		}
	} break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED: {
		auto cfg = noice::get_bridge()->configuration_instance();
		cfg->set_streaming_active(true);
	} break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED: {
		auto cfg = noice::get_bridge()->configuration_instance();
		cfg->set_streaming_active(false);
	} break;
	default:
		break;
	}
}

void noice::ui::ui::load()
{
	// Add translator.
	_translator = static_cast<QTranslator *>(new noice_translator(this));
	QCoreApplication::installTranslator(_translator);

	{ // Noice Menu
		QMainWindow *main_widget = reinterpret_cast<QMainWindow *>(obs_frontend_get_main_window());

		_menu = new QMenu(main_widget);

		// Add Updater
		_update_action = _menu->addAction(obs_module_text(I18N_MENU_CHECKFORUPDATES.data()));
		connect(_update_action, &QAction::triggered, this, &noice::ui::ui::menu_update_triggered);

		_menu->addSeparator();

		// Add About
		_about_action = _menu->addAction(obs_module_text(I18N_MENU_ABOUT.data()));
		_about_action->setMenuRole(QAction::NoRole);
		connect(_about_action, &QAction::triggered, this, &noice::ui::ui::menu_about_triggered);

		{ // Add an actual Menu entry.
			_menu_action = new QAction(main_widget);
			_menu_action->setMenuRole(QAction::NoRole);
			_menu_action->setMenu(_menu);
			_menu_action->setText(QT_UTF8(obs_module_text(I18N_MENU.data())));

			// Insert the new menu right before the Help menu.
			QList<QMenu *> obs_menus = main_widget->menuBar()->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly);
			if (QMenu *help_menu = obs_menus.at(1); help_menu) {
				main_widget->menuBar()->insertAction(help_menu->menuAction(), _menu_action);
			} else {
				main_widget->menuBar()->addAction(_menu_action);
			}
		}
	}

#if 0
	{ // Chat Dock
		_chat_dock = QSharedPointer<noice::ui::dock::chat>::create();
		_chat_dock_action = _chat_dock->add_obs_dock();
	}

	{ // Event List Dock
		_eventlist_dock = QSharedPointer<noice::ui::dock::eventlist>::create();
		_eventlist_dock_action = _eventlist_dock->add_obs_dock();
	}
#endif
	{ // Stats Dock
		_stats_dock = QSharedPointer<noice::ui::dock::stats>::create();
		_stats_dock_action = _stats_dock->add_obs_dock();
	}
}

void noice::ui::ui::unload()
{
#if 0
	if (_chat_dock) { // Chat Dock
		_chat_dock->deleteLater();
		_chat_dock = nullptr;
		_chat_dock_action->deleteLater();
		_chat_dock_action = nullptr;
	}

	if (_eventlist_dock) { // Event List Dock
		_eventlist_dock->deleteLater();
		_eventlist_dock = nullptr;
		_eventlist_dock_action->deleteLater();
		_eventlist_dock_action = nullptr;
	}
#endif
	if (_stats_dock) { // Stats Dock
		_stats_dock->deleteLater();
		_stats_dock = nullptr;
		_stats_dock_action->deleteLater();
		_stats_dock_action = nullptr;
	}

	if (_menu) { // Noice Menu
		_update_action->deleteLater();
		_menu_action->deleteLater();
		_menu->deleteLater();
	}

	// Remove translator.
	QCoreApplication::removeTranslator(_translator);
}

void noice::ui::ui::menu_update_triggered(bool)
{
	QDesktopServices::openUrl(QUrl(QT_UTF8("https://github.com/noice-com/obs-noice/releases")));
}

void noice::ui::ui::menu_about_triggered(bool)
{
	QDesktopServices::openUrl(QUrl(QT_UTF8("https://noice.com")));
}

std::shared_ptr<noice::ui::ui> noice::ui::ui::_instance = nullptr;

void noice::ui::ui::initialize()
{
	if (!noice::ui::ui::_instance)
		noice::ui::ui::_instance = std::make_shared<noice::ui::ui>();
}

void noice::ui::ui::finalize()
{
	noice::ui::ui::_instance.reset();
}

std::shared_ptr<noice::ui::ui> noice::ui::ui::instance()
{
	return noice::ui::ui::_instance;
}
