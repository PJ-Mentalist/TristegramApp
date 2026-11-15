/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_effects.h"

#include "base/event_filter.h"
#include "base/random.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"

#include <QtCore/QPointer>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>

#include <cmath>
#include <vector>

namespace Tristegram {
namespace {

constexpr auto kPi = 3.14159265358979;

enum class Kind {
	None,
	Ring,
	Sparks,
	Hearts,
	Confetti,
};

struct Particle {
	QPointF position;
	QPointF velocity;
	QColor color;
	double size = 0.;
	double rotation = 0.;
	double spin = 0.;
	crl::time born = 0;
	crl::time life = 0;
	Kind kind = Kind::Sparks;
};

struct Ring {
	QPointF center;
	QColor color;
	crl::time born = 0;
	double radius = 0.;
};

[[nodiscard]] double Random01() {
	return base::RandomValue<uint32>() / double(0xFFFFFFFFU);
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double t) {
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * t,
		a.greenF() + (b.greenF() - a.greenF()) * t,
		a.blueF() + (b.blueF() - a.blueF()) * t);
}

void PaintHeart(QPainter &p, QPointF center, double size) {
	auto path = QPainterPath();
	const auto s = size / 2.;
	path.moveTo(center.x(), center.y() + s * 0.9);
	path.cubicTo(
		center.x() - s * 1.6, center.y() - s * 0.2,
		center.x() - s * 0.7, center.y() - s * 1.4,
		center.x(), center.y() - s * 0.45);
	path.cubicTo(
		center.x() + s * 0.7, center.y() - s * 1.4,
		center.x() + s * 1.6, center.y() - s * 0.2,
		center.x(), center.y() + s * 0.9);
	p.drawPath(path);
}

class Overlay final : public Ui::RpWidget {
public:
	explicit Overlay(QWidget *window);

	void burst(QPointF local, Kind kind, int amount, double scale);
	void ring(QPointF local);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void tick();

	std::vector<Particle> _particles;
	std::vector<Ring> _rings;
	Ui::Animations::Basic _animation;

};

Overlay::Overlay(QWidget *window)
: RpWidget(window)
, _animation([=] { tick(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	resize(window->size());
	base::install_event_filter(this, window, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			resize(parentWidget()->size());
		}
		return base::EventFilterResult::Continue;
	});
	hide();
}

void Overlay::burst(QPointF local, Kind kind, int amount, double scale) {
	const auto colors = CurrentLogoStyle();
	const auto now = crl::now();
	for (auto i = 0; i != amount; ++i) {
		auto particle = Particle();
		const auto angle = (kind == Kind::Hearts)
			? (-kPi / 2 + (Random01() - 0.5) * kPi * 0.7)
			: (Random01() * 2 * kPi);
		const auto speed = style::ConvertScale(1) * scale * ((kind == Kind::Confetti)
			? (0.18 + Random01() * 0.32)
			: (kind == Kind::Hearts)
			? (0.08 + Random01() * 0.12)
			: (0.12 + Random01() * 0.28));
		particle.position = local;
		particle.velocity = QPointF(std::cos(angle), std::sin(angle)) * speed;
		if (kind == Kind::Confetti) {
			static const auto palette = std::vector<QColor>{
				QColor(0xFF, 0x45, 0x3A),
				QColor(0xFF, 0x9F, 0x0A),
				QColor(0xFF, 0xD6, 0x0A),
				QColor(0x34, 0xC7, 0x59),
				QColor(0x0A, 0x84, 0xFF),
				QColor(0xBF, 0x5A, 0xF2),
			};
			particle.color = (Random01() < 0.5)
				? palette[base::RandomIndex(int(palette.size()))]
				: Mix(colors.from, colors.to, Random01());
		} else {
			particle.color = Mix(colors.from, colors.to, Random01());
		}
		particle.size = style::ConvertScale(1) * scale * ((kind == Kind::Hearts)
			? (9. + Random01() * 7.)
			: (kind == Kind::Confetti)
			? (5. + Random01() * 4.)
			: (2. + Random01() * 2.5));
		particle.rotation = Random01() * 360.;
		particle.spin = (Random01() - 0.5) * 0.6;
		particle.born = now;
		particle.life = (kind == Kind::Hearts)
			? (700 + crl::time(Random01() * 400))
			: (kind == Kind::Confetti)
			? (900 + crl::time(Random01() * 500))
			: (380 + crl::time(Random01() * 260));
		particle.kind = kind;
		_particles.push_back(particle);
	}
	if (_particles.size() > 600) {
		_particles.erase(
			_particles.begin(),
			_particles.begin() + (_particles.size() - 600));
	}
	show();
	raise();
	if (!_animation.animating()) {
		_animation.start();
	}
}

void Overlay::ring(QPointF local) {
	const auto colors = CurrentLogoStyle();
	_rings.push_back({
		.center = local,
		.color = colors.from,
		.born = crl::now(),
		.radius = double(style::ConvertScale(26)),
	});
	show();
	raise();
	if (!_animation.animating()) {
		_animation.start();
	}
}

void Overlay::tick() {
	const auto now = crl::now();
	_particles.erase(ranges::remove_if(_particles, [&](const Particle &p) {
		return (now - p.born) >= p.life;
	}), _particles.end());
	_rings.erase(ranges::remove_if(_rings, [&](const Ring &r) {
		return (now - r.born) >= 450;
	}), _rings.end());
	if (_particles.empty() && _rings.empty()) {
		_animation.stop();
		hide();
		return;
	}
	update();
}

void Overlay::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto now = crl::now();
	p.setPen(Qt::NoPen);
	for (const auto &particle : _particles) {
		const auto t = (now - particle.born) / double(particle.life);
		const auto dt = double(now - particle.born);
		auto position = particle.position + particle.velocity * dt;
		if (particle.kind == Kind::Confetti) {
			position.ry() += 0.00035 * dt * dt * style::ConvertScale(1);
		} else if (particle.kind == Kind::Hearts) {
			position.ry() -= 0.00004 * dt * dt * style::ConvertScale(1);
		}
		const auto fade = (t < 0.7) ? 1. : std::max(0., (1. - t) / 0.3);
		auto color = particle.color;
		color.setAlphaF(fade);
		p.setBrush(color);
		switch (particle.kind) {
		case Kind::Hearts:
			PaintHeart(p, position, particle.size * (0.6 + 0.4 * std::min(t * 4, 1.)));
			break;
		case Kind::Confetti: {
			p.save();
			p.translate(position);
			p.rotate(particle.rotation + particle.spin * dt);
			const auto w = particle.size;
			const auto h = particle.size * (0.45 + 0.35 * std::abs(std::sin(dt / 120.)));
			p.drawRect(QRectF(-w / 2, -h / 2, w, h));
			p.restore();
		} break;
		default: {
			const auto r = particle.size * (1. - t * 0.6);
			auto glow = color;
			glow.setAlphaF(fade * 0.35);
			p.setBrush(glow);
			p.drawEllipse(position, r * 2.2, r * 2.2);
			p.setBrush(color);
			p.drawEllipse(position, r, r);
		} break;
		}
	}
	for (const auto &ring : _rings) {
		const auto t = (now - ring.born) / 450.;
		const auto eased = 1. - std::pow(1. - t, 3);
		auto color = ring.color;
		color.setAlphaF((1. - t) * 0.85);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color, style::ConvertScale(2) * (1.5 - t)));
		const auto r = ring.radius * (0.25 + 0.75 * eased);
		p.drawEllipse(ring.center, r, r);
		p.setPen(Qt::NoPen);
	}
}

