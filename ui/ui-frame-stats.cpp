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

#include "ui-frame-stats.hpp"
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScreen>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include <string_view>
#include <string>
#include "common.hpp"

#define TIMER_INTERVAL 2000
#define REC_TIME_LEFT_INTERVAL 30000

constexpr std::string_view AITUM_MULTI_SERVICE = "aitum_multi_service_";

static void setThemeID(QWidget *widget, const QString &themeID)
{
	if (widget->property("themeID").toString() != themeID) {
		widget->setProperty("themeID", themeID);

		/* force style sheet recalculation */
		QString qss = widget->styleSheet();
		widget->setStyleSheet("/* */");
		widget->setStyleSheet(qss);
	}
}

static inline const char *Str(const char *lookup)
{
	return obs_frontend_get_locale_string(lookup);
}

static inline QString QTStr(const char *lookupVal)
{
	return QString::fromUtf8(Str(lookupVal));
}

void noice::ui::frame::basicstats::OBSFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	noice::ui::frame::basicstats *stats = reinterpret_cast<noice::ui::frame::basicstats *>(ptr);

	switch (event) {
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		stats->StartRecTimeLeft();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		stats->ResetRecTimeLeft();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// This is only reached when the non-closable (dock) stats
		// window is being cleaned up. The closable stats window is
		// already gone by this point as it's deleted on close.
		obs_frontend_remove_event_callback(OBSFrontendEvent, stats);
		break;
	default:
		break;
	}
}

static QString MakeTimeLeftText(int hours, int minutes)
{
	return QString::asprintf("%d %s, %d %s", hours, Str("Hours"), minutes, Str("Minutes"));
}

static QString MakeMissedFramesText(uint32_t total_lagged, uint32_t total_rendered, long double num)
{
	return QString("%1 / %2 (%3%)").arg(QString::number(total_lagged), QString::number(total_rendered), QString::number(num, 'f', 1));
}

