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

#pragma once
#include <QAction>
#include <QMenu>
#include <QSharedPointer>
#include <QTranslator>
#include <memory>
#include <obs-frontend-api.h>
#include "ui-dock-chat.hpp"
#include "ui-dock-eventlist.hpp"

namespace noice::ui {
class ui : public QObject {
	Q_OBJECT;

private:
	QTranslator *_translator;

	QMenu *_menu;
	QAction *_menu_action;
	QAction *_update_action;
	QAction *_about_action;

	QSharedPointer<dock::eventlist> _eventlist_dock;
	QAction *_eventlist_dock_action;

	QSharedPointer<dock::chat> _chat_dock;
	QAction *_chat_dock_action;

	bool _core_module_found;

public:
	~ui();
	ui();

private:
	static void obs_event_handler(obs_frontend_event event, void *private_data);

	void load();

	void unload();

private slots:
	; // Needed by some linters.

	void menu_update_triggered(bool);

	void menu_about_triggered(bool);

private /* Singleton */:
	static std::shared_ptr<noice::ui::ui> _instance;

public /* Singleton */:
	static void initialize();

	static void finalize();

	static std::shared_ptr<noice::ui::ui> instance();
};
} // namespace noice::ui
