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

#pragma once
#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QHBoxLayout>
#include "obs/obs-browser.hpp"

namespace noice::ui::dock {
class eventlist : public QDockWidget {
	Q_OBJECT;

	QCefWidget *_browser;

public:
	explicit eventlist();
	~eventlist();

	QAction *add_obs_dock();
	void reset_session();

protected:
	void closeEvent(QCloseEvent *event) override;

protected Q_SLOTS:
	void on_visibilityChanged(bool visible);
	void on_topLevelChanged(bool topLevel);
};
} // namespace noice::ui::dock
