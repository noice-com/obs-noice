// Copyright (C) 2023 Noice Inc.
//
// Taken and modified from own3dpro-obs-plugin
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

#include "ui-dock-chat.hpp"
#include "noice-bridge.hpp"
#include <QMainWindow>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <string_view>
#include "common.hpp"

constexpr std::string_view I18N_CHAT = "Dock.Chat";

constexpr std::string_view CFG_CHAT_FIRSTRUN = "dock.chat.firstrun";
constexpr std::string_view CFG_CHAT_FLOATING = "dock.chat.floating";

static void service_changed(void *param, calldata_t *data)
{
	noice::ui::dock::chat *self = reinterpret_cast<noice::ui::dock::chat *>(param);
	auto cfg = noice::get_bridge()->configuration_instance();
	bool deployment_changed = calldata_bool(data, "deployment_changed");

	if (cfg->noice_service_selected() && deployment_changed)
		self->reset_session();
}

noice::ui::dock::chat::chat() : QDockWidget(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()))
{
	_browser = obs::browser::instance()->create_widget(this, "");
	_browser->setMinimumSize(300, 170);

	setWidget(_browser);
	setAttribute(Qt::WA_NativeWindow);
	setMaximumSize(std::numeric_limits<int16_t>::max(), std::numeric_limits<int16_t>::max());
	setWindowTitle(QT_UTF8(obs_module_text(I18N_CHAT.data())));
	setObjectName("noice::chat");

	// Dock functionality
	setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	setAllowedAreas(Qt::AllDockWidgetAreas);

	auto cfg = noice::get_bridge()->configuration_instance();

	{ // Check if this is the first time the user runs this plug-in.
		auto data = cfg->get();

		obs_data_set_default_bool(data.get(), CFG_CHAT_FIRSTRUN.data(), true);
		obs_data_set_default_bool(data.get(), CFG_CHAT_FLOATING.data(), true);

		setFloating(obs_data_get_bool(data.get(), CFG_CHAT_FLOATING.data()));
	}

	// Connect Signals
	connect(this, &QDockWidget::visibilityChanged, this, &chat::on_visibilityChanged);
	connect(this, &QDockWidget::topLevelChanged, this, &chat::on_topLevelChanged);

	reset_session();
	signal_handler_connect(cfg->get_signal_handler(), "service", service_changed, this);

	// Hide initially.
	hide();
}

noice::ui::dock::chat::~chat()
{
	auto cfg = noice::get_bridge()->configuration_instance();
	if (cfg != nullptr)
		signal_handler_disconnect(cfg->get_signal_handler(), "service", service_changed, this);
}

QAction *noice::ui::dock::chat::add_obs_dock()
{
	COMPILER_WARNINGS_PUSH
#if COMPILER_MSVC
	COMPILER_WARNINGS_DISABLE(4996)
#else
	COMPILER_WARNINGS_DISABLE("-Wdeprecated-declarations")
#endif
	QAction *action = reinterpret_cast<QAction *>(obs_frontend_add_dock(this));
	COMPILER_WARNINGS_POP
	action->setObjectName("noice::chat::action");
	action->setText(windowTitle());

	{ // Restore Dock Status
		auto mw = reinterpret_cast<QMainWindow *>(obs_frontend_get_main_window());
		mw->restoreDockWidget(this);
	}

	auto cfg = noice::get_bridge()->configuration_instance();

	{ // Is this the first time the user runs the plugin?
		auto data = cfg->get();

		if (obs_data_get_bool(data.get(), CFG_CHAT_FIRSTRUN.data())) {
			obs_data_set_bool(data.get(), CFG_CHAT_FIRSTRUN.data(), false);
			cfg->save();
		}
	}

	return action;
}

void noice::ui::dock::chat::reset_session()
{
	if (_browser) {
		auto bridge = noice::get_bridge();
		std::string token = std::string(bridge->get_unique_identifier());
		std::string url = bridge->get_web_endpoint("home?machine-token=" + token);
		_browser->setURL(url);
	}
}

void noice::ui::dock::chat::closeEvent(QCloseEvent *event)
{
	event->ignore();
	hide();
}

void noice::ui::dock::chat::on_visibilityChanged(bool visible) {}

void noice::ui::dock::chat::on_topLevelChanged(bool topLevel)
{
	auto cfg = noice::get_bridge()->configuration_instance();
	auto data = cfg->get();
	obs_data_set_bool(data.get(), CFG_CHAT_FLOATING.data(), topLevel);
	cfg->save();
}
