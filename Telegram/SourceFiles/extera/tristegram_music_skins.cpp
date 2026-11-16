/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_music_skins.h"

#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtGui/QLinearGradient>
#include <QtGui/QPainterPath>
#include <QtGui/QRadialGradient>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tristegram::MusicSkins {
namespace {

constexpr auto kPi = 3.14159265358979323846;

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.) * color.alphaF());
	return color;
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double t) {
	return QColor::fromRgbF(
		float(a.redF() + (b.redF() - a.redF()) * t),
		float(a.greenF() + (b.greenF() - a.greenF()) * t),
		float(a.blueF() + (b.blueF() - a.blueF()) * t),
		float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

// Deterministic 0..1 numbers for stars and bars.
class Random final {
public:
	explicit Random(uint64 seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ULL) {
	}
	[[nodiscard]] double next() {
		_state = _state * 6364136223846793005ULL + 1442695040888963407ULL;
		return double((_state >> 33) % 10000) / 10000.;
	}

private:
	uint64 _state = 0;

};

// The cover cropped to fill `target`, or the logo gradient.
void DrawCoverIn(QPainter &p, const QRectF &target, const Args &args) {
	if (!args.cover.isNull()) {
		const auto &cover = args.cover;
		const auto ratio = target.width() / std::max(target.height(), 1.);
		auto sw = double(cover.width());
		auto sh = double(cover.height());
		if (sw / std::max(sh, 1.) > ratio) {
			sw = sh * ratio;
		} else {
			sh = sw / ratio;
		}
		const auto source = QRectF(
			(cover.width() - sw) / 2.,
			(cover.height() - sh) / 2.,
			sw,
			sh);
		p.drawImage(target, cover, source);
	} else {
		auto gradient = QLinearGradient(target.topLeft(), target.bottomRight());
		gradient.setColorAt(0., args.from);
		gradient.setColorAt(1., args.to);
		p.fillRect(target, gradient);
	}
}

void PaintNote(QPainter &p, QPointF center, double size, QColor color) {
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto s = size;
	p.drawEllipse(QPointF(center.x() - s * 0.35, center.y() + s * 0.45), s * 0.26, s * 0.2);
	p.drawRect(QRectF(center.x() - s * 0.13, center.y() - s * 0.55, s * 0.12, s * 1.0));
	p.drawRect(QRectF(center.x() - s * 0.13, center.y() - s * 0.55, s * 0.5, s * 0.14));
}

// A cassette: the cover as the label, turning reels in the window.
void PaintCassette(QPainter &p, QRectF rect, const Args &args) {
	const auto w = rect.width();
	const auto h = w * 0.64;
	const auto body = QRectF(rect.x(), rect.center().y() - h / 2., w, h);
	const auto radius = w * 0.06;

	// Shadow and shell.
	p.setPen(Qt::NoPen);
	for (auto i = 3; i != 0; --i) {
		p.setBrush(Alpha(args.from, 0.06 * i));
		p.drawRoundedRect(
			body.translated(0, w * 0.02 * i).marginsAdded(
				QMarginsF(w * 0.01 * i, 0, w * 0.01 * i, 0)),
			radius,
			radius);
	}
	auto shell = QLinearGradient(body.topLeft(), body.bottomLeft());
	shell.setColorAt(0., QColor(0x34, 0x34, 0x3C));
	shell.setColorAt(1., QColor(0x17, 0x17, 0x1C));
	p.setBrush(shell);
	p.drawRoundedRect(body, radius, radius);

	// The label with the cover.
	const auto label = QRectF(
		body.x() + w * 0.06,
		body.y() + h * 0.08,
		w * 0.88,
		h * 0.6);
	{
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(label, radius * 0.6, radius * 0.6);
		p.setClipPath(clip);
		DrawCoverIn(p, label, args);
		p.fillRect(label, QColor(0, 0, 0, 70));
		auto stripe = QLinearGradient(label.topLeft(), label.topRight());
		stripe.setColorAt(0., args.from);
		stripe.setColorAt(1., args.to);
		p.fillRect(
			QRectF(label.x(), label.y(), label.width(), label.height() * 0.2),
			stripe);
		p.restore();
	}
	p.setFont(st::semiboldFont->f);
	p.setPen(QColor(255, 255, 255));
	const auto titleRect = QRectF(
		label.x() + w * 0.04,
		label.y(),
		label.width() - w * 0.08,
		label.height() * 0.2);
	p.drawText(
		titleRect,
		Qt::AlignLeft | Qt::AlignVCenter,
		st::semiboldFont->elided(
			args.title.isEmpty() ? u"Tristegram"_q : args.title,
			int(titleRect.width())));

	// The window with the reels.
	const auto window = QRectF(
		body.center().x() - w * 0.3,
		label.y() + label.height() * 0.4,
		w * 0.6,
		label.height() * 0.46);
	p.setPen(QPen(QColor(255, 255, 255, 50), std::max(1., w / 200.)));
	p.setBrush(QColor(0x0E, 0x0E, 0x12, 220));
	p.drawRoundedRect(window, window.height() / 2., window.height() / 2.);
	const auto reel = window.height() * 0.36;
	const auto left = QPointF(window.x() + window.height() / 2. + w * 0.02, window.center().y());
	const auto right = QPointF(window.right() - window.height() / 2. - w * 0.02, window.center().y());
	const auto progress = std::clamp(args.seconds / 240., 0., 1.);
	const auto tapes = std::array<double, 2>{
		reel * (1.25 - 0.35 * progress),
		reel * (0.9 + 0.35 * progress),
	};
	const auto angle = args.seconds * 140.;
	const auto centers = std::array<QPointF, 2>{ left, right };
	for (auto k = 0; k != 2; ++k) {
		const auto c = centers[k];
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0x4A, 0x2E, 0x22));
		p.drawEllipse(c, std::min(tapes[k], window.height() * 0.48), std::min(tapes[k], window.height() * 0.48));
		p.setBrush(QColor(0xEE, 0xEE, 0xF2));
		p.drawEllipse(c, reel * 0.62, reel * 0.62);
		p.save();
		p.translate(c);
		p.rotate(angle);
		p.setBrush(QColor(0x22, 0x22, 0x28));
		for (auto i = 0; i != 6; ++i) {
			p.rotate(60.);
			p.drawRect(QRectF(-reel * 0.07, -reel * 0.5, reel * 0.14, reel * 0.22));
		}
		p.drawEllipse(QPointF(), reel * 0.2, reel * 0.2);
		p.restore();
	}

	// The bottom part with holes and the screws.
	auto bottom = QPainterPath();
	const auto by = body.bottom() - h * 0.2;
	bottom.moveTo(body.x() + w * 0.2, body.bottom());
	bottom.lineTo(body.x() + w * 0.25, by);
	bottom.lineTo(body.right() - w * 0.25, by);
	bottom.lineTo(body.right() - w * 0.2, body.bottom());
	bottom.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 16));
	p.drawPath(bottom);
	p.setBrush(QColor(0x0E, 0x0E, 0x12));
	for (const auto x : { 0.36, 0.45, 0.55, 0.64 }) {
		p.drawEllipse(
			QPointF(body.x() + w * x, by + h * 0.1),
			w * 0.014,
			w * 0.014);
	}
	p.setBrush(QColor(0x9A, 0x9A, 0xA4));
	const auto screw = w * 0.016;
	for (const auto &point : {
			QPointF(body.x() + w * 0.03, body.y() + w * 0.03),
			QPointF(body.right() - w * 0.03, body.y() + w * 0.03),
			QPointF(body.x() + w * 0.03, body.bottom() - w * 0.03),
			QPointF(body.right() - w * 0.03, body.bottom() - w * 0.03),
			QPointF(body.center().x(), body.bottom() - w * 0.03) }) {
		p.drawEllipse(point, screw, screw);
	}
}