noice::ui::frame::basicstats::basicstats(QWidget *parent, bool closable)
	: QFrame(parent), cpu_info(os_cpu_usage_info_start()), timer(this), recTimeLeft(this)
{
	QVBoxLayout *mainLayout = new QVBoxLayout();
	QGridLayout *topLayout = new QGridLayout();
	outputLayout = new QGridLayout();

	bitrates.reserve(REC_TIME_LEFT_INTERVAL / TIMER_INTERVAL);

	int row = 0;

	auto newStatBare = [&](QString name, QWidget *label, int col) {
		QLabel *typeLabel = new QLabel(name, this);
		topLayout->addWidget(typeLabel, row, col);
		topLayout->addWidget(label, row++, col + 1);
	};

	auto newStat = [&](const char *strLoc, QWidget *label, int col) {
		std::string str = "Basic.Stats.";
		str += strLoc;
		newStatBare(QTStr(str.c_str()), label, col);
	};

	/* --------------------------------------------- */

	cpuUsage = new QLabel(this);
	hddSpace = new QLabel(this);
	recordTimeLeft = new QLabel(this);
	memUsage = new QLabel(this);

	QString str = MakeTimeLeftText(99999, 59);
	int textWidth = recordTimeLeft->fontMetrics().boundingRect(str).width();
	recordTimeLeft->setMinimumWidth(textWidth);

	newStat("CPUUsage", cpuUsage, 0);
	newStat("HDDSpaceAvailable", hddSpace, 0);
	newStat("DiskFullIn", recordTimeLeft, 0);
	newStat("MemoryUsage", memUsage, 0);

	fps = new QLabel(this);
	renderTime = new QLabel(this);
	skippedFrames = new QLabel(this);
	missedFrames = new QLabel(this);

	str = MakeMissedFramesText(999999, 999999, 99.99);
	textWidth = missedFrames->fontMetrics().boundingRect(str).width();
	missedFrames->setMinimumWidth(textWidth);

	row = 0;

	newStatBare("FPS", fps, 2);
	newStat("AverageTimeToRender", renderTime, 2);
	newStat("MissedFrames", missedFrames, 2);
	newStat("SkippedFrames", skippedFrames, 2);

	/* --------------------------------------------- */
	QPushButton *closeButton = nullptr;
	if (closable)
		closeButton = new QPushButton(QTStr("Close"));
	QPushButton *resetButton = new QPushButton(QTStr("Reset"));
	QHBoxLayout *buttonLayout = new QHBoxLayout;
	buttonLayout->addStretch();
	buttonLayout->addWidget(resetButton);
	if (closable)
		buttonLayout->addWidget(closeButton);

	/* --------------------------------------------- */

	int col = 0;
	auto addOutputCol = [&](const char *loc) {
		QLabel *label = new QLabel(QTStr(loc), this);
		label->setStyleSheet("font-weight: bold");
		outputLayout->addWidget(label, 0, col++);
	};

	addOutputCol("Basic.Settings.Output");
	addOutputCol("Basic.Stats.Status");
	addOutputCol("Basic.Stats.DroppedFrames");
	addOutputCol("Basic.Stats.MegabytesSent");
	addOutputCol("Basic.Stats.Bitrate");
	outputLayoutCullSize = outputLayout->count();

	/* --------------------------------------------- */

	QVBoxLayout *outputContainerLayout = new QVBoxLayout();
	outputContainerLayout->addLayout(outputLayout);
	outputContainerLayout->addStretch();

	QWidget *widget = new QWidget(this);
	widget->setLayout(outputContainerLayout);

	QScrollArea *scrollArea = new QScrollArea(this);
	scrollArea->setWidget(widget);
	scrollArea->setWidgetResizable(true);

	/* --------------------------------------------- */

	mainLayout->addLayout(topLayout);
	mainLayout->addWidget(scrollArea);
	mainLayout->addLayout(buttonLayout);
	setLayout(mainLayout);

	/* --------------------------------------------- */
	if (closable)
		connect(closeButton, &QPushButton::clicked, [this]() { close(); });
	connect(resetButton, &QPushButton::clicked, [this]() { Reset(); });

	resize(800, 280);

	setWindowTitle(QTStr("Basic.Stats"));
#ifdef __APPLE__
	setWindowIcon(QIcon::fromTheme("obs", QIcon(":/res/images/obs_256x256.png")));
#else
	setWindowIcon(QIcon::fromTheme("obs", QIcon(":/res/images/obs.png")));
#endif

	setWindowModality(Qt::NonModal);
	setAttribute(Qt::WA_DeleteOnClose, true);

	QObject::connect(&timer, &QTimer::timeout, this, &noice::ui::frame::basicstats::Update);
	timer.setInterval(TIMER_INTERVAL);

	if (isVisible())
		timer.start();

	Update();

	QObject::connect(&recTimeLeft, &QTimer::timeout, this, &noice::ui::frame::basicstats::RecordingTimeLeft);
	recTimeLeft.setInterval(REC_TIME_LEFT_INTERVAL);

	obs_frontend_add_event_callback(OBSFrontendEvent, this);

	if (obs_frontend_recording_active())
		StartRecTimeLeft();
}

void noice::ui::frame::basicstats::closeEvent(QCloseEvent *event)
{
	// This code is only reached when the non-dockable stats window is
	// manually closed or OBS is exiting.
	obs_frontend_remove_event_callback(OBSFrontendEvent, this);

	QWidget::closeEvent(event);
}

noice::ui::frame::basicstats::~basicstats()
{
	os_cpu_usage_info_destroy(cpu_info);
	qDeleteAll(outputLabels);
}

void noice::ui::frame::basicstats::AddOutputLabels(obs_weak_output_t *outputWeak, bool rec, QString name)
{
	OutputLabels *ol = new OutputLabels;

	ol->outputWeak = outputWeak;
	ol->rec = rec;
	ol->name = new QLabel(name, this);
	ol->status = new QLabel(this);
	ol->droppedFrames = new QLabel(this);
	ol->megabytesSent = new QLabel(this);
	ol->bitrate = new QLabel(this);

	outputLabels.push_back(std::move(ol));
	UpdateOutputLayout();
}

