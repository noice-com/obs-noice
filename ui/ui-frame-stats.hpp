// Copyright (C) 2024 Noice Inc.
//
// Taken and modified from OBS window-basic-stats
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

#include <obs.hpp>
#include <util/platform.h>
#include <util/util.hpp>
#include <obs-frontend-api.h>
#include <QPointer>
#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QList>

class QGridLayout;
class QCloseEvent;

namespace noice::ui::frame {
class basicstats : public QFrame {
	Q_OBJECT

	QLabel *fps = nullptr;
	QLabel *cpuUsage = nullptr;
	QLabel *hddSpace = nullptr;
	QLabel *recordTimeLeft = nullptr;
	QLabel *memUsage = nullptr;

	QLabel *renderTime = nullptr;
	QLabel *skippedFrames = nullptr;
	QLabel *missedFrames = nullptr;

	QGridLayout *outputLayout = nullptr;
	int outputLayoutCullSize = 0;

	os_cpu_usage_info_t *cpu_info = nullptr;

	QTimer timer;
	QTimer recTimeLeft;
	uint64_t num_bytes = 0;
	std::vector<long double> bitrates;

	struct OutputLabels {
		obs_weak_output_t *outputWeak = nullptr;
		bool rec = false;

		QPointer<QLabel> name;
		QPointer<QLabel> status;
		QPointer<QLabel> droppedFrames;
		QPointer<QLabel> megabytesSent;
		QPointer<QLabel> bitrate;

		uint64_t lastBytesSent = 0;
		uint64_t lastBytesSentTime = 0;

		int first_total = 0;
		int first_dropped = 0;

		~OutputLabels();
		void Detach();
		bool IsActive();
		void Update();
		void Reset();

		long double kbps = 0.0l;
	};

	QList<OutputLabels *> outputLabels;

	std::string UrlToService(const std::string &url);

	void AddOutputLabels(obs_weak_output_t *outputWeak, bool rec, QString name);
	void UpdateOutputLayout();
	void Update();

	virtual void closeEvent(QCloseEvent *event) override;

	static void OBSFrontendEvent(enum obs_frontend_event event, void *ptr);

public:
	basicstats(QWidget *parent = nullptr, bool closable = true);
	~basicstats();

	static void InitializeValues();

	void StartRecTimeLeft();
	void ResetRecTimeLeft();

private slots:
	void RecordingTimeLeft();

public slots:
	void Reset();

protected:
	virtual void showEvent(QShowEvent *event) override;
	virtual void hideEvent(QHideEvent *event) override;
};
} // namespace noice::ui::dock