void PaintRavenShape(QPainter &p, QPointF at, double size, double flap, QColor color) {
	p.save();
	p.translate(at);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto s = size;
	// Body, head, beak and tail.
	p.drawEllipse(QPointF(0, 0), s * 0.42, s * 0.16);
	p.drawEllipse(QPointF(s * 0.4, -s * 0.06), s * 0.13, s * 0.11);
	auto beak = QPainterPath();
	beak.moveTo(s * 0.5, -s * 0.09);
	beak.lineTo(s * 0.7, -s * 0.02);
	beak.lineTo(s * 0.5, s * 0.0);
	beak.closeSubpath();
	p.drawPath(beak);
	auto tail = QPainterPath();
	tail.moveTo(-s * 0.35, -s * 0.04);
	tail.lineTo(-s * 0.72, -s * 0.14);
	tail.lineTo(-s * 0.66, s * 0.02);
	tail.lineTo(-s * 0.72, s * 0.16);
	tail.lineTo(-s * 0.35, s * 0.06);
	tail.closeSubpath();
	p.drawPath(tail);
	// Wings: the far one a bit darker, both follow the flap.
	for (auto k = 0; k != 2; ++k) {
		const auto lift = flap * (k ? 0.85 : 1.);
		const auto tipY = -s * (0.15 + 0.75 * lift);
		const auto tipX = -s * (0.15 + 0.1 * k);
		auto wing = QPainterPath();
		wing.moveTo(s * 0.2, -s * 0.04);
		wing.cubicTo(
			QPointF(s * 0.05, tipY * 0.6),
			QPointF(tipX + s * 0.1, tipY),
			QPointF(tipX - s * 0.25, tipY - s * 0.05));
		wing.cubicTo(
			QPointF(tipX - s * 0.05, tipY * 0.45),
			QPointF(-s * 0.05, -s * 0.02),
			QPointF(-s * 0.15, s * 0.02));
		wing.closeSubpath();
		p.setBrush(k ? color.darker(130) : color);
		p.drawPath(wing);
	}
	p.restore();
}

