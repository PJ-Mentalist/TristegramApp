/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_notify_style.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/style_window.h"

#include <QtGui/QPainterPath>

#include <cmath>

namespace Tristegram {
namespace {

enum class Style {
	Classic,
	Glass,
	Neon,
	Aurora,
	Gradient,
	Stripe,
	Minimal,
	Hologram,
	Count,
};

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double t) {
	return QColor::fromRgbF(
		float(a.redF() + (b.redF() - a.redF()) * t),
		float(a.greenF() + (b.greenF() - a.greenF()) * t),
		float(a.blueF() + (b.blueF() - a.blueF()) * t),
		float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

[[nodiscard]] double Radius(QRectF rect) {
	const auto value = style::ConvertScale(std::clamp(
		Extera::Int(u"tristegram_notify_radius"_q),
		0,
		40));
	return std::min(double(value), std::min(rect.width(), rect.height()) / 2.);
}

[[nodiscard]] bool DarkBackground() {
	return st::notificationBg->c.lightnessF() < 0.5;
}

void PaintBorder(QPainter &p, const QPainterPath &shape, double alpha) {
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(Alpha(st::windowFg->c, alpha), 1.));
	p.drawPath(shape);
}

void PaintBlob(QPainter &p, QPointF center, double radius, QColor color) {
	auto gradient = QRadialGradient(center, std::max(radius, 1.));
	gradient.setColorAt(0., color);
	gradient.setColorAt(1., Alpha(color, 0.));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(center, radius, radius);
}

} // namespace

int NotifyStylesCount() {
	return int(Style::Count);
}

QString NotifyStyleName(int style) {
	switch (Style(style)) {
	case Style::Classic: return tr::lng_tgnf_classic(tr::now);
	case Style::Glass: return tr::lng_tgnf_glass(tr::now);
	case Style::Neon: return tr::lng_tgnf_neon(tr::now);
	case Style::Aurora: return tr::lng_tgnf_aurora(tr::now);
	case Style::Gradient: return tr::lng_tgnf_gradient(tr::now);
	case Style::Stripe: return tr::lng_tgnf_stripe(tr::now);
	case Style::Minimal: return tr::lng_tgnf_minimal(tr::now);
	case Style::Hologram: return tr::lng_tgnf_hologram(tr::now);
	case Style::Count: break;
	}
	return QString();
}

int NotifyStyle() {
	return std::clamp(
		Extera::Int(u"tristegram_notify_style"_q),
		0,
		int(Style::Count) - 1);
}

bool NotifyStyleAnimated(int style) {
	switch (Style(style)) {
	case Style::Neon:
	case Style::Aurora:
	case Style::Hologram:
		return !Extera::Bool(u"tristegram_eco"_q);
	default:
		return false;
	}
}

int NotifyAppear() {
	return std::clamp(Extera::Int(u"tristegram_notify_appear"_q), 0, 3);
}

crl::time NotifyAppearDuration() {
	return NotifyAppear() ? 460 : 0;
}

QPainterPath NotifyShape(QRectF rect) {
	const auto inner = rect.marginsRemoved(QMarginsF(0.5, 0.5, 0.5, 0.5));
	const auto radius = Radius(rect);
	auto result = QPainterPath();
	if (radius > 0.) {
		result.addRoundedRect(inner, radius, radius);
	} else {
		result.addRect(inner);
	}
	return result;
}

void PaintNotifyBackground(
		QPainter &p,
		QRectF rect,
		int style,
		crl::time now,
		bool over) {
	auto hq = PainterHighQualityEnabler(p);
	const auto shape = NotifyShape(rect);
	const auto bg = st::notificationBg->c;
	const auto logo = CurrentLogoStyle();
	const auto dark = DarkBackground();
	const auto w = rect.width();
	const auto h = rect.height();
	p.save();
	p.setPen(Qt::NoPen);
	switch (Style(style)) {
	case Style::Classic:
	case Style::Minimal:
		p.setBrush(bg);
		p.drawPath(shape);
		PaintBorder(p, shape, dark ? 0.1 : 0.07);
		break;
	case Style::Glass: {
		p.setBrush(Alpha(bg, 0.82));
		p.drawPath(shape);
		p.setClipPath(shape);
		auto shine = QLinearGradient(rect.topLeft(), QPointF(rect.x(), rect.y() + h * 0.6));
		shine.setColorAt(0., Alpha(QColor(255, 255, 255), dark ? 0.12 : 0.35));
		shine.setColorAt(1., Alpha(QColor(255, 255, 255), 0.));
		p.setBrush(shine);
		p.drawRect(rect);
		PaintBlob(p, QPointF(rect.x() + w * 0.85, rect.y()), h * 0.9, Alpha(logo.to, 0.14));
		p.setClipping(false);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(Alpha(QColor(255, 255, 255), dark ? 0.22 : 0.7), 1.));
		p.drawPath(shape);
	} break;
	case Style::Neon: {
		p.setBrush(dark ? bg : Mix(bg, QColor(0x0E, 0x10, 0x1A), 0.04));
		p.drawPath(shape);
		const auto angle = NotifyStyleAnimated(style)
			? std::fmod(now / 12., 360.)
			: 45.;
		auto ring = QConicalGradient(rect.center(), -angle);
		ring.setColorAt(0., logo.from);
		ring.setColorAt(0.5, logo.to);
		ring.setColorAt(1., logo.from);
		p.setClipPath(shape);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(ring), style::ConvertScale(8)));
		p.setOpacity(0.22);
		p.drawPath(shape);
		p.setOpacity(1.);
		p.setClipping(false);
		p.setPen(QPen(QBrush(ring), style::ConvertScale(2)));
		p.drawPath(NotifyShape(rect.marginsRemoved(QMarginsF(1, 1, 1, 1))));
	} break;
	case Style::Aurora: {
		p.setBrush(bg);
		p.drawPath(shape);
		p.setClipPath(shape);
		const auto t = NotifyStyleAnimated(style) ? (now / 1000.) : 0.;
		PaintBlob(
			p,
			QPointF(
				rect.x() + w * (0.2 + 0.12 * std::sin(t * 0.7)),
				rect.y() + h * (0.35 + 0.25 * std::cos(t * 0.9))),
			h * 1.3,
			Alpha(logo.from, dark ? 0.32 : 0.26));
		PaintBlob(
			p,
			QPointF(
				rect.x() + w * (0.75 + 0.12 * std::cos(t * 0.6)),
				rect.y() + h * (0.6 + 0.25 * std::sin(t * 0.8))),
			h * 1.4,
			Alpha(logo.to, dark ? 0.3 : 0.24));
		PaintBlob(
			p,
			QPointF(
				rect.x() + w * (0.5 + 0.2 * std::sin(t * 0.45 + 1.)),
				rect.y() + h * (0.1 + 0.2 * std::cos(t * 0.5 + 2.))),
			h,
			Alpha(Mix(logo.from, logo.to, 0.5), 0.18));
		p.setClipping(false);
		PaintBorder(p, shape, dark ? 0.12 : 0.08);
	} break;
	case Style::Gradient: {
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
		fill.setColorAt(0., Mix(bg, logo.from, dark ? 0.22 : 0.16));
		fill.setColorAt(1., Mix(bg, logo.to, dark ? 0.26 : 0.2));
		p.setBrush(fill);
		p.drawPath(shape);
		PaintBorder(p, shape, 0.08);
	} break;
	case Style::Stripe: {
		p.setBrush(bg);
		p.drawPath(shape);
		p.setClipPath(shape);
		auto stripe = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		stripe.setColorAt(0., logo.from);
		stripe.setColorAt(1., logo.to);
		p.setBrush(stripe);
		p.drawRect(QRectF(rect.x(), rect.y(), style::ConvertScale(4), h));
		auto glow = QLinearGradient(rect.topLeft(), QPointF(rect.x() + w * 0.4, rect.y()));
		glow.setColorAt(0., Alpha(logo.from, 0.12));
		glow.setColorAt(1., Alpha(logo.from, 0.));
		p.setBrush(glow);
		p.drawRect(rect);
		p.setClipping(false);
		PaintBorder(p, shape, dark ? 0.1 : 0.07);
	} break;
	case Style::Hologram: {
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
		fill.setColorAt(0., Mix(bg, logo.from, 0.08));
		fill.setColorAt(0.5, bg);
		fill.setColorAt(1., Mix(bg, logo.to, 0.1));
		p.setBrush(fill);
		p.drawPath(shape);
		p.setClipPath(shape);
		const auto period = 2800.;
		const auto phase = NotifyStyleAnimated(style)
			? (std::fmod(double(now), period) / period)
			: 0.45;
		const auto x = rect.x() - h + (w + 2 * h) * phase;
		auto band = QLinearGradient(
			QPointF(x - h * 0.6, rect.y()),
			QPointF(x + h * 0.6, rect.y() + h));
		band.setColorAt(0., Alpha(QColor(255, 255, 255), 0.));
		band.setColorAt(0.45, Alpha(logo.from, 0.12));
		band.setColorAt(0.5, Alpha(QColor(255, 255, 255), dark ? 0.16 : 0.5));
		band.setColorAt(0.55, Alpha(logo.to, 0.12));
		band.setColorAt(1., Alpha(QColor(255, 255, 255), 0.));
		p.setBrush(band);
		p.drawRect(rect);
		p.setClipping(false);
		PaintBorder(p, shape, dark ? 0.12 : 0.08);
	} break;
	case Style::Count:
		break;
	}
	if (over) {
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.03));
		p.drawPath(shape);
	}
	p.restore();
}

