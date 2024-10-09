// Copyright (C) 2024 Noice Inc.
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

#include "ui-dock-stats.hpp"
#include "ui-frame-stats.hpp"
#include "noice-bridge.hpp"
#include <QMainWindow>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <string_view>
#include "common.hpp"

constexpr std::string_view I18N_STATS = "Dock.Stats";

constexpr std::string_view CFG_STATS_FIRSTRUN = "dock.stats.firstrun";
constexpr std::string_view CFG_STATS_FLOATING = "dock.stats.floating";

noice::ui::dock::stats::stats() : QDockWidget(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()))
{
	noice::ui::frame::basicstats *statsDlg = new noice::ui::frame::basicstats(this, false);
	setWidget(statsDlg);

	setAttribute(Qt::WA_NativeWindow);
	setMaximumSize(std::numeric_limits<int16_t>::max(), std::numeric_limits<int16_t>::max());
	setWindowTitle(QT_UTF8(obs_module_text(I18N_STATS.data())));
	setObjectName("noice::stats");

	// Dock functionality
	setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	setAllowedAreas(Qt::AllDockWidgetAreas);

	auto cfg = noice::get_bridge()->configuration_instance();

	{ // Check if this is the first time the user runs this plug-in.
		auto data = cfg->get();

		obs_data_set_default_bool(data.get(), CFG_STATS_FIRSTRUN.data(), true);
		obs_data_set_default_bool(data.get(), CFG_STATS_FLOATING.data(), true);

		setFloating(obs_data_get_bool(data.get(), CFG_STATS_FLOATING.data()));
	}

	// Connect Signals
	connect(this, &QDockWidget::visibilityChanged, this, &stats::on_visibilityChanged);
	connect(this, &QDockWidget::topLevelChanged, this, &stats::on_topLevelChanged);

	// Hide initially.
	hide();
}

noice::ui::dock::stats::~stats() {}

QAction *noice::ui::dock::stats::add_obs_dock()
{
	COMPILER_WARNINGS_PUSH
#if COMPILER_MSVC
	COMPILER_WARNINGS_DISABLE(4996)
#else
	COMPILER_WARNINGS_DISABLE("-Wdeprecated-declarations")
#endif
	QAction *action = reinterpret_cast<QAction *>(obs_frontend_add_dock(this));
	COMPILER_WARNINGS_POP
	action->setObjectName("noice::stats::action");
	action->setText(windowTitle());

	{ // Restore Dock Status
		auto mw = reinterpret_cast<QMainWindow *>(obs_frontend_get_main_window());
		mw->restoreDockWidget(this);
	}

	auto cfg = noice::get_bridge()->configuration_instance();

	{ // Is this the first time the user runs the plugin?
		auto data = cfg->get();

		if (obs_data_get_bool(data.get(), CFG_STATS_FIRSTRUN.data())) {
			obs_data_set_bool(data.get(), CFG_STATS_FIRSTRUN.data(), false);
			cfg->save();
		}
	}

	return action;
}

void noice::ui::dock::stats::closeEvent(QCloseEvent *event)
{
	event->ignore();
	hide();
}

void noice::ui::dock::stats::on_visibilityChanged(bool visible) {}

void noice::ui::dock::stats::on_topLevelChanged(bool topLevel)
{
	auto cfg = noice::get_bridge()->configuration_instance();
	auto data = cfg->get();
	obs_data_set_bool(data.get(), CFG_STATS_FLOATING.data(), topLevel);
	cfg->save();
}