// A raven flies over the moon with the cover on it.
void PaintRaven(QPainter &p, QRectF rect, const Args &args) {
	const auto center = rect.center();
	const auto radius = rect.width() / 2.;
	auto clip = QPainterPath();
	clip.addEllipse(rect);
	p.save();
	p.setClipPath(clip);

	auto sky = QRadialGradient(center, radius);
	sky.setColorAt(0., Mix(QColor(0x1A, 0x1C, 0x34), args.from, 0.18));
	sky.setColorAt(1., QColor(0x07, 0x07, 0x10));
	p.fillRect(rect, sky);

	// Stars that twinkle.
	auto random = Random(args.seed);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != 22; ++i) {
		const auto x = rect.x() + rect.width() * random.next();
		const auto y = rect.y() + rect.height() * random.next() * 0.8;
		const auto size = radius * (0.006 + 0.012 * random.next());
		const auto twinkle = 0.45 + 0.55 * std::abs(
			std::sin(args.seconds * (0.8 + random.next()) + i));
		p.setBrush(QColor(255, 255, 255, int(200 * twinkle)));
		p.drawEllipse(QPointF(x, y), size, size);
	}

	// The moon with a halo.
	const auto moon = QPointF(center.x() + radius * 0.18, center.y() - radius * 0.1);
	const auto moonRadius = radius * 0.4;
	auto halo = QRadialGradient(moon, moonRadius * 1.8);
	halo.setColorAt(0., Alpha(args.to, 0.35));
	halo.setColorAt(1., Alpha(args.to, 0.));
	p.setBrush(halo);
	p.drawEllipse(moon, moonRadius * 1.8, moonRadius * 1.8);
	{
		p.save();
		auto disc = QPainterPath();
		disc.addEllipse(moon, moonRadius, moonRadius);
		p.setClipPath(disc, Qt::IntersectClip);
		const auto target = QRectF(
			moon.x() - moonRadius,
			moon.y() - moonRadius,
			moonRadius * 2,
			moonRadius * 2);
		if (args.cover.isNull()) {
			auto light = QRadialGradient(
				QPointF(moon.x() - moonRadius * 0.3, moon.y() - moonRadius * 0.3),
				moonRadius * 1.4);
			light.setColorAt(0., QColor(0xFB, 0xF6, 0xE6));
			light.setColorAt(1., QColor(0xC9, 0xC2, 0xAE));
			p.fillRect(target, light);
			p.setBrush(QColor(0, 0, 0, 22));
			p.drawEllipse(QPointF(moon.x() + moonRadius * 0.3, moon.y() + moonRadius * 0.1), moonRadius * 0.18, moonRadius * 0.18);
			p.drawEllipse(QPointF(moon.x() - moonRadius * 0.25, moon.y() + moonRadius * 0.35), moonRadius * 0.12, moonRadius * 0.12);
		} else {
			DrawCoverIn(p, target, args);
			auto shade = QRadialGradient(moon, moonRadius);
			shade.setColorAt(0.7, QColor(0, 0, 0, 0));
			shade.setColorAt(1., QColor(0, 0, 0, 90));
			p.fillRect(target, shade);
		}
		p.restore();
	}

	// Hills at the bottom.
	auto hills = QPainterPath();
	hills.moveTo(rect.x(), rect.bottom());
	hills.lineTo(rect.x(), center.y() + radius * 0.62);
	hills.cubicTo(
		QPointF(rect.x() + radius * 0.6, center.y() + radius * 0.42),
		QPointF(center.x() + radius * 0.1, center.y() + radius * 0.78),
		QPointF(rect.right(), center.y() + radius * 0.56));
	hills.lineTo(rect.right(), rect.bottom());
	hills.closeSubpath();
	p.setBrush(QColor(0x05, 0x05, 0x09));
	p.drawPath(hills);

	// The raven crosses the sky again and again.
	const auto period = 7.5;
	const auto phase = std::fmod(args.seconds, period) / period;
	const auto size = radius * 0.42;
	const auto x = rect.x() - size + (rect.width() + 2 * size) * phase;
	const auto y = center.y() + radius * 0.05
		+ radius * 0.16 * std::sin(phase * 2 * kPi * 1.3);
	const auto flap = args.playing
		? (0.5 + 0.5 * std::sin(args.seconds * 9.))
		: 0.35;
	PaintRavenShape(p, QPointF(x, y), size, flap, QColor(0x04, 0x04, 0x06));
	p.restore();

	// The rim in the logo colors.
	auto rim = QLinearGradient(rect.topLeft(), rect.bottomRight());
	rim.setColorAt(0., args.from);
	rim.setColorAt(1., args.to);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), std::max(2., radius / 40.)));
	p.drawEllipse(rect.marginsRemoved(QMarginsF(1, 1, 1, 1)));
}

