/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_cursor.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>
#include <QtGui/QCursor>
#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>

#include <cmath>
#include <deque>

namespace Tristegram {
namespace {

constexpr auto kTrailLife = crl::time(380);

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

// A see-through window that never takes the mouse. It covers only the
// part of the app window where the trail is, so every frame is cheap.
// All the positions are global.
class CursorOverlay final : public Ui::RpWidget {
public:
	explicit CursorOverlay(QWidget *window);

	void moved(QPoint global);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Point {
		QPointF position;
		crl::time when = 0;
	};

	void tick();
	void fit(QRect needed);
	[[nodiscard]] QRect dirtyRect() const;
	[[nodiscard]] QRect windowRect() const;

	std::deque<Point> _trail;
	QPointF _target;
	QPointF _follow;
	base::Timer _timer;
	crl::time _lastMove = 0;
	QRect _painted;

};

CursorOverlay::CursorOverlay(QWidget *window)
: RpWidget(window)
, _timer([=] { tick(); }) {
	setWindowFlags(Qt::Tool
		| Qt::FramelessWindowHint
		| Qt::NoDropShadowWindowHint
		| Qt::WindowTransparentForInput
		| Qt::WindowDoesNotAcceptFocus);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setProperty("tristegramEffects", true);
	hide();
}

QRect CursorOverlay::windowRect() const {
	const auto window = parentWidget();
	return window
		? QRect(window->mapToGlobal(QPoint()), window->size())
		: QRect();
}

void CursorOverlay::fit(QRect needed) {
	needed = needed.intersected(windowRect());
	if (needed.isEmpty()) {
		return;
	}
	const auto current = geometry();
	const auto area = [](QRect rect) {
		return qint64(rect.width()) * rect.height();
	};
	const auto big = Px(160);
	const auto tooSmall = !current.contains(needed);
	const auto tooBig = (current.width() > big || current.height() > big)
		&& area(current) > 6 * area(needed);
	if (!tooSmall && !tooBig) {
		return;
	}
	// Some room around, so a moving mouse does not move the window
	// on every frame.
	const auto margin = Px(90);
	const auto geometry = needed.marginsAdded(
		{ margin, margin, margin, margin }
	).intersected(windowRect());
	setGeometry(geometry);
	_painted = QRect();
	update();
}

void CursorOverlay::moved(QPoint global) {
	const auto position = QPointF(global);
	const auto now = crl::now();
	if (!_lastMove || now - _lastMove > 500) {
		_follow = position;
	}
	_target = position;
	_lastMove = now;
	_trail.push_back({ position, now });
	while (_trail.size() > 40) {
		_trail.pop_front();
	}
	if (isHidden()) {
		_painted = QRect();
		fit(dirtyRect());
		show();
		raise();
	}
	if (!_timer.isActive()) {
		_timer.callEach(16);
	}
}

void CursorOverlay::tick() {
	const auto now = crl::now();
	while (!_trail.empty() && now - _trail.front().when > kTrailLife) {
		_trail.pop_front();
	}
	// The dot and the ring catch up with the mouse smoothly.
	_follow += (_target - _follow) * 0.28;
	const auto settled = (std::abs(_follow.x() - _target.x()) < 0.3)
		&& (std::abs(_follow.y() - _target.y()) < 0.3);
	const auto rect = dirtyRect();
	fit(rect);
	const auto origin = geometry().topLeft();
	update(rect.united(_painted).translated(-origin));
	_painted = rect;
	const auto mode = Extera::Int(u"tristegram_cursor"_q);
	const auto idle = (now - _lastMove > 1500);
	if ((mode == 1 && _trail.empty()) || ((mode != 1) && settled && idle)) {
		_timer.cancel();
		if (mode == 1 || idle) {
			hide();
		}
	}
}

QRect CursorOverlay::dirtyRect() const {
	const auto pad = Px(30);
	auto result = QRectF(_follow, QSizeF(1, 1)).toAlignedRect();
	for (const auto &point : _trail) {
		result = result.united(QRectF(point.position, QSizeF(1, 1)).toAlignedRect());
	}
	result = result.united(QRectF(_target, QSizeF(1, 1)).toAlignedRect());
	return result.marginsAdded({ pad, pad, pad, pad });
}

void CursorOverlay::paintEvent(QPaintEvent *e) {
	const auto mode = Extera::Int(u"tristegram_cursor"_q);
	if (!mode) {
		return;
	}
	auto p = QPainter(this);
	p.setCompositionMode(QPainter::CompositionMode_Source);
	p.fillRect(e->rect(), Qt::transparent);
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.translate(-QPointF(geometry().topLeft()));
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto now = crl::now();
	if (mode == 1) {
		// A glowing trail that fades behind the mouse.
		for (auto i = 1; i < int(_trail.size()); ++i) {
			const auto &a = _trail[i - 1];
			const auto &b = _trail[i];
			const auto life = 1. - std::clamp(
				(now - b.when) / double(kTrailLife),
				0.,
				1.);
			const auto t = i / double(_trail.size());
			auto color = QColor::fromRgbF(
				float(colors.from.redF() + (colors.to.redF() - colors.from.redF()) * t),
				float(colors.from.greenF() + (colors.to.greenF() - colors.from.greenF()) * t),
				float(colors.from.blueF() + (colors.to.blueF() - colors.from.blueF()) * t));
			auto glow = color;
			glow.setAlphaF(0.18 * life);
			p.setPen(QPen(glow, Px(10) * life + 1., Qt::SolidLine, Qt::RoundCap));
			p.drawLine(a.position, b.position);
			color.setAlphaF(0.85 * life);
			p.setPen(QPen(color, Px(3) * life + 0.5, Qt::SolidLine, Qt::RoundCap));
			p.drawLine(a.position, b.position);
		}
	} else if (mode == 2) {
		// A glowing dot just behind the mouse.
		auto glow = QRadialGradient(_follow, Px(12));
		auto c = colors.from;
		c.setAlphaF(0.45);
		glow.setColorAt(0., c);
		c.setAlphaF(0.);
		glow.setColorAt(1., c);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(_follow, Px(12), Px(12));
		p.setBrush(colors.to);
		p.drawEllipse(_follow, Px(3), Px(3));
	} else if (mode == 3) {
		// A ring around the mouse that follows it softly.
		auto ring = QLinearGradient(
			_follow - QPointF(Px(14), Px(14)),
			_follow + QPointF(Px(14), Px(14)));
		ring.setColorAt(0., colors.from);
		ring.setColorAt(1., colors.to);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(ring), Px(2)));
		p.drawEllipse(_follow, Px(14), Px(14));
	}
}