void noice::ui::frame::basicstats::UpdateOutputLayout()
{
	while (outputLayout->count() > outputLayoutCullSize) {
		auto item = outputLayout->itemAt(outputLayoutCullSize);
		if (item == nullptr)
			break;
		outputLayout->removeItem(item);
	}

	outputLabels.erase(std::remove_if(outputLabels.begin(), outputLabels.end(),
					  [](OutputLabels *ol) {
						  OBSOutputAutoRelease output = obs_weak_output_get_output(ol->outputWeak);
						  if (output) {
							  return false;
						  } else {
							  delete ol;
							  return true;
						  }
					  }),
			   outputLabels.end());

	int row = 0;
	for (auto ol : outputLabels) {
		row++;
		int col = 0;

		outputLayout->addWidget(ol->name, row, col++);
		ol->name->setVisible(true);
		outputLayout->addWidget(ol->status, row, col++);
		ol->status->setVisible(true);
		outputLayout->addWidget(ol->droppedFrames, row, col++);
		ol->droppedFrames->setVisible(true);
		outputLayout->addWidget(ol->megabytesSent, row, col++);
		ol->megabytesSent->setVisible(true);
		outputLayout->addWidget(ol->bitrate, row, col++);
		ol->bitrate->setVisible(true);
	}
}

static uint32_t first_encoded = 0xFFFFFFFF;
static uint32_t first_skipped = 0xFFFFFFFF;
static uint32_t first_rendered = 0xFFFFFFFF;
static uint32_t first_lagged = 0xFFFFFFFF;

void noice::ui::frame::basicstats::InitializeValues()
{
	video_t *video = obs_get_video();
	first_encoded = video_output_get_total_frames(video);
	first_skipped = video_output_get_skipped_frames(video);
	first_rendered = obs_get_total_frames();
	first_lagged = obs_get_lagged_frames();
}

static std::string ValueOrEmpty(const char *s)
{
	return s == nullptr ? std::string() : s;
}

std::string noice::ui::frame::basicstats::UrlToService(const std::string &url)
{
	std::string ret;

	obs_module_t *rtmp = obs_get_module("rtmp-services");
	if (rtmp == nullptr)
		return ret;

	const char *rtmp_services_json = obs_module_get_config_path(rtmp, "services.json");

	try {
		std::ifstream rtmp_stream(rtmp_services_json, std::ios::in);
		nlohmann::ordered_json rtmp_data = nlohmann::ordered_json::parse(rtmp_stream);
		rtmp_stream.close();

		nlohmann::ordered_json &rtmp_services = rtmp_data["services"];
		if (!rtmp_services.is_array())
			throw std::runtime_error("No services array");

		for (auto it = rtmp_services.begin(); it != rtmp_services.end();) {
			if (!ret.empty())
				break;

			auto name = it->at("name").get<std::string>();
			nlohmann::ordered_json &servers = it->at("servers");
			if (!servers.is_array())
				throw std::runtime_error("No servers array");
			for (auto it2 = servers.begin(); it2 != servers.end();) {
				auto surl = it2->at("url").get<std::string>();
				if (url == surl) {
					ret = name;
					break;
				}
				++it2;
			}
			++it;
		}
	} catch (std::exception const &ex) {
		DLOG_ERROR("%s", ex.what());
	} catch (...) {
		DLOG_ERROR("unknown error occurred");
	}

	bfree((void *)rtmp_services_json);
	return ret;
}