// A drip: the bag empties as the song goes, drops fall in the chamber.
void PaintDrip(QPainter &p, QRectF rect, const Args &args, double progress) {
	const auto s = rect.width();
	const auto cx = rect.center().x();
	const auto top = rect.y();
	const auto line = std::max(1.5, s / 110.);

	// The hook.
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(0xB8, 0xB8, 0xC4), line));
	p.drawEllipse(QPointF(cx, top + s * 0.03), s * 0.025, s * 0.025);
	p.drawLine(QPointF(cx, top + s * 0.055), QPointF(cx, top + s * 0.085));

	// The bag with the liquid.
	const auto bag = QRectF(cx - s * 0.2, top + s * 0.085, s * 0.4, s * 0.48);
	const auto bagRadius = s * 0.08;
	auto bagPath = QPainterPath();
	bagPath.addRoundedRect(bag, bagRadius, bagRadius);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 26));
	p.drawPath(bagPath);
	{
		p.save();
		p.setClipPath(bagPath);
		const auto level = 0.12 + 0.8 * (1. - std::clamp(progress, 0., 1.));
		const auto surface = bag.bottom() - bag.height() * level;
		auto liquid = QPainterPath();
		liquid.moveTo(bag.x(), bag.bottom());
		const auto steps = 24;
		for (auto i = 0; i <= steps; ++i) {
			const auto x = bag.x() + bag.width() * i / double(steps);
			const auto wave = args.playing
				? std::sin(i * 0.6 + args.seconds * 3.) * s * 0.006
				: 0.;
			liquid.lineTo(x, surface + wave);
		}
		liquid.lineTo(bag.right(), bag.bottom());
		liquid.closeSubpath();
		auto fill = QLinearGradient(bag.topLeft(), bag.bottomLeft());
		fill.setColorAt(0., Alpha(args.from, 0.85));
		fill.setColorAt(1., Alpha(args.to, 0.95));
		p.setBrush(fill);
		p.drawPath(liquid);
		// Bubbles going up while it plays.
		auto random = Random(args.seed + 7);
		p.setBrush(QColor(255, 255, 255, 70));
		for (auto i = 0; i != 6; ++i) {
			const auto x = bag.x() + bag.width() * (0.15 + 0.7 * random.next());
			const auto speed = 0.08 + 0.1 * random.next();
			const auto t = std::fmod(args.seconds * speed + random.next(), 1.);
			const auto y = bag.bottom() - (bag.bottom() - surface) * t;
			const auto r = s * (0.005 + 0.008 * random.next());
			p.drawEllipse(QPointF(x, y), r, r);
		}
		p.restore();
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 120), line));
	p.drawPath(bagPath);
	// The scale on the side.
	p.setPen(QPen(QColor(255, 255, 255, 110), line * 0.8));
	for (auto i = 1; i != 6; ++i) {
		const auto y = bag.y() + bag.height() * i / 6.;
		const auto len = (i % 2) ? s * 0.035 : s * 0.055;
		p.drawLine(QPointF(bag.right() - s * 0.02 - len, y), QPointF(bag.right() - s * 0.02, y));
	}
	// The label with the cover.
	{
		const auto label = QRectF(cx - s * 0.1, bag.y() + bag.height() * 0.22, s * 0.16, s * 0.16);
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(label, s * 0.02, s * 0.02);
		p.setClipPath(clip);
		DrawCoverIn(p, label, args);
		if (args.cover.isNull()) {
			PaintNote(p, label.center(), label.width() * 0.55, QColor(255, 255, 255, 230));
		}
		p.restore();
		p.setPen(QPen(QColor(255, 255, 255, 160), line * 0.7));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(label, s * 0.02, s * 0.02);
	}

	// The neck, the drip chamber and the tube.
	const auto neck = QRectF(cx - s * 0.025, bag.bottom(), s * 0.05, s * 0.05);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xD8, 0xD8, 0xE0, 200));
	p.drawRoundedRect(neck, s * 0.01, s * 0.01);
	const auto chamber = QRectF(cx - s * 0.05, neck.bottom(), s * 0.1, s * 0.17);
	p.setBrush(QColor(255, 255, 255, 30));
	p.setPen(QPen(QColor(255, 255, 255, 140), line));
	p.drawRoundedRect(chamber, s * 0.03, s * 0.03);
	const auto pool = chamber.bottom() - chamber.height() * 0.3;
	{
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(chamber, s * 0.03, s * 0.03);
		p.setClipPath(clip);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(args.to, 0.9));
		p.drawRect(QRectF(chamber.x(), pool, chamber.width(), chamber.bottom() - pool));
		// One drop each period: it swells, falls, splashes.
		const auto period = 0.9;
		const auto t = std::fmod(args.seconds, period) / period;
		const auto dropTop = chamber.y() + s * 0.012;
		const auto dropSize = s * 0.016;
		p.setBrush(Alpha(args.from, 0.95));
		if (t < 0.45) {
			const auto grow = t / 0.45;
			p.drawEllipse(QPointF(cx, dropTop + dropSize * grow * 0.6), dropSize * grow, dropSize * grow);
		} else if (t < 0.8) {
			const auto fall = (t - 0.45) / 0.35;
			const auto y = dropTop + (pool - dropTop - dropSize) * fall * fall;
			auto drop = QPainterPath();
			drop.moveTo(cx, y - dropSize * 1.6);
			drop.cubicTo(
				QPointF(cx + dropSize, y - dropSize * 0.4),
				QPointF(cx + dropSize, y + dropSize),
				QPointF(cx, y + dropSize));
			drop.cubicTo(
				QPointF(cx - dropSize, y + dropSize),
				QPointF(cx - dropSize, y - dropSize * 0.4),
				QPointF(cx, y - dropSize * 1.6));
			p.drawPath(drop);
		} else {
			const auto splash = (t - 0.8) / 0.2;
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(Alpha(QColor(255, 255, 255), 1. - splash), line));
			p.drawEllipse(QPointF(cx, pool), s * 0.04 * splash, s * 0.01 * splash);
		}
		p.restore();
	}
	auto tube = QPainterPath();
	tube.moveTo(cx, chamber.bottom());
	tube.cubicTo(
		QPointF(cx, rect.bottom() - s * 0.02),
		QPointF(cx + s * 0.3, rect.bottom() - s * 0.02),
		QPointF(cx + s * 0.34, rect.bottom() - s * 0.14));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 110), line * 1.6, Qt::SolidLine, Qt::RoundCap));
	p.drawPath(tube);
	p.setPen(QPen(Alpha(args.to, 0.8), line * 0.7, Qt::SolidLine, Qt::RoundCap));
	p.drawPath(tube);
	// The roller clamp.
	const auto clampAt = tube.pointAtPercent(0.35);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xE6, 0xE6, 0xEC));
	p.drawRoundedRect(
		QRectF(clampAt.x() - s * 0.03, clampAt.y() - s * 0.022, s * 0.06, s * 0.044),
		s * 0.01,
		s * 0.01);
	p.setBrush(args.from);
	p.drawEllipse(clampAt, s * 0.012, s * 0.012);
}

