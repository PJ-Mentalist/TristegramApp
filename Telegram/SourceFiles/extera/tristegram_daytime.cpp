/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_daytime.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "core/application.h"
#include "extera/extera_config.h"
#include "mainwindow.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QPointer>

namespace Tristegram {
namespace {

enum class Phase {
	None,
	Day,
	Night,
};

struct State {
	base::Timer timer;
	Phase applied = Phase::None;
	QPointer<Ui::RpWidget> warm;
	QPointer<QWidget> warmParent;
	double warmAlpha = 0.;
	rpl::lifetime lifetime;
};

[[nodiscard]] State &Get() {
	static auto result = State();
	return result;
}

[[nodiscard]] int NightHour() {
	return std::clamp(Extera::Int(u"tristegram_daytime_night"_q), 18, 23);
}

[[nodiscard]] int MorningHour() {
	return std::clamp(Extera::Int(u"tristegram_daytime_morning"_q), 5, 10);
}

// Minutes since midnight.
[[nodiscard]] int NowMinutes() {
	const auto time = QTime::currentTime();
	return time.hour() * 60 + time.minute();
}

[[nodiscard]] Phase CurrentPhase() {
	const auto now = NowMinutes();
	const auto night = NightHour() * 60;
	const auto morning = MorningHour() * 60;
	return (now >= night || now < morning) ? Phase::Night : Phase::Day;
}

// How warm the light is: grows through the evening hours before the
// night, stays a little at night and goes away in the first morning hour.
[[nodiscard]] double WarmAlpha() {
	if (!Extera::Bool(u"tristegram_daytime_theme"_q)
		|| !Extera::Bool(u"tristegram_daytime_warm"_q)) {
		return 0.;
	}
	constexpr auto kEvening = 0.11;
	constexpr auto kNight = 0.06;
	const auto now = NowMinutes();
	const auto night = NightHour() * 60;
	const auto morning = MorningHour() * 60;
	const auto eveningStart = night - 4 * 60;
	if (now >= night || now < morning) {
		return kNight;
	} else if (now < morning + 60) {
		return kNight * (1. - (now - morning) / 60.);
	} else if (now >= eveningStart) {
		const auto t = std::clamp((now - eveningStart) / 90., 0., 1.);
		return kEvening * t;
	}
	return 0.;
}

void ApplyWarm() {
	auto &state = Get();
	const auto alpha = WarmAlpha();
	const auto window = Core::App().activePrimaryWindow();
	const auto body = window ? window->widget()->body().get() : nullptr;
	if (alpha <= 0. || !body) {
		if (state.warm) {
			state.warm->hide();
		}
		return;
	}
	if (!state.warm || state.warmParent.data() != body) {
		if (state.warm) {
			delete state.warm.data();
		}
		const auto raw = Ui::CreateChild<Ui::RpWidget>(body);
		raw->setAttribute(Qt::WA_TransparentForMouseEvents);
		raw->setObjectName(u"tristegram_warm"_q);
		raw->setGeometry(QRect(QPoint(), body->size()));
		raw->paintRequest() | rpl::on_next([=](QRect clip) {
			auto p = QPainter(raw);
			p.fillRect(
				clip,
				QColor(255, 140, 40, int(std::round(Get().warmAlpha * 255))));
		}, raw->lifetime());
		base::install_event_filter(raw, body, [=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Resize) {
				raw->setGeometry(QRect(QPoint(), body->size()));
			} else if (e->type() == QEvent::ChildAdded) {
				// Stay above the layers and boxes that come later.
				crl::on_main(raw, [=] { raw->raise(); });
			}
			return base::EventFilterResult::Continue;
		});
		state.warm = raw;
		state.warmParent = body;
	}
	if (state.warmAlpha != alpha || state.warm->isHidden()) {
		state.warmAlpha = alpha;
		state.warm->show();
		state.warm->raise();
		state.warm->update();
	}
}

void Check() {
	auto &state = Get();
	if (!Extera::Bool(u"tristegram_daytime_theme"_q)) {
		state.applied = Phase::None;
		ApplyWarm();
		return;
	}
	const auto phase = CurrentPhase();
	// Only when the phase changes, so switching by hand still works.
	if (phase != state.applied) {
		state.applied = phase;
		const auto background = Window::Theme::Background();
		const auto wantNight = (phase == Phase::Night);
		if (background->nightModeChangeAllowed()
			&& Window::Theme::IsNightMode() != wantNight) {
			Window::Theme::ToggleNightMode();
		}
	}
	ApplyWarm();
}

} // namespace

void InstallDaytimeTheme() {
	auto &state = Get();
	state.timer.setCallback([] { Check(); });
	state.timer.callEach(30 * crl::time(1000));
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_daytime"_q);
	}) | rpl::on_next([](const QString &) {
		Get().applied = Phase::None;
		Check();
	}, state.lifetime);
	// After the first window is shown.
	crl::on_main([] { Check(); });
}

} // namespace Tristegram