void noice::ui::frame::basicstats::Update()
{
	/* TODO: Un-hardcode */

	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	/* ------------------------------------------- */
	/* general usage                               */

	double curFPS = obs_get_active_fps();
	double obsFPS = (double)ovi.fps_num / (double)ovi.fps_den;

	QString str = QString::number(curFPS, 'f', 2);
	fps->setText(str);

	if (curFPS < (obsFPS * 0.8))
		setThemeID(fps, "error");
	else if (curFPS < (obsFPS * 0.95))
		setThemeID(fps, "warning");
	else
		setThemeID(fps, "");

	/* ------------------ */

	double usage = os_cpu_usage_info_query(cpu_info);
	str = QString::number(usage, 'g', 2) + QStringLiteral("%");
	cpuUsage->setText(str);

	/* ------------------ */

	const char *path = obs_frontend_get_current_record_output_path();

#define MBYTE (1024ULL * 1024ULL)
#define GBYTE (1024ULL * 1024ULL * 1024ULL)
#define TBYTE (1024ULL * 1024ULL * 1024ULL * 1024ULL)
	num_bytes = os_get_free_disk_space(path);
	bfree((void *)path);
	QString abrv = QStringLiteral(" MB");
	long double num;

	num = (long double)num_bytes / (1024.0l * 1024.0l);
	if (num_bytes > TBYTE) {
		num /= 1024.0l * 1024.0l;
		abrv = QStringLiteral(" TB");
	} else if (num_bytes > GBYTE) {
		num /= 1024.0l;
		abrv = QStringLiteral(" GB");
	}

	str = QString::number(num, 'f', 1) + abrv;
	hddSpace->setText(str);

	if (num_bytes < GBYTE)
		setThemeID(hddSpace, "error");
	else if (num_bytes < (5 * GBYTE))
		setThemeID(hddSpace, "warning");
	else
		setThemeID(hddSpace, "");

	/* ------------------ */

	num = (long double)os_get_proc_resident_size() / (1024.0l * 1024.0l);

	str = QString::number(num, 'f', 1) + QStringLiteral(" MB");
	memUsage->setText(str);

	/* ------------------ */

	num = (long double)obs_get_average_frame_time_ns() / 1000000.0l;

	str = QString::number(num, 'f', 1) + QStringLiteral(" ms");
	renderTime->setText(str);

	long double fpsFrameTime = (long double)ovi.fps_den * 1000.0l / (long double)ovi.fps_num;

	if (num > fpsFrameTime)
		setThemeID(renderTime, "error");
	else if (num > fpsFrameTime * 0.75l)
		setThemeID(renderTime, "warning");
	else
		setThemeID(renderTime, "");

	/* ------------------ */

	video_t *video = obs_get_video();
	uint32_t total_encoded = video_output_get_total_frames(video);
	uint32_t total_skipped = video_output_get_skipped_frames(video);

	if (total_encoded < first_encoded || total_skipped < first_skipped) {
		first_encoded = total_encoded;
		first_skipped = total_skipped;
	}
	total_encoded -= first_encoded;
	total_skipped -= first_skipped;

	num = total_encoded ? (long double)total_skipped / (long double)total_encoded : 0.0l;
	num *= 100.0l;

	str = QString("%1 / %2 (%3%)").arg(QString::number(total_skipped), QString::number(total_encoded), QString::number(num, 'f', 1));
	skippedFrames->setText(str);

	if (num > 5.0l)
		setThemeID(skippedFrames, "error");
	else if (num > 1.0l)
		setThemeID(skippedFrames, "warning");
	else
		setThemeID(skippedFrames, "");

	/* ------------------ */

	uint32_t total_rendered = obs_get_total_frames();
	uint32_t total_lagged = obs_get_lagged_frames();

	if (total_rendered < first_rendered || total_lagged < first_lagged) {
		first_rendered = total_rendered;
		first_lagged = total_lagged;
	}
	total_rendered -= first_rendered;
	total_lagged -= first_lagged;

	num = total_rendered ? (long double)total_lagged / (long double)total_rendered : 0.0l;
	num *= 100.0l;

	str = MakeMissedFramesText(total_lagged, total_rendered, num);
	missedFrames->setText(str);

	if (num > 5.0l)
		setThemeID(missedFrames, "error");
	else if (num > 1.0l)
		setThemeID(missedFrames, "warning");
	else
		setThemeID(missedFrames, "");

	/* ------------------------------------------- */
	/* recording/streaming stats                   */
	auto cb = [](void *param, obs_output_t *output) {
		noice::ui::frame::basicstats *self = reinterpret_cast<noice::ui::frame::basicstats *>(param);
		obs_weak_output_t *outputWeak = obs_output_get_weak_output(output);

		for (auto outputLabel : self->outputLabels) {
			if (outputLabel->outputWeak == outputWeak) {
				obs_weak_output_release(outputWeak);
				return true;
			}
		}

		obs_service_t *service = obs_output_get_service(output);
		if (!service) {
			std::string name = ValueOrEmpty(obs_output_get_name(output));

			// For backwards compatibility with the standard OBS Stats, ignore the rest
			if (name != "adv_ffmpeg_output" && name != "adv_file_output") {
				obs_weak_output_release(outputWeak);
				return true;
			}
			self->AddOutputLabels(outputWeak, true, QTStr("Basic.Stats.Output.Recording"));
		} else {
			obs_data_t *settings = obs_service_get_settings(service);
			// Service name from OBS
			std::string serviceName = ValueOrEmpty(obs_data_get_string(settings, "service"));
			obs_data_release(settings);

			// Service name from plugins like Aitum Multistream
			// Aitum Vertical / obs-multi-rtmp use naming convention with less info
			if (serviceName.empty()) {
				std::string name = ValueOrEmpty(obs_service_get_name(service));
				if (name.rfind(AITUM_MULTI_SERVICE, 0) == 0) {
					serviceName = name.substr(AITUM_MULTI_SERVICE.length());
				}
			}

			// Map against services known through services.json
			if (serviceName.empty()) {
				QString url(obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL));
				serviceName = self->UrlToService(url.toStdString());

				// Fallback to plain hostname
				if (serviceName.empty()) {
					QUrl qu(url);
					serviceName = qu.host().toStdString();
				}
			}

			if (serviceName.empty()) {
				self->AddOutputLabels(outputWeak, false, QTStr("Basic.Stats.Output.Stream"));
			} else {
				self->AddOutputLabels(outputWeak, false, QTStr(serviceName.c_str()));
			}
		}

		return true;
	};

	obs_enum_outputs(cb, (void *)this);

	for (auto outputLabel : outputLabels) {
		outputLabel->Update();
		if (outputLabel->rec && outputLabel->IsActive()) {
			long double kbps = outputLabel->kbps;
			bitrates.push_back(kbps);
		}
	}
}