// Drops fall into a pool with the cover and make rings.
void PaintRipples(QPainter &p, QRectF rect, const Args &args) {
	const auto center = rect.center();
	const auto radius = rect.width() / 2.;
	auto clip = QPainterPath();
	clip.addEllipse(rect);
	p.save();
	p.setClipPath(clip);
	DrawCoverIn(p, rect, args);
	auto deep = QRadialGradient(center, radius);
	deep.setColorAt(0., QColor(0x06, 0x0A, 0x18, 120));
	deep.setColorAt(1., QColor(0x02, 0x04, 0x0A, 210));
	p.fillRect(rect, deep);

	const auto line = std::max(1.5, radius / 60.);
	const auto period = 2.4;
	for (auto k = 0; k != 3; ++k) {
		const auto t = std::fmod(args.seconds + k * period / 3., period) / period;
		for (auto ring = 0; ring != 3; ++ring) {
			const auto q = t - ring * 0.12;
			if (q <= 0.) {
				continue;
			}
			const auto r = radius * 0.95 * q;
			const auto alpha = (1. - q) * (ring ? 0.5 : 0.9);
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(
				Alpha(ring % 2 ? args.to : args.from, alpha),
				line * (1. - q * 0.5)));
			p.drawEllipse(center, r, r * 0.92);
		}
	}
	// The next drop falls from above.
	{
		const auto t = std::fmod(args.seconds + period * 0.82, period / 3.) / (period / 3.);
		if (t < 0.5) {
			const auto y = rect.y() + (center.y() - rect.y()) * (t / 0.5) * (t / 0.5);
			const auto size = radius * 0.045;
			p.setPen(Qt::NoPen);
			auto fill = QLinearGradient(QPointF(center.x(), y - size * 2), QPointF(center.x(), y + size));
			fill.setColorAt(0., QColor(255, 255, 255, 60));
			fill.setColorAt(1., QColor(255, 255, 255, 230));
			p.setBrush(fill);
			auto drop = QPainterPath();
			drop.moveTo(center.x(), y - size * 1.8);
			drop.cubicTo(
				QPointF(center.x() + size, y - size * 0.4),
				QPointF(center.x() + size, y + size),
				QPointF(center.x(), y + size));
			drop.cubicTo(
				QPointF(center.x() - size, y + size),
				QPointF(center.x() - size, y - size * 0.4),
				QPointF(center.x(), y - size * 1.8));
			p.drawPath(drop);
		}
	}
	p.restore();
	auto rim = QLinearGradient(rect.topLeft(), rect.bottomRight());
	rim.setColorAt(0., args.from);
	rim.setColorAt(1., args.to);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), std::max(2., radius / 40.)));
	p.drawEllipse(rect.marginsRemoved(QMarginsF(1, 1, 1, 1)));
}