[[nodiscard]] Overlay *OverlayFor(QWidget *window) {
	if (!window) {
		return nullptr;
	}
	for (const auto child : window->children()) {
		if (const auto overlay = qobject_cast<Ui::RpWidget*>(child)) {
			if (overlay->property("tristegramEffects").toBool()) {
				return static_cast<Overlay*>(overlay);
			}
		}
	}
	const auto result = new Overlay(window);
	result->setProperty("tristegramEffects", true);
	return result;
}

[[nodiscard]] bool Suitable(QWidget *window) {
	return window
		&& window->isWindow()
		&& !qobject_cast<QMenu*>(window)
		&& !(window->windowFlags() & Qt::Popup)
		&& !(window->windowFlags() & Qt::ToolTip)
		&& window->width() > 300
		&& window->height() > 300;
}

void OnClick(QWidget *target, QPoint global) {
	const auto kind = Kind(std::clamp(
		Extera::Int(u"tristegram_click_effect"_q),
		0,
		int(Kind::Confetti)));
	if (kind == Kind::None || Extera::Bool(u"tristegram_eco"_q)) {
		return;
	}
	const auto window = target ? target->window() : nullptr;
	if (!Suitable(window)) {
		return;
	}
	const auto overlay = OverlayFor(window);
	const auto local = QPointF(overlay->mapFromGlobal(global));
	switch (kind) {
	case Kind::Ring: overlay->ring(local); break;
	case Kind::Sparks: overlay->burst(local, Kind::Sparks, 12, 1.); break;
	case Kind::Hearts: overlay->burst(local, Kind::Hearts, 5, 1.); break;
	case Kind::Confetti: overlay->burst(local, Kind::Confetti, 18, 1.); break;
	case Kind::None: break;
	}
}

void OnType(QWidget *focused) {
	const auto kind = Extera::Int(u"tristegram_type_effect"_q);
	if (!kind || Extera::Bool(u"tristegram_eco"_q)) {
		return;
	}
	const auto edit = qobject_cast<QTextEdit*>(focused);
	const auto window = focused ? focused->window() : nullptr;
	if (!edit || !Suitable(window)) {
		return;
	}
	const auto caret = edit->cursorRect();
	const auto global = edit->viewport()->mapToGlobal(caret.center());
	const auto overlay = OverlayFor(window);
	const auto local = QPointF(overlay->mapFromGlobal(global));
	switch (kind) {
	case 1: overlay->burst(local, Kind::Sparks, 5, 0.7); break;
	case 2: overlay->burst(local, Kind::Hearts, 2, 0.6); break;
	case 3: overlay->burst(local, Kind::Confetti, 6, 0.6); break;
	}
}

} // namespace

void InstallInputEffects() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	base::install_event_filter(qApp, [](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			if (mouse->button() == Qt::LeftButton) {
				OnClick(
					QApplication::widgetAt(mouse->globalPosition().toPoint()),
					mouse->globalPosition().toPoint());
			}
		} else if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (!key->text().isEmpty()
				&& !key->isAutoRepeat()
				&& key->text().at(0).isPrint()) {
				OnType(QApplication::focusWidget());
			}
		}
		return base::EventFilterResult::Continue;
	});
}

} // namespace Tristegram