[[nodiscard]] bool Suitable(QWidget *window) {
	return window
		&& window->isWindow()
		&& window->isVisible()
		&& !window->property("tristegramEffects").toBool()
		&& !qobject_cast<QMenu*>(window)
		&& !(window->windowFlags() & Qt::Popup)
		&& !(window->windowFlags() & Qt::ToolTip)
		&& window->width() > 300
		&& window->height() > 300;
}

[[nodiscard]] CursorOverlay *OverlayFor(QWidget *window) {
	const auto existing = window->property("tristegramCursorOverlay");
	if (existing.isValid()) {
		if (const auto overlay = qobject_cast<QObject*>(
				existing.value<QObject*>())) {
			return static_cast<CursorOverlay*>(overlay);
		}
	}
	const auto overlay = new CursorOverlay(window);
	window->setProperty(
		"tristegramCursorOverlay",
		QVariant::fromValue(static_cast<QObject*>(overlay)));
	return overlay;
}

} // namespace

void InstallCursorEffect() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	base::install_event_filter(qApp, [](not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseMove) {
			return base::EventFilterResult::Continue;
		}
		// The same move goes to all the parents, handle it once.
		static auto lastTime = quint64();
		static auto lastPoint = QPoint();
		const auto mouse = static_cast<QMouseEvent*>(e.get());
		const auto global = mouse->globalPosition().toPoint();
		if (mouse->timestamp() == lastTime && global == lastPoint) {
			return base::EventFilterResult::Continue;
		}
		lastTime = mouse->timestamp();
		lastPoint = global;
		if (!Extera::Int(u"tristegram_cursor"_q)
			|| Extera::Bool(u"tristegram_eco"_q)) {
			return base::EventFilterResult::Continue;
		}
		const auto window = QApplication::activeWindow();
		if (Suitable(window)) {
			OverlayFor(window)->moved(global);
		}
		return base::EventFilterResult::Continue;
	});
}

} // namespace Tristegram