// The cover in the middle, planets on orbits and a ring of bars.
void PaintOrbits(QPainter &p, QRectF rect, const Args &args) {
	const auto center = rect.center();
	const auto radius = rect.width() / 2.;
	const auto inner = radius * 0.4;

	// Bars around the cover.
	const auto bars = 56;
	auto random = Random(args.seed + 3);
	for (auto i = 0; i != bars; ++i) {
		const auto angle = 2 * kPi * i / bars;
		const auto noise = random.next();
		const auto value = args.playing
			? 0.25 + 0.75 * std::abs(
				std::sin(args.seconds * (2.2 + noise * 2.) + i * 0.55)
				* std::sin(args.seconds * 1.3 + noise * 6.))
			: 0.12 + 0.1 * noise;
		const auto from = inner + radius * 0.05;
		const auto to = from + radius * 0.22 * value;
		const auto dir = QPointF(std::cos(angle), std::sin(angle));
		p.setPen(QPen(
			Mix(args.from, args.to, i / double(bars)),
			std::max(2., radius / 45.),
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(center + dir * from, center + dir * to);
	}

	// Orbits with planets.
	const auto orbits = std::array<double, 3>{ 0.74, 0.86, 0.97 };
	const auto speeds = std::array<double, 3>{ 0.23, -0.15, 0.09 };
	for (auto k = 0; k != 3; ++k) {
		const auto r = radius * orbits[k];
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, 30), 1.));
		p.drawEllipse(center, r, r);
		const auto angle = 2 * kPi * (args.seconds * speeds[k] + k * 0.31);
		const auto planet = center + QPointF(std::cos(angle), std::sin(angle)) * r;
		const auto size = radius * (0.035 + 0.012 * k);
		auto glow = QRadialGradient(planet, size * 3.);
		const auto color = k == 1 ? args.to : args.from;
		glow.setColorAt(0., Alpha(color, 0.6));
		glow.setColorAt(1., Alpha(color, 0.));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(planet, size * 3., size * 3.);
		p.setBrush(color.lighter(130));
		p.drawEllipse(planet, size, size);
	}

	// The cover turns slowly.
	p.save();
	p.translate(center);
	p.rotate(args.seconds * 12.);
	const auto target = QRectF(-inner, -inner, inner * 2, inner * 2);
	auto clip = QPainterPath();
	clip.addEllipse(target);
	p.setClipPath(clip);
	DrawCoverIn(p, target, args);
	if (args.cover.isNull()) {
		PaintNote(p, QPointF(), inner * 0.7, QColor(255, 255, 255, 230));
	}
	p.restore();
	auto rim = QLinearGradient(
		QPointF(center.x() - inner, center.y() - inner),
		QPointF(center.x() + inner, center.y() + inner));
	rim.setColorAt(0., args.from);
	rim.setColorAt(1., args.to);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), std::max(2., radius / 35.)));
	p.drawEllipse(center, inner, inner);
}