void ApplyNotifyAppear(
		QPainter &p,
		QRectF rect,
		double progress,
		bool fromRight) {
	if (progress >= 1.) {
		return;
	}
	const auto t = std::clamp(progress, 0., 1.);
	switch (NotifyAppear()) {
	case 1: {
		const auto eased = 1. - std::pow(1. - t, 3);
		p.translate((1. - eased) * rect.width() * 0.4 * (fromRight ? 1 : -1), 0);
	} break;
	case 2: {
		// easeOutBack, a small overshoot.
		const auto c = 1.9;
		const auto x = t - 1.;
		const auto eased = 1. + (c + 1.) * x * x * x + c * x * x;
		const auto scale = 0.82 + 0.18 * eased;
		p.translate(rect.center());
		p.scale(scale, scale);
		p.translate(-rect.center());
	} break;
	case 3: {
		const auto eased = 1. - std::pow(1. - t, 3);
		p.translate(0, -(1. - eased) * rect.height() * 0.5);
	} break;
	}
}

void PaintNotifyStylePreview(
		QPainter &p,
		QRectF rect,
		int style,
		crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto card = QRectF(
		rect.x() - rect.width() * 0.2,
		rect.y() + rect.height() * 0.2,
		rect.width() * 1.4,
		rect.height() * 0.6);
	p.save();
	PaintNotifyBackground(p, card, style, now);
	const auto logo = CurrentLogoStyle();
	const auto avatar = card.height() * 0.56;
	const auto left = card.x() + card.height() * 0.22;
	auto fill = QLinearGradient(
		QPointF(left, card.center().y() - avatar / 2),
		QPointF(left + avatar, card.center().y() + avatar / 2));
	fill.setColorAt(0., logo.from);
	fill.setColorAt(1., logo.to);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawEllipse(QRectF(left, card.center().y() - avatar / 2, avatar, avatar));
	const auto lines = left + avatar + card.height() * 0.18;
	const auto lineHeight = std::max(2., card.height() * 0.1);
	p.setBrush(Alpha(st::windowFg->c, 0.7));
	p.drawRoundedRect(
		QRectF(lines, card.center().y() - lineHeight * 1.6, card.width() * 0.36, lineHeight),
		lineHeight / 2,
		lineHeight / 2);
	p.setBrush(Alpha(st::windowSubTextFg->c, 0.6));
	p.drawRoundedRect(
		QRectF(lines, card.center().y() + lineHeight * 0.6, card.width() * 0.5, lineHeight),
		lineHeight / 2,
		lineHeight / 2);
	p.restore();
}

} // namespace Tristegram
