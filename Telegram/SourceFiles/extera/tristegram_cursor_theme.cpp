/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_cursor_theme.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtCore/QDir>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>
#include <QtGui/QPainterPathStroker>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <cmath>
#include <memory>
#include <optional>

namespace Tristegram {
namespace {

constexpr auto kPi = 3.14159265358979;
constexpr auto kPresets = 14;
constexpr auto kRoleProperty = "tristegramCursorRole";

enum class Shape {
	Arrow,
	Soft,
	Plane,
	Drop,
	Dot,
	Ring,
	Pixel,
	Heart,
	Star,
	Sharp,
	Count,
};

enum class Role {
	None,
	Arrow,
	Pointer,
	Text,
	Window, // a window without its own cursor
};

enum class Fill {
	Solid,
	Gradient,
	Logo,
	Rainbow,
};

struct Look {
	Shape shape = Shape::Soft;
	Fill fill = Fill::Solid;
	QColor color = QColor(255, 255, 255);
	QColor color2;
	QColor outline = QColor(0x1B, 0x1B, 0x1F);
	double outlineWidth = 1.3;
	QColor glow;
	bool shadow = true;
};

[[nodiscard]] QColor Hex(const char *value) {
	return QColor(QString::fromLatin1(value));
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] Look Preset(int index) {
	const auto logo = CurrentLogoStyle();
	auto look = Look();
	switch (index) {
	case 1: // White
		break;
	case 2: // Black
		look.color = Hex("#16161A");
		look.outline = QColor(255, 255, 255);
		break;
	case 3: // Logo
		look.fill = Fill::Logo;
		look.outline = QColor(255, 255, 255);
		look.glow = logo.from;
		break;
	case 4: // Neon
		look.shape = Shape::Arrow;
		look.color = Hex("#0B0F1A");
		look.outline = Hex("#39F3FF");
		look.outlineWidth = 1.6;
		look.glow = Hex("#39F3FF");
		look.shadow = false;
		break;
	case 5: // Navigator
		look.shape = Shape::Plane;
		look.fill = Fill::Logo;
		look.outline = QColor(255, 255, 255);
		break;
	case 6: // Drop
		look.shape = Shape::Drop;
		look.fill = Fill::Gradient;
		look.color = Hex("#FF5E7E");
		look.color2 = Hex("#FF9F43");
		look.outline = QColor(255, 255, 255);
		break;
	case 7: // Dot
		look.shape = Shape::Dot;
		look.color = logo.from;
		look.outline = QColor(255, 255, 255);
		look.glow = logo.from;
		look.shadow = false;
		break;
	case 8: // Ring
		look.shape = Shape::Ring;
		look.fill = Fill::Logo;
		look.outline = WithAlpha(QColor(255, 255, 255), 0.9);
		look.outlineWidth = 1.;
		look.shadow = false;
		break;
	case 9: // Pixel
		look.shape = Shape::Pixel;
		look.outline = QColor(0, 0, 0);
		look.outlineWidth = 1.2;
		look.shadow = false;
		break;
	case 10: // Heart
		look.shape = Shape::Heart;
		look.fill = Fill::Gradient;
		look.color = Hex("#FF4D6D");
		look.color2 = Hex("#FF8FA3");
		look.outline = QColor(255, 255, 255);
		break;
	case 11: // Star
		look.shape = Shape::Star;
		look.fill = Fill::Gradient;
		look.color = Hex("#FFD60A");
		look.color2 = Hex("#FF9F0A");
		look.outline = Hex("#6B3F00");
		look.outlineWidth = 1.1;
		break;
	case 12: // Minimal
		look.shape = Shape::Sharp;
		look.color = QColor(0, 0, 0);
		look.outline = QColor(255, 255, 255);
		look.outlineWidth = 1.2;
		break;
	case 13: // Rainbow
		look.fill = Fill::Rainbow;
		look.outline = QColor(255, 255, 255);
		break;
	case 14: // Glass
		look.color = WithAlpha(QColor(255, 255, 255), 0.42);
		look.outline = WithAlpha(QColor(255, 255, 255), 0.95);
		look.outlineWidth = 1.4;
		break;
	}
	return look;
}

[[nodiscard]] Look CustomLook() {
	auto look = Look();
	look.shape = Shape(std::clamp(
		Extera::Int(u"tristegram_cursor_shape"_q),
		0,
		int(Shape::Count) - 1));
	const auto color = QColor(Extera::String(u"tristegram_cursor_fill"_q));
	const auto outline = QColor(
		Extera::String(u"tristegram_cursor_outline"_q));
	look.fill = Extera::Bool(u"tristegram_cursor_gradient"_q)
		? Fill::Logo
		: Fill::Solid;
	look.color = color.isValid() ? color : CurrentLogoStyle().from;
	look.outline = outline.isValid() ? outline : QColor(255, 255, 255);
	look.glow = Extera::Bool(u"tristegram_cursor_glow"_q)
		? ((look.fill == Fill::Logo) ? CurrentLogoStyle().from : look.color)
		: QColor();
	look.shadow = Extera::Bool(u"tristegram_cursor_shadow"_q);
	return look;
}

[[nodiscard]] QPainterPath Polygon(std::initializer_list<QPointF> points) {
	auto result = QPainterPath();
	auto first = true;
	for (const auto &point : points) {
		if (first) {
			result.moveTo(point);
			first = false;
		} else {
			result.lineTo(point);
		}
	}
	result.closeSubpath();
	return result;
}

// The polygon grown by `radius` with round corners.
[[nodiscard]] QPainterPath Rounded(const QPainterPath &path, double radius) {
	auto stroker = QPainterPathStroker();
	stroker.setWidth(radius * 2.);
	stroker.setJoinStyle(Qt::RoundJoin);
	stroker.setCapStyle(Qt::RoundCap);
	return stroker.createStroke(path).united(path).simplified();
}

// Shapes in a unit square, the hot spot is returned separately.
[[nodiscard]] QPainterPath UnitShape(Shape shape) {
	switch (shape) {
	case Shape::Arrow:
		return Polygon({
			{ 0.02, 0.02 },
			{ 0.02, 0.84 },
			{ 0.24, 0.65 },
			{ 0.38, 0.97 },
			{ 0.53, 0.91 },
			{ 0.39, 0.6 },
			{ 0.66, 0.6 },
		});
	case Shape::Soft:
		return Rounded(Polygon({
			{ 0.08, 0.08 },
			{ 0.08, 0.76 },
			{ 0.27, 0.6 },
			{ 0.6, 0.58 },
		}), 0.06);
	case Shape::Plane:
		return Rounded(Polygon({
			{ 0.05, 0.05 },
			{ 0.9, 0.4 },
			{ 0.52, 0.52 },
			{ 0.4, 0.9 },
		}), 0.03);
	case Shape::Drop: {
		auto result = QPainterPath();
		result.addEllipse(QPointF(0.54, 0.54), 0.4, 0.4);
		return result.united(Polygon({
			{ 0.04, 0.04 },
			{ 0.56, 0.14 },
			{ 0.14, 0.56 },
		})).simplified();
	}
	case Shape::Dot: {
		auto result = QPainterPath();
		result.addEllipse(QPointF(0.5, 0.5), 0.2, 0.2);
		return result;
	}
	case Shape::Ring: {
		auto outer = QPainterPath();
		outer.addEllipse(QPointF(0.5, 0.5), 0.42, 0.42);
		auto inner = QPainterPath();
		inner.addEllipse(QPointF(0.5, 0.5), 0.32, 0.32);
		auto dot = QPainterPath();
		dot.addEllipse(QPointF(0.5, 0.5), 0.07, 0.07);
		return outer.subtracted(inner).united(dot);
	}
	case Shape::Pixel: {
		constexpr auto cell = 1. / 11.;
		auto result = QPainterPath();
		const auto add = [&](int column, int row, int width) {
			auto rect = QPainterPath();
			rect.addRect(QRectF(
				column * cell,
				row * cell,
				width * cell,
				cell));
			result = result.united(rect);
		};
		for (auto row = 0; row != 8; ++row) {
			add(0, row, row + 1);
		}
		add(0, 8, 5);
		add(0, 9, 2);
		add(3, 9, 2);
		add(4, 10, 2);
		return result.simplified();
	}
	case Shape::Heart: {
		auto result = QPainterPath();
		result.moveTo(0.5, 0.9);
		result.cubicTo(0.02, 0.6, 0.06, 0.12, 0.5, 0.3);
		result.cubicTo(0.94, 0.12, 0.98, 0.6, 0.5, 0.9);
		result.closeSubpath();
		return result;
	}
	case Shape::Star: {
		auto result = QPainterPath();
		for (auto i = 0; i != 10; ++i) {
			const auto radius = (i % 2) ? 0.2 : 0.46;
			const auto angle = -kPi / 2 + i * kPi / 5;
			const auto point = QPointF(
				0.5 + std::cos(angle) * radius,
				0.53 + std::sin(angle) * radius);
			if (!i) {
				result.moveTo(point);
			} else {
				result.lineTo(point);
			}
		}
		result.closeSubpath();
		return Rounded(result, 0.025);
	}
	case Shape::Sharp:
		return Polygon({
			{ 0.04, 0.04 },
			{ 0.3, 0.86 },
			{ 0.42, 0.48 },
			{ 0.82, 0.32 },
		});
	case Shape::Count:
		break;
	}
	return QPainterPath();
}

[[nodiscard]] QPointF UnitHotSpot(Shape shape) {
	switch (shape) {
	case Shape::Arrow: return { 0.02, 0.02 };
	case Shape::Soft: return { 0.03, 0.03 };
	case Shape::Plane: return { 0.04, 0.04 };
	case Shape::Drop: return { 0.05, 0.05 };
	case Shape::Pixel: return { 0.02, 0.02 };
	case Shape::Sharp: return { 0.05, 0.05 };
	case Shape::Dot:
	case Shape::Ring:
	case Shape::Heart:
	case Shape::Star:
	case Shape::Count:
		break;
	}
	return { 0.5, 0.5 };
}

[[nodiscard]] bool CenteredShape(Shape shape) {
	return (UnitHotSpot(shape) == QPointF(0.5, 0.5));
}

[[nodiscard]] QBrush FillBrush(const Look &look, QRectF box) {
	switch (look.fill) {
	case Fill::Solid:
		return look.color;
	case Fill::Gradient:
	case Fill::Logo: {
		const auto logo = CurrentLogoStyle();
		auto gradient = QLinearGradient(box.topLeft(), box.bottomRight());
		gradient.setColorAt(0., (look.fill == Fill::Logo)
			? logo.from
			: look.color);
		gradient.setColorAt(1., (look.fill == Fill::Logo)
			? logo.to
			: look.color2.isValid()
			? look.color2
			: look.color);
		return gradient;
	}
	case Fill::Rainbow: {
		auto gradient = QConicalGradient(box.center(), 120.);
		gradient.setColorAt(0., Hex("#FF453A"));
		gradient.setColorAt(0.17, Hex("#FF9F0A"));
		gradient.setColorAt(0.33, Hex("#FFD60A"));
		gradient.setColorAt(0.5, Hex("#34C759"));
		gradient.setColorAt(0.67, Hex("#0A84FF"));
		gradient.setColorAt(0.83, Hex("#BF5AF2"));
		gradient.setColorAt(1., Hex("#FF453A"));
		return gradient;
	}
	}
	return look.color;
}

[[nodiscard]] QColor AccentOf(const Look &look) {
	if (look.glow.isValid()) {
		return look.glow;
	} else if (look.fill == Fill::Logo || look.fill == Fill::Rainbow) {
		return CurrentLogoStyle().from;
	} else if (look.fill == Fill::Gradient && look.color2.isValid()) {
		return look.color2;
	}
	const auto light = (look.color.lightnessF() > 0.6);
	return (light && look.outline.lightnessF() < 0.4)
		? CurrentLogoStyle().from
		: look.color;
}

void PaintPath(
		QPainter &p,
		const QPainterPath &path,
		const Look &look,
		QRectF box,
		double unit) {
	const auto join = (look.shape == Shape::Pixel)
		? Qt::MiterJoin
		: Qt::RoundJoin;
	const auto outline = look.outlineWidth * unit;
	if (look.shadow) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 60));
		p.drawPath(path.translated(0, unit * 1.3));
		p.setBrush(QColor(0, 0, 0, 30));
		p.drawPath(path.translated(0, unit * 2.2));
	}
	if (look.glow.isValid()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			WithAlpha(look.glow, 0.18),
			outline + unit * 5.,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.drawPath(path);
		p.setPen(QPen(
			WithAlpha(look.glow, 0.32),
			outline + unit * 2.6,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.drawPath(path);
	}
	p.setBrush(FillBrush(look, box));
	if (look.outline.isValid() && look.outline.alpha() > 0 && outline > 0.) {
		p.setPen(QPen(look.outline, outline, Qt::SolidLine, Qt::RoundCap, join));
	} else {
		p.setPen(Qt::NoPen);
	}
	p.drawPath(path);
}

void PaintBadge(QPainter &p, QPointF center, double radius, const Look &look, double unit) {
	p.setPen(QPen(
		look.outline.isValid() ? look.outline : QColor(255, 255, 255),
		std::max(1., look.outlineWidth * unit)));
	p.setBrush(AccentOf(look));
	p.drawEllipse(center, radius, radius);
	p.setPen(Qt::NoPen);
	p.setBrush(WithAlpha(QColor(255, 255, 255), 0.85));
	p.drawEllipse(center, radius * 0.36, radius * 0.36);
}

void PaintBeam(QPainter &p, QRectF box, const Look &look, double unit) {
	const auto cx = box.center().x();
	const auto top = box.y() + box.height() * 0.08;
	const auto bottom = box.y() + box.height() * 0.92;
	const auto serif = box.width() * 0.16;
	auto path = QPainterPath();
	path.moveTo(cx - serif, top);
	path.quadTo(cx, top, cx, top + serif * 0.9);
	path.quadTo(cx, top, cx + serif, top);
	path.moveTo(cx, top + serif * 0.9);
	path.lineTo(cx, bottom - serif * 0.9);
	path.moveTo(cx - serif, bottom);
	path.quadTo(cx, bottom, cx, bottom - serif * 0.9);
	path.quadTo(cx, bottom, cx + serif, bottom);
	const auto core = (look.fill == Fill::Solid && look.color.alpha() < 200)
		? QBrush(QColor(255, 255, 255))
		: FillBrush(look, box);
	const auto width = std::max(1.6 * unit, box.width() * 0.07);
	p.setBrush(Qt::NoBrush);
	const auto edge = look.outline.isValid()
		? look.outline
		: QColor(0, 0, 0);
	if (look.glow.isValid()) {
		p.setPen(QPen(WithAlpha(look.glow, 0.3), width + unit * 4., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
	}
	p.setPen(QPen(edge, width + look.outlineWidth * unit * 2., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(path);
	p.setPen(QPen(core, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(path);
}

struct Rendered {
	QImage image;
	QPoint hotSpot;
};

[[nodiscard]] Rendered RenderLook(
		const Look &look,
		Role role,
		int size,
		double ratio) {
	auto result = Rendered();
	result.image = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.image.setDevicePixelRatio(ratio);
	result.image.fill(Qt::transparent);
	const auto unit = size / 26.;
	const auto pad = unit * (look.glow.isValid() ? 3.5 : 1.5);
	const auto box = QRectF(pad, pad, size - 2 * pad, size - 2 * pad);
	if (role == Role::Text) {
		{
			auto p = QPainter(&result.image);
			auto hq = PainterHighQualityEnabler(p);
			PaintBeam(p, box, look, unit);
		}
		result.hotSpot = box.center().toPoint();
		return result;
	}
	auto p = std::make_unique<QPainter>(&result.image);
	auto hq = std::make_unique<PainterHighQualityEnabler>(*p);
	const auto centered = CenteredShape(look.shape);
	auto shapeBox = box;
	if (role == Role::Pointer && !centered) {
		// Leave room for the badge in the corner.
		shapeBox = QRectF(box.x(), box.y(), box.width() * 0.86, box.height() * 0.86);
	}
	auto transform = QTransform();
	transform.translate(shapeBox.x(), shapeBox.y());
	transform.scale(shapeBox.width(), shapeBox.height());
	auto path = transform.map(UnitShape(look.shape));
	if (role == Role::Pointer && look.shape == Shape::Dot) {
		// The dot grows into a soft ring over buttons.
		auto ring = QPainterPath();
		ring.addEllipse(box.center(), box.width() * 0.4, box.width() * 0.4);
		auto inner = QPainterPath();
		inner.addEllipse(box.center(), box.width() * 0.33, box.width() * 0.33);
		auto dot = QPainterPath();
		dot.addEllipse(box.center(), box.width() * 0.1, box.width() * 0.1);
		path = ring.subtracted(inner).united(dot);
	} else if (role == Role::Pointer && look.shape == Shape::Ring) {
		auto ring = QPainterPath();
		ring.addEllipse(box.center(), box.width() * 0.46, box.width() * 0.46);
		auto inner = QPainterPath();
		inner.addEllipse(box.center(), box.width() * 0.38, box.width() * 0.38);
		auto dot = QPainterPath();
		dot.addEllipse(box.center(), box.width() * 0.14, box.width() * 0.14);
		path = ring.subtracted(inner).united(dot);
	} else if (role == Role::Pointer && centered) {
		auto grow = QTransform();
		grow.translate(box.center().x(), box.center().y());
		grow.scale(1.12, 1.12);
		grow.translate(-box.center().x(), -box.center().y());
		path = grow.map(path);
	}
	PaintPath(*p, path, look, box, unit);
	if (role == Role::Pointer && !centered) {
		const auto radius = box.width() * 0.17;
		PaintBadge(
			*p,
			QPointF(box.right() - radius, box.bottom() - radius),
			radius,
			look,
			unit);
	}
	hq = nullptr;
	p = nullptr;
	const auto hot = UnitHotSpot(look.shape);
	result.hotSpot = QPointF(
		shapeBox.x() + hot.x() * shapeBox.width(),
		shapeBox.y() + hot.y() * shapeBox.height()).toPoint();
	return result;
}

[[nodiscard]] QString PictureFolder() {
	return cWorkingDir() + u"tdata/tristegram_cursor/"_q;
}

[[nodiscard]] QString PicturePath() {
	return PictureFolder() + u"cursor.png"_q;
}

QImage &PictureCache() {
	static auto result = QImage();
	return result;
}

bool &PictureLoaded() {
	static auto result = false;
	return result;
}

[[nodiscard]] Rendered RenderPicture(Role role, int size, double ratio) {
	const auto &picture = CursorPicture();
	if (picture.isNull()) {
		return RenderLook(Preset(1), role, size, ratio);
	}
	if (role == Role::Text) {
		auto look = Preset(1);
		return RenderLook(look, role, size, ratio);
	}
	const auto scale = (role == Role::Pointer) ? 1.08 : 1.;
	const auto target = QSizeF(picture.size()).scaled(
		QSizeF(size, size) * scale,
		Qt::KeepAspectRatio);
	auto result = Rendered();
	const auto full = QSize(
		std::max(1, int(std::ceil(target.width()))),
		std::max(1, int(std::ceil(target.height()))));
	result.image = QImage(full * ratio, QImage::Format_ARGB32_Premultiplied);
	result.image.setDevicePixelRatio(ratio);
	result.image.fill(Qt::transparent);
	{
		auto p = QPainter(&result.image);
		auto hq = PainterHighQualityEnabler(p);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(QRectF(QPointF(), target), picture);
	}
	const auto hot = CursorPictureHotSpot();
	result.hotSpot = QPoint(
		std::clamp(int(hot.x() * target.width()), 0, full.width() - 1),
		std::clamp(int(hot.y() * target.height()), 0, full.height() - 1));
	return result;
}

struct Cursors {
	QCursor arrow;
	QCursor pointer;
	QCursor text;
};

std::optional<Cursors> &Current() {
	static auto result = std::optional<Cursors>();
	return result;
}

bool Applying = false;

[[nodiscard]] int Theme() {
	return std::clamp(
		Extera::Int(u"tristegram_cursor_theme"_q),
		0,
		CursorThemesCount() - 1);
}

[[nodiscard]] double MaxRatio() {
	auto result = 1.;
	for (const auto screen : QGuiApplication::screens()) {
		result = std::max(result, screen->devicePixelRatio());
	}
	return std::min(result, 4.);
}

[[nodiscard]] QCursor Make(const Rendered &rendered) {
	return QCursor(
		QPixmap::fromImage(rendered.image),
		rendered.hotSpot.x(),
		rendered.hotSpot.y());
}

void Rebuild() {
	const auto theme = Theme();
	if (!theme) {
		Current() = std::nullopt;
		return;
	}
	const auto size = std::clamp(
		style::ConvertScale(Extera::Int(u"tristegram_cursor_size"_q)),
		12,
		160);
	const auto ratio = MaxRatio();
	const auto render = [&](Role role) {
		if (theme == CursorFileIndex()) {
			return RenderPicture(role, size, ratio);
		}
		const auto look = (theme == CursorCustomIndex())
			? CustomLook()
			: Preset(theme);
		return RenderLook(look, role, size, ratio);
	};
	Current() = Cursors{
		.arrow = Make(render(Role::Arrow)),
		.pointer = Make(render(Role::Pointer)),
		.text = Make(render(Role::Text)),
	};
}

[[nodiscard]] const QCursor *CursorFor(Role role) {
	const auto &current = Current();
	if (!current) {
		return nullptr;
	}
	switch (role) {
	case Role::Arrow:
	case Role::Window: return &current->arrow;
	case Role::Pointer: return &current->pointer;
	case Role::Text: return &current->text;
	case Role::None: break;
	}
	return nullptr;
}

[[nodiscard]] Role StoredRole(QWidget *widget) {
	return Role(widget->property(kRoleProperty).toInt());
}

void SetRole(QWidget *widget, Role role) {
	widget->setProperty(
		kRoleProperty,
		(role == Role::None) ? QVariant() : QVariant(int(role)));
}

[[nodiscard]] bool Ours(QWidget *widget) {
	return widget->property("tristegramEffects").toBool();
}

void ApplyTo(QWidget *widget) {
	if (Applying || !Current() || Ours(widget)) {
		return;
	}
	auto role = Role::None;
	if (widget->testAttribute(Qt::WA_SetCursor)) {
		switch (widget->cursor().shape()) {
		case Qt::ArrowCursor: role = Role::Arrow; break;
		case Qt::PointingHandCursor: role = Role::Pointer; break;
		case Qt::IBeamCursor: role = Role::Text; break;
		case Qt::BitmapCursor:
			// Our own cursor or a picture set by the widget itself.
			return;
		default:
			SetRole(widget, Role::None);
			return;
		}
	} else if (widget->isWindow()) {
		role = Role::Window;
	} else {
		SetRole(widget, Role::None);
		return;
	}
	if (const auto cursor = CursorFor(role)) {
		Applying = true;
		SetRole(widget, role);
		widget->setCursor(*cursor);
		Applying = false;
	}
}

void Restore(QWidget *widget) {
	const auto role = StoredRole(widget);
	if (role == Role::None) {
		return;
	}
	Applying = true;
	SetRole(widget, Role::None);
	switch (role) {
	case Role::Arrow: widget->setCursor(Qt::ArrowCursor); break;
	case Role::Pointer: widget->setCursor(Qt::PointingHandCursor); break;
	case Role::Text: widget->setCursor(Qt::IBeamCursor); break;
	case Role::Window: widget->unsetCursor(); break;
	case Role::None: break;
	}
	Applying = false;
}

void Refresh() {
	const auto widgets = QApplication::allWidgets();
	for (const auto widget : widgets) {
		Restore(widget);
	}
	Rebuild();
	if (!Current()) {
		return;
	}
	for (const auto widget : widgets) {
		ApplyTo(widget);
	}
}

class Filter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *e) override {
		const auto type = e->type();
		if ((type == QEvent::CursorChange || type == QEvent::Show)
			&& !Applying
			&& Current()
			&& watched->isWidgetType()) {
			const auto widget = static_cast<QWidget*>(watched);
			if (type == QEvent::CursorChange || widget->isWindow()) {
				ApplyTo(widget);
			}
		}
		return false;
	}

};

[[nodiscard]] Shape ShapeOfTheme(int index) {
	return (index == CursorCustomIndex())
		? CustomLook().shape
		: Preset(index).shape;
}

} // namespace

void InstallCustomCursor() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	qApp->installEventFilter(new Filter(qApp));
	static auto lifetime = rpl::lifetime();
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_cursor_"_q)
			|| key.startsWith(u"tristegram_logo"_q);
	}) | rpl::on_next([](const QString &key) {
		if (key.startsWith(u"tristegram_logo"_q)
			&& !Current()) {
			return;
		}
		// Coalesce a burst of changes, like a color drag.
		static auto scheduled = false;
		if (!scheduled) {
			scheduled = true;
			crl::on_main([] {
				scheduled = false;
				Refresh();
			});
		}
	}, lifetime);
	if (Theme()) {
		crl::on_main([] { Refresh(); });
	}
}

int CursorThemesCount() {
	return kPresets + 3;
}

int CursorCustomIndex() {
	return kPresets + 1;
}

int CursorFileIndex() {
	return kPresets + 2;
}

QString CursorThemeName(int index) {
	switch (index) {
	case 0: return tr::lng_tgcu_system(tr::now);
	case 1: return tr::lng_tgcu_white(tr::now);
	case 2: return tr::lng_tgcu_black(tr::now);
	case 3: return tr::lng_tgcu_logo(tr::now);
	case 4: return tr::lng_tgcu_neon(tr::now);
	case 5: return tr::lng_tgcu_plane(tr::now);
	case 6: return tr::lng_tgcu_drop(tr::now);
	case 7: return tr::lng_tgcu_dot(tr::now);
	case 8: return tr::lng_tgcu_ring(tr::now);
	case 9: return tr::lng_tgcu_pixel(tr::now);
	case 10: return tr::lng_tgcu_heart(tr::now);
	case 11: return tr::lng_tgcu_star(tr::now);
	case 12: return tr::lng_tgcu_minimal(tr::now);
	case 13: return tr::lng_tgcu_rainbow(tr::now);
	case 14: return tr::lng_tgcu_glass(tr::now);
	}
	if (index == CursorCustomIndex()) {
		return tr::lng_tgcu_custom(tr::now);
	} else if (index == CursorFileIndex()) {
		return tr::lng_tgcu_file(tr::now);
	}
	return QString();
}

void PaintCursorThemePreview(
		QPainter &p,
		QRectF rect,
		int index,
		int roleIndex) {
	const auto size = int(std::min(rect.width(), rect.height()));
	if (size <= 0) {
		return;
	}
	const auto role = (roleIndex == 1)
		? Role::Pointer
		: (roleIndex == 2)
		? Role::Text
		: Role::Arrow;
	const auto ratio = style::DevicePixelRatio();
	auto rendered = Rendered();
	if (!index) {
		// The system arrow, drawn plain.
		auto look = Preset(1);
		look.shape = Shape::Arrow;
		look.shadow = false;
		rendered = RenderLook(look, role, size, ratio);
	} else if (index == CursorFileIndex()) {
		if (CursorPicture().isNull()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto inner = rect.marginsRemoved(
				QMarginsF(size * 0.18, size * 0.18, size * 0.18, size * 0.18));
			auto pen = QPen(
				WithAlpha(st::windowSubTextFg->c, 0.8),
				std::max(1.5, size * 0.04));
			pen.setStyle(Qt::DashLine);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(inner, size * 0.12, size * 0.12);
			p.setPen(QPen(
				st::windowSubTextFg->c,
				std::max(1.5, size * 0.05),
				Qt::SolidLine,
				Qt::RoundCap));
			const auto c = inner.center();
			const auto arm = inner.width() * 0.2;
			p.drawLine(QPointF(c.x() - arm, c.y()), QPointF(c.x() + arm, c.y()));
			p.drawLine(QPointF(c.x(), c.y() - arm), QPointF(c.x(), c.y() + arm));
			return;
		}
		rendered = RenderPicture(role, size, ratio);
	} else {
		const auto look = (index == CursorCustomIndex())
			? CustomLook()
			: Preset(index);
		rendered = RenderLook(look, role, size, ratio);
	}
	const auto image = rendered.image;
	const auto logical = QSizeF(image.size()) / image.devicePixelRatio();
	const auto shift = (CenteredShape(ShapeOfTheme(index))
			|| !index
			|| index == CursorFileIndex()
			|| role == Role::Text)
		? QPointF()
		: QPointF(size * 0.08, size * 0.06);
	p.drawImage(
		QRectF(
			rect.x() + (rect.width() - logical.width()) / 2. + shift.x(),
			rect.y() + (rect.height() - logical.height()) / 2. + shift.y(),
			logical.width(),
			logical.height()),
		image);
}

int CursorShapesCount() {
	return int(Shape::Count);
}

QString CursorShapeName(int shape) {
	switch (Shape(shape)) {
	case Shape::Arrow: return tr::lng_tgcu_shape_arrow(tr::now);
	case Shape::Soft: return tr::lng_tgcu_shape_soft(tr::now);
	case Shape::Plane: return tr::lng_tgcu_plane(tr::now);
	case Shape::Drop: return tr::lng_tgcu_drop(tr::now);
	case Shape::Dot: return tr::lng_tgcu_dot(tr::now);
	case Shape::Ring: return tr::lng_tgcu_ring(tr::now);
	case Shape::Pixel: return tr::lng_tgcu_pixel(tr::now);
	case Shape::Heart: return tr::lng_tgcu_heart(tr::now);
	case Shape::Star: return tr::lng_tgcu_star(tr::now);
	case Shape::Sharp: return tr::lng_tgcu_minimal(tr::now);
	case Shape::Count: break;
	}
	return QString();
}

void PaintCursorShapePreview(QPainter &p, QRectF rect, int shape) {
	const auto size = int(std::min(rect.width(), rect.height()));
	if (size <= 0) {
		return;
	}
	auto look = CustomLook();
	look.shape = Shape(std::clamp(shape, 0, int(Shape::Count) - 1));
	const auto rendered = RenderLook(
		look,
		Role::Arrow,
		size,
		style::DevicePixelRatio());
	const auto logical = QSizeF(rendered.image.size())
		/ rendered.image.devicePixelRatio();
	p.drawImage(
		QRectF(
			rect.x() + (rect.width() - logical.width()) / 2.,
			rect.y() + (rect.height() - logical.height()) / 2.,
			logical.width(),
			logical.height()),
		rendered.image);
}

bool SetCursorPicture(const QImage &image) {
	if (image.isNull()) {
		return false;
	}
	auto prepared = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (std::max(prepared.width(), prepared.height()) > 256) {
		prepared = prepared.scaled(
			256,
			256,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	QDir().mkpath(PictureFolder());
	if (!prepared.save(PicturePath(), "PNG")) {
		return false;
	}
	PictureCache() = prepared;
	PictureLoaded() = true;
	auto &config = Extera::Config::Instance();
	config.setString(
		u"tristegram_cursor_file"_q,
		QString::number(crl::now()));
	config.setInt(u"tristegram_cursor_theme"_q, CursorFileIndex());
	return true;
}

void SetCursorPictureHotSpot(QPointF part) {
	Extera::Config::Instance().setString(
		u"tristegram_cursor_hot"_q,
		QString::number(int(std::round(std::clamp(part.x(), 0., 1.) * 1000)))
			+ ','
			+ QString::number(int(std::round(std::clamp(part.y(), 0., 1.) * 1000))));
}

QImage CursorPicture() {
	if (!PictureLoaded()) {
		PictureLoaded() = true;
		auto image = QImage(PicturePath());
		if (!image.isNull()) {
			PictureCache() = image.convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
		}
	}
	return PictureCache();
}

QPointF CursorPictureHotSpot() {
	const auto parts = Extera::String(u"tristegram_cursor_hot"_q).split(',');
	if (parts.size() != 2) {
		return QPointF();
	}
	return QPointF(
		std::clamp(parts[0].toInt() / 1000., 0., 1.),
		std::clamp(parts[1].toInt() / 1000., 0., 1.));
}

} // namespace Tristegram