void noice::ui::frame::basicstats::StartRecTimeLeft()
{
	if (recTimeLeft.isActive())
		ResetRecTimeLeft();

	recordTimeLeft->setText(QTStr("Calculating"));
	recTimeLeft.start();
}

void noice::ui::frame::basicstats::ResetRecTimeLeft()
{
	if (recTimeLeft.isActive()) {
		bitrates.clear();
		recTimeLeft.stop();
		recordTimeLeft->setText(QTStr(""));
	}
}

void noice::ui::frame::basicstats::RecordingTimeLeft()
{
	if (bitrates.empty())
		return;

	long double averageBitrate = accumulate(bitrates.begin(), bitrates.end(), 0.0) / (long double)bitrates.size();
	if (averageBitrate == 0)
		return;

	long double bytesPerSec = (averageBitrate / 8.0l) * 1000.0l;
	long double secondsUntilFull = (long double)num_bytes / bytesPerSec;

	bitrates.clear();

	int totalMinutes = (int)secondsUntilFull / 60;
	int minutes = totalMinutes % 60;
	int hours = totalMinutes / 60;

	QString text = MakeTimeLeftText(hours, minutes);
	recordTimeLeft->setText(text);
	recordTimeLeft->setMinimumWidth(recordTimeLeft->width());
}

void noice::ui::frame::basicstats::Reset()
{
	timer.start();

	first_encoded = 0xFFFFFFFF;
	first_skipped = 0xFFFFFFFF;
	first_rendered = 0xFFFFFFFF;
	first_lagged = 0xFFFFFFFF;

	for (auto outputLabel : outputLabels)
		outputLabel->Reset();
	Update();
}

noice::ui::frame::basicstats::OutputLabels::~OutputLabels()
{
	if (outputWeak != nullptr) {
		obs_weak_output_release(outputWeak);
		outputWeak = nullptr;
	}
	Detach();
}