// Just the cover: a big rounded square that floats a little.
void PaintCover(QPainter &p, QRectF rect, const Args &args) {
	const auto s = rect.width() * 0.9;
	const auto bob = args.playing ? std::sin(args.seconds * 1.4) * rect.width() * 0.015 : 0.;
	const auto target = QRectF(
		rect.center().x() - s / 2.,
		rect.center().y() - s / 2. + bob,
		s,
		s);
	const auto radius = s * 0.09;
	p.setPen(Qt::NoPen);
	for (auto i = 4; i != 0; --i) {
		p.setBrush(Alpha(args.from, 0.05 * i));
		p.drawRoundedRect(
			target.translated(0, s * 0.03).marginsAdded(
				QMarginsF(s * 0.012 * (5 - i), s * 0.012 * (5 - i), s * 0.012 * (5 - i), s * 0.012 * (5 - i))),
			radius * 1.2,
			radius * 1.2);
	}
	p.save();
	auto clip = QPainterPath();
	clip.addRoundedRect(target, radius, radius);
	p.setClipPath(clip);
	DrawCoverIn(p, target, args);
	if (args.cover.isNull()) {
		PaintNote(p, target.center(), s * 0.35, QColor(255, 255, 255, 230));
	}
	auto sheen = QLinearGradient(target.topLeft(), target.bottomRight());
	sheen.setColorAt(0., QColor(255, 255, 255, 50));
	sheen.setColorAt(0.45, QColor(255, 255, 255, 0));
	p.fillRect(target, sheen);
	p.restore();
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 40), 1.));
	p.drawRoundedRect(target, radius, radius);
}

} // namespace

