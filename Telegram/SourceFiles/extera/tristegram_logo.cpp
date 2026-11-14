/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_logo.h"

#include "extera/extera_config.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QLinearGradient>
#include <QtGui/QRadialGradient>

#include <algorithm>
#include <array>
#include <cmath>

namespace Tristegram {
namespace {

// All geometry is in a 256x256 box, the same as the logo artwork.
constexpr auto kSize = 256.;
constexpr auto kCenter = 128.;
constexpr auto kPi = 3.14159265358979323846;
constexpr auto kArrowEnd = 1.12;

struct Orbit {
	double cx = 0.;
	double cy = 0.;
	double rx = 0.;
	double ry = 0.;
	double rotation = 0.; // degrees
};

constexpr auto kOrbitA = Orbit{ 128., 134., 104., 40., -25. };
constexpr auto kOrbitB = Orbit{ 126., 130., 100., 38., 58. };

struct Preset {
	const char *from;
	const char *to;
};

constexpr auto kPresets = std::array<Preset, 12>{ {
	{ "#f07a0c", "#f07a0c" }, // Classic orange
	{ "#ff8a1f", "#e8318a" }, // Sunset
	{ "#ffb21f", "#e5383b" }, // Fire
	{ "#2fd0f0", "#3a5cf0" }, // Ocean
	{ "#22c58b", "#5b6cf6" }, // Aurora
	{ "#8b5cf6", "#ec4899" }, // Neon
	{ "#c6f01f", "#22a45d" }, // Lime
	{ "#2f8cf0", "#2f8cf0" }, // Blue
	{ "#8b5cf6", "#8b5cf6" }, // Purple
	{ "#22a45d", "#22a45d" }, // Green
	{ "#e8508a", "#e8508a" }, // Pink
	{ "#e5383b", "#e5383b" }, // Red
} };

struct OrbitPoint {
	QPointF position;
	double angle = 0.; // degrees, direction of growing parameter
};

[[nodiscard]] OrbitPoint PointOn(const Orbit &orbit, double t) {
	const auto r = orbit.rotation * kPi / 180.;
	const auto x = orbit.rx * std::cos(t);
	const auto y = orbit.ry * std::sin(t);
	const auto dx = -orbit.rx * std::sin(t);
	const auto dy = orbit.ry * std::cos(t);
	return {
		QPointF(
			orbit.cx + x * std::cos(r) - y * std::sin(r),
			orbit.cy + x * std::sin(r) + y * std::cos(r)),
		std::atan2(
			dx * std::sin(r) + dy * std::cos(r),
			dx * std::cos(r) - dy * std::sin(r)) * 180. / kPi,
	};
}

[[nodiscard]] QPainterPath OrbitPath(const Orbit &orbit, double &length) {
	constexpr auto kSteps = 96;
	auto result = QPainterPath();
	length = 0.;
	auto previous = QPointF();
	for (auto i = 0; i <= kSteps; ++i) {
		const auto point = PointOn(orbit, 2. * kPi * i / kSteps).position;
		if (!i) {
			result.moveTo(point);
		} else {
			result.lineTo(point);
			const auto d = point - previous;
			length += std::sqrt(d.x() * d.x() + d.y() * d.y());
		}
		previous = point;
	}
	return result;
}

[[nodiscard]] QPainterPath LetterPath() {
	auto p = QPainterPath();
	p.moveTo(80, 84);
	p.quadTo(80, 76, 88, 76);
	p.lineTo(168, 76);
	p.quadTo(176, 76, 176, 84);
	p.lineTo(176, 98);
	p.quadTo(176, 104, 170, 104);
	p.lineTo(143, 104);
	p.lineTo(143, 184);
	p.quadTo(143, 192, 135, 192);
	p.lineTo(121, 192);
	p.quadTo(113, 192, 113, 184);
	p.lineTo(113, 104);
	p.lineTo(86, 104);
	p.quadTo(80, 104, 80, 98);
	p.closeSubpath();
	return p;
}

[[nodiscard]] QPainterPath ArrowPath() {
	auto p = QPainterPath();
	p.moveTo(18, 0);
	p.lineTo(-12, -13);
	p.lineTo(-5, 0);
	p.lineTo(-12, 13);
	p.closeSubpath();
	return p;
}

[[nodiscard]] double Clamp01(double value) {
	return std::clamp(value, 0., 1.);
}

[[nodiscard]] double Segment(double t, double from, double till) {
	return Clamp01((t - from) / (till - from));
}

[[nodiscard]] double EaseInOut(double t) {
	return (t < 0.5)
		? (4. * t * t * t)
		: (1. - std::pow(-2. * t + 2., 3) / 2.);
}

[[nodiscard]] double EaseBack(double t) {
	constexpr auto c = 1.7;
	return 1. + (c + 1.) * std::pow(t - 1., 3) + c * std::pow(t - 1., 2);
}

// Draws `path` with an outline in the logo gradient below a white fill,
// so the shape is separated from the white orbits behind it.
void PaintOutlined(
		QPainter &p,
		const QPainterPath &path,
		const QBrush &outline,
		double outlineWidth) {
	p.strokePath(
		path,
		QPen(outline, outlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.fillPath(path, Qt::white);
}

} // namespace

int LogoPresetsCount() {
	return int(kPresets.size());
}

LogoStyle LogoPreset(int index) {
	const auto &preset = kPresets[std::clamp(index, 0, LogoPresetsCount() - 1)];
	return {
		.from = QColor(QString::fromLatin1(preset.from)),
		.to = QColor(QString::fromLatin1(preset.to)),
		.glow = true,
	};
}

LogoStyle CurrentLogoStyle() {
	auto result = LogoPreset(Extera::Int(u"tristegram_logo"_q));
	result.glow = Extera::Bool(u"tristegram_logo_glow"_q);
	return result;
}

bool LogoAnimationEnabled() {
	return Extera::Bool(u"tristegram_logo_anim"_q)
		&& !Extera::Bool(u"tristegram_eco"_q);
}

void PaintLogo(
		QPainter &p,
		const QRectF &rect,
		const LogoStyle &style,
		double progress) {
	const auto t = Clamp01(progress);
	const auto animating = (t > 0.) && (t < 1.);

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.translate(rect.topLeft());
	p.scale(rect.width() / kSize, rect.height() / kSize);

	// Short squeeze when tapped.
	if (animating) {
		const auto squeeze = (t < 0.15)
			? (1. - 0.08 * (1. - std::pow(1. - t / 0.15, 3)))
			: (0.92 + 0.08 * EaseBack(Segment(t, 0.15, 0.6)));
		p.translate(kCenter, kCenter);
		p.scale(squeeze, squeeze);
		p.translate(-kCenter, -kCenter);
	}

	auto gradient = QLinearGradient(
		QPointF(kCenter + 90.5, kCenter - 90.5),
		QPointF(kCenter - 90.5, kCenter + 90.5));
	gradient.setColorAt(0., style.from);
	gradient.setColorAt(1., style.to);
	const auto brush = QBrush(gradient);

	if (style.glow) {
		auto glow = QRadialGradient(QPointF(kCenter, kCenter), kCenter * 1.32);
		auto inner = style.from;
		inner.setAlphaF(0.55);
		auto outer = style.to;
		outer.setAlphaF(0.);
		glow.setColorAt(0.7, inner);
		glow.setColorAt(1., outer);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(QPointF(kCenter, kCenter), kCenter * 1.32, kCenter * 1.32);
	}

	p.setPen(Qt::NoPen);
	p.setBrush(brush);
	p.drawEllipse(QPointF(kCenter, kCenter), 124., 124.);

	// Orbits: white strokes with a gap, like the artwork.
	p.save();
	if (animating) {
		const auto rotation = EaseInOut(Segment(t, 0.1, 0.85)) * 360.;
		p.translate(kCenter, kCenter);
		p.rotate(rotation);
		p.translate(-kCenter, -kCenter);
	}
	for (const auto &orbit : { kOrbitA, kOrbitB }) {
		auto length = 0.;
		const auto path = OrbitPath(orbit, length);
		constexpr auto kWidth = 10.;
		auto pen = QPen(Qt::white, kWidth, Qt::CustomDashLine, Qt::RoundCap);
		pen.setDashPattern({
			0.78 * length / kWidth,
			0.22 * length / kWidth,
		});
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}
	p.restore();

	PaintOutlined(p, LetterPath(), brush, 9.);

	const auto travel = animating
		? EaseInOut(Segment(t, 0.05, 0.9)) * 2. * kPi
		: 0.;
	const auto arrow = PointOn(kOrbitA, kArrowEnd + travel);
	p.save();
	p.translate(arrow.position);
	p.rotate(arrow.angle + 180.);
	PaintOutlined(p, ArrowPath(), brush, 5.);
	p.restore();

	p.restore();
}

} // namespace Tristegram