void noice::ui::frame::basicstats::OutputLabels::Detach()
{
	if (name != nullptr) {
		name->setVisible(false);
		name->setParent(nullptr);
	}
	if (status != nullptr) {
		status->setVisible(false);
		status->setParent(nullptr);
	}
	if (droppedFrames != nullptr) {
		droppedFrames->setVisible(false);
		droppedFrames->setParent(nullptr);
	}
	if (megabytesSent != nullptr) {
		megabytesSent->setVisible(false);
		megabytesSent->setParent(nullptr);
	}
	if (bitrate != nullptr) {
		bitrate->setVisible(false);
		bitrate->setParent(nullptr);
	}
}

bool noice::ui::frame::basicstats::OutputLabels::IsActive()
{
	OBSOutputAutoRelease output = obs_weak_output_get_output(outputWeak);
	return output ? obs_output_active(output) : false;
}

void noice::ui::frame::basicstats::OutputLabels::Update()
{
	OBSOutputAutoRelease output = obs_weak_output_get_output(outputWeak);

	uint64_t totalBytes = output ? obs_output_get_total_bytes(output) : 0;
	uint64_t curTime = os_gettime_ns();
	uint64_t bytesSent = totalBytes;

	if (bytesSent < lastBytesSent)
		bytesSent = 0;
	if (bytesSent == 0)
		lastBytesSent = 0;

	uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
	long double timePassed = (long double)(curTime - lastBytesSentTime) / 1000000000.0l;
	kbps = (long double)bitsBetween / timePassed / 1000.0l;

	if (timePassed < 0.01l)
		kbps = 0.0l;

	QString str = QTStr("Basic.Stats.Status.Inactive");
	QString themeID;
	bool active = output ? obs_output_active(output) : false;
	if (rec) {
		if (active)
			str = QTStr("Basic.Stats.Status.Recording");
	} else {
		if (active) {
			bool reconnecting = output ? obs_output_reconnecting(output) : false;

			if (reconnecting) {
				str = QTStr("Basic.Stats.Status.Reconnecting");
				themeID = "error";
			} else {
				str = QTStr("Basic.Stats.Status.Live");
				themeID = "good";
			}
		}
	}

	status->setText(str);
	setThemeID(status, themeID);

	long double num = (long double)totalBytes / (1024.0l * 1024.0l);
	const char *unit = "MiB";
	if (num > 1024) {
		num /= 1024;
		unit = "GiB";
	}
	megabytesSent->setText(QString("%1 %2").arg(num, 0, 'f', 1).arg(unit));

	num = kbps;
	unit = "kb/s";
	if (num >= 10'000) {
		num /= 1000;
		unit = "Mb/s";
	}
	bitrate->setText(QString("%1 %2").arg(num, 0, 'f', 0).arg(unit));

	if (!rec) {
		int total = output ? obs_output_get_total_frames(output) : 0;
		int dropped = output ? obs_output_get_frames_dropped(output) : 0;

		if (total < first_total || dropped < first_dropped) {
			first_total = 0;
			first_dropped = 0;
		}

		total -= first_total;
		dropped -= first_dropped;

		num = total ? (long double)dropped / (long double)total * 100.0l : 0.0l;

		str = QString("%1 / %2 (%3%)").arg(QString::number(dropped), QString::number(total), QString::number(num, 'f', 1));
		droppedFrames->setText(str);

		if (num > 5.0l)
			setThemeID(droppedFrames, "error");
		else if (num > 1.0l)
			setThemeID(droppedFrames, "warning");
		else
			setThemeID(droppedFrames, "");
	}

	lastBytesSent = bytesSent;
	lastBytesSentTime = curTime;
}

void noice::ui::frame::basicstats::OutputLabels::Reset()
{
	OBSOutputAutoRelease output = obs_weak_output_get_output(outputWeak);

	first_total = obs_output_get_total_frames(output);
	first_dropped = obs_output_get_frames_dropped(output);
}

void noice::ui::frame::basicstats::showEvent(QShowEvent *)
{
	timer.start(TIMER_INTERVAL);
}

void noice::ui::frame::basicstats::hideEvent(QHideEvent *)
{
	timer.stop();
}