Skin Current() {
	const auto value = Extera::Int(u"tristegram_player_skin"_q);
	return (value >= 0 && value < kCount) ? Skin(value) : Skin::Vinyl;
}

void Choose(Skin skin) {
	Extera::Config::Instance().setInt(u"tristegram_player_skin"_q, int(skin));
}

QString Name(Skin skin) {
	switch (skin) {
	case Skin::Vinyl: return tr::lng_tgm_skin_vinyl(tr::now);
	case Skin::Cassette: return tr::lng_tgm_skin_cassette(tr::now);
	case Skin::Raven: return tr::lng_tgm_skin_raven(tr::now);
	case Skin::Drops: return tr::lng_tgm_skin_drip(tr::now);
	case Skin::Ripples: return tr::lng_tgm_skin_ripples(tr::now);
	case Skin::Orbits: return tr::lng_tgm_skin_orbits(tr::now);
	case Skin::Cover: return tr::lng_tgm_skin_cover(tr::now);
	}
	return QString();
}

void Paint(QPainter &p, QRectF rect, Skin skin, const Args &args) {
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	switch (skin) {
	case Skin::Cassette: PaintCassette(p, rect, args); break;
	case Skin::Raven: PaintRaven(p, rect, args); break;
	case Skin::Drops: PaintDrip(p, rect, args, args.progress); break;
	case Skin::Ripples: PaintRipples(p, rect, args); break;
	case Skin::Orbits: PaintOrbits(p, rect, args); break;
	case Skin::Cover: PaintCover(p, rect, args); break;
	case Skin::Vinyl: break;
	}
	p.restore();
}

} // namespace Tristegram::MusicSkins
