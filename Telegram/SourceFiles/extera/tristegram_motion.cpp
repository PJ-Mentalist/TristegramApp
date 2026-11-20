/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_motion.h"

#include "core/application.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "mainwindow.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/popup_menu.h"
#include "base/event_filter.h"
#include "window/window_controller.h"
#include "styles/style_basic.h"

#include <QtCore/QPointer>

#include <algorithm>
#include <cmath>

namespace Tristegram {
namespace {

constexpr auto kFadeDuration = crl::time(360);
constexpr auto kSplashDuration = crl::time(1150);

QPointer<Ui::RpWidget> CurrentFade;

[[nodiscard]] double EaseOutBack(double t) {
	constexpr auto c1 = 1.70158;
	constexpr auto c3 = c1 + 1.;
	const auto x = t - 1.;
	return 1. + c3 * x * x * x + c1 * x * x;
}

[[nodiscard]] Ui::RpWidget *MakeOverlay(not_null<QWidget*> parent) {
	const auto raw = Ui::CreateChild<Ui::RpWidget>(parent.get());
	raw->setAttribute(Qt::WA_TransparentForMouseEvents);
	raw->setGeometry(QRect(QPoint(), parent->size()));
	return raw;
}

} // namespace

void PrepareThemeFade() {
	if (!Extera::Bool(u"tristegram_theme_fade"_q)) {
		return;
	}
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	const auto widget = window->widget();
	const auto body = widget->body();
	if (!widget->isVisible()
		|| widget->isMinimized()
		|| body->width() <= 0
		|| body->height() <= 0) {
		return;
	}
	// From the screen, not by drawing the widgets again: some of them
	// do not survive being drawn into a picture.
	auto grabbed = QRect();
	const auto global = QRect(body->mapToGlobal(QPoint()), body->size());
	const auto image = Ui::PopupMenu::GrabScreen(global, &grabbed);
	if (image.isNull() || grabbed != global) {
		return;
	}
	const auto picture = QPixmap::fromImage(image);
	if (CurrentFade) {
		delete CurrentFade.data();
	}
	const auto raw = MakeOverlay(body);
	CurrentFade = raw;
	const auto animation = raw->lifetime().make_state<
		Ui::Animations::Simple>();
	raw->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(raw);
		p.setOpacity(animation->value(0.));
		p.drawPixmap(0, 0, picture);
	}, raw->lifetime());
	raw->show();
	raw->raise();
	animation->start([=] {
		raw->update();
		if (!animation->animating()) {
			raw->hide();
			raw->deleteLater();
		}
	}, 1., 0., kFadeDuration, anim::easeOutCirc);
}

void PrepareChatTransition(not_null<QWidget*> widget) {
	const auto kind = Extera::Int(u"tristegram_transition"_q);
	if (kind <= 0
		|| kind > 4
		|| Extera::Bool(u"tristegram_eco"_q)
		|| !widget->isVisible()
		|| widget->width() <= 0
		|| widget->height() <= 0) {
		return;
	}
	auto grabbed = QRect();
	const auto global = QRect(widget->mapToGlobal(QPoint()), widget->size());
	const auto image = Ui::PopupMenu::GrabScreen(global, &grabbed);
	if (image.isNull() || grabbed != global) {
		return;
	}
	const auto picture = QPixmap::fromImage(image);
	const auto raw = MakeOverlay(widget);
	const auto animation = raw->lifetime().make_state<
		Ui::Animations::Simple>();
	raw->paintRequest() | rpl::on_next([=] {
		const auto t = animation->value(0.);
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		const auto w = raw->width();
		const auto h = raw->height();
		switch (kind) {
		case 1: { // Slide: the old chat goes left.
			p.setOpacity(1. - t * 0.6);
			p.drawPixmap(QPointF(-w * 0.35 * t, 0.), picture);
		} break;
		case 2: { // Fade.
			p.setOpacity(1. - t);
			p.drawPixmap(0, 0, picture);
		} break;
		case 3: { // Scale: the old chat shrinks into the screen.
			const auto scale = 1. - 0.12 * t;
			p.setOpacity(1. - t);
			p.translate(w / 2., h / 2.);
			p.scale(scale, scale);
			p.translate(-w / 2., -h / 2.);
			p.drawPixmap(0, 0, picture);
		} break;
		case 4: { // Flip: the old chat turns away like a page.
			auto transform = QTransform();
			transform.translate(0., h / 2.);
			transform.rotate(-88. * t, Qt::YAxis);
			transform.translate(0., -h / 2.);
			p.setTransform(transform);
			p.setOpacity(1. - t * 0.5);
			p.drawPixmap(0, 0, picture);
		} break;
		}
	}, raw->lifetime());
	raw->show();
	raw->raise();
	// The new chat may put its parts on top, stay above them.
	crl::on_main(raw, [=] { raw->raise(); });
	animation->start([=] {
		raw->update();
		if (!animation->animating()) {
			raw->hide();
			raw->deleteLater();
		}
	}, 0., 1., (kind == 4) ? 320 : 240, anim::easeOutCirc);
}

void ShowSplash(not_null<QWidget*> parent) {
	static auto shown = false;
	if (shown || !Extera::Bool(u"tristegram_splash"_q)) {
		return;
	}
	shown = true;
	const auto raw = MakeOverlay(parent);
	const auto animation = raw->lifetime().make_state<
		Ui::Animations::Simple>();
	raw->paintRequest() | rpl::on_next([=] {
		const auto t = animation->value(1.);
		const auto fade = (t < 0.62) ? 1. : std::max(1. - (t - 0.62) / 0.38, 0.);
		auto p = QPainter(raw);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(fade);
		p.fillRect(raw->rect(), st::windowBg);

		const auto appear = std::clamp(t / 0.28, 0., 1.);
		const auto scale = 0.72 + 0.28 * EaseOutBack(appear);
		const auto side = std::clamp(
			std::min(raw->width(), raw->height()) * 0.2,
			72.,
			168.) * scale;
		const auto center = QPointF(raw->width() / 2., raw->height() * 0.46);
		const auto logo = QRectF(
			center.x() - side / 2.,
			center.y() - side / 2.,
			side,
			side);
		p.setOpacity(fade * appear);
		Tristegram::PaintLogo(
			p,
			logo,
			Tristegram::CurrentLogoStyle(crl::now()),
			std::clamp((t - 0.12) / 0.55, 0., 1.));

		const auto words = std::clamp((t - 0.2) / 0.25, 0., 1.);
		if (words > 0.) {
			p.setOpacity(fade * words);
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFg);
			const auto top = logo.bottom()
				+ style::ConvertScale(14)
				+ (1. - words) * style::ConvertScale(8);
			p.drawText(
				QRectF(0, top, raw->width(), st::semiboldFont->height * 1.5),
				(Qt::AlignTop | Qt::AlignHCenter).toInt(),
				u"Tristegram"_q);
		}
	}, raw->lifetime());
	base::install_event_filter(raw, parent, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			raw->setGeometry(QRect(QPoint(), parent->size()));
		}
		return base::EventFilterResult::Continue;
	});
	raw->show();
	raw->raise();
	animation->start([=] {
		raw->update();
		if (!animation->animating()) {
			raw->hide();
			raw->deleteLater();
		}
	}, 0., 1., kSplashDuration, anim::linear);
}

} // namespace Tristegram
