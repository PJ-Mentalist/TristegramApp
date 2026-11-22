/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_effects.h"

#include "base/random.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QPointer>
#include <QtGui/QPainterPath>
#include <QtGui/QtEvents>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QWidget>

#include <cmath>
#include <optional>
#include <vector>

namespace Tristegram {
namespace {

constexpr auto kPi = 3.14159265358979;
constexpr auto kMaxWindows = 8;
constexpr auto kMaxParticles = 400;
constexpr auto kSide = 360.;

enum class Click {
	None,
	Ripple,
	Double,
	Sparks,
	Hearts,
	Stars,
	Confetti,
	Firework,
	Bubbles,
	Snow,
	Petals,
	Pulse,
	Pixels,
	Rainbow,
	Lightning,
	Count,
};

enum class Type {
	None,
	Sparks,
	Hearts,
	Stars,
	Confetti,
	Letters,
	Bubbles,
	Embers,
	Snow,
	Pixels,
	Rainbow,
	Flash,
	Petals,
	Count,
};

enum class Draw {
	Spark,
	Heart,
	Star,
	Confetti,
	Bubble,
	Snow,
	Petal,
	Pixel,
	Letter,
	Ember,
	Bolt,
	Ray,
	Ring,
	Pulse,
	RainbowRing,
	Flash,
};

struct Particle {
	Draw draw = Draw::Spark;
	QPointF origin;
	QPointF velocity; // px per ms
	double gravity = 0.; // px per ms^2, down
	double wobble = 0.; // px
	double size = 0.;
	double unit = 1.;
	double rotation = 0.;
	double spin = 0.; // degrees per ms
	double seed = 0.;
	bool drag = false;
	QColor color;
	crl::time born = 0;
	crl::time life = 1;
	QString text;
};

struct Rng {
	uint32 state = 0;

	double operator()() {
		state = state * 1664525U + 1013904223U;
		return (state >> 8) / double(1U << 24);
	}
	double range(double from, double till) {
		return from + (till - from) * (*this)();
	}
};

struct Params {
	double unit = 1.;
	double amount = 1.;
	int colorMode = 0;
};

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double t) {
	return QColor::fromRgbF(
		float(a.redF() + (b.redF() - a.redF()) * t),
		float(a.greenF() + (b.greenF() - a.greenF()) * t),
		float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

[[nodiscard]] QColor Hex(const char *value) {
	return QColor(QString::fromLatin1(value));
}

[[nodiscard]] QColor Pick(Rng &rng, int mode) {
	switch (mode) {
	case 1:
		return QColor::fromHsvF(float(rng()), 0.72f, 1.f);
	case 2: {
		const auto base = st::windowActiveTextFg->c;
		return Mix(base, QColor(255, 255, 255), rng() * 0.35);
	}
	case 3:
		return Mix(Hex("#FFE066"), Hex("#FF9F0A"), rng());
	case 4: {
		auto custom = QColor(Extera::String(u"tristegram_fx_custom"_q));
		if (!custom.isValid()) {
			custom = CurrentLogoStyle().from;
		}
		return Mix(custom, QColor(255, 255, 255), rng() * 0.3);
	}
	}
	const auto logo = CurrentLogoStyle();
	return Mix(logo.from, logo.to, rng());
}

[[nodiscard]] QColor Festive(Rng &rng, int mode) {
	if (mode == 4 || mode == 3) {
		return Pick(rng, mode);
	}
	static const auto palette = std::vector<QColor>{
		Hex("#FF453A"),
		Hex("#FF9F0A"),
		Hex("#FFD60A"),
		Hex("#34C759"),
		Hex("#0A84FF"),
		Hex("#BF5AF2"),
		Hex("#FF2D92"),
	};
	return (rng() < 0.6)
		? palette[std::min(int(rng() * palette.size()), int(palette.size()) - 1)]
		: Pick(rng, mode);
}

[[nodiscard]] QColor Warm(Rng &rng, int mode) {
	return (mode == 0)
		? Mix(Hex("#FF5A1F"), Hex("#FFD60A"), rng())
		: Pick(rng, mode);
}

[[nodiscard]] QColor Light(QColor color, double amount) {
	return Mix(color, QColor(255, 255, 255), amount);
}

class Spawner final {
public:
	Spawner(
		std::vector<Particle> &list,
		QPointF at,
		crl::time now,
		Rng &rng,
		Params params)
	: _list(list)
	, _at(at)
	, _now(now)
	, _rng(rng)
	, _params(params) {
	}

	[[nodiscard]] int count(double base) const {
		return std::max(1, int(std::round(base * _params.amount)));
	}
	[[nodiscard]] double u() const {
		return _params.unit;
	}
	Rng &rng() {
		return _rng;
	}
	[[nodiscard]] int mode() const {
		return _params.colorMode;
	}

	Particle &add(Draw draw, crl::time life, crl::time delay = 0) {
		auto particle = Particle();
		particle.draw = draw;
		particle.origin = _at;
		particle.unit = _params.unit;
		particle.born = _now + delay;
		particle.life = std::max(life, crl::time(1));
		particle.seed = _rng();
		particle.rotation = _rng() * 360.;
		particle.color = Pick(_rng, _params.colorMode);
		_list.push_back(particle);
		return _list.back();
	}

	// Angle in radians, speed in px per ms before the unit.
	void aim(Particle &particle, double angle, double speed) {
		particle.velocity = QPointF(std::cos(angle), std::sin(angle))
			* speed
			* _params.unit;
	}

	[[nodiscard]] double around() {
		return _rng() * 2 * kPi;
	}
	[[nodiscard]] double up(double spread) {
		return -kPi / 2 + (_rng() - 0.5) * spread;
	}

private:
	std::vector<Particle> &_list;
	QPointF _at;
	crl::time _now = 0;
	Rng &_rng;
	Params _params;

};

void SpawnClick(Spawner &s, Click kind) {
	auto &rng = s.rng();
	const auto u = s.u();
	switch (kind) {
	case Click::Ripple: {
		auto &ring = s.add(Draw::Ring, 560);
		ring.size = 34 * u;
	} break;
	case Click::Double: {
		auto &first = s.add(Draw::Ring, 520);
		first.size = 28 * u;
		const auto color = first.color;
		auto &second = s.add(Draw::Ring, 620, 110);
		second.size = 46 * u;
		second.color = color;
	} break;
	case Click::Sparks:
		for (auto i = 0, n = s.count(14); i != n; ++i) {
			auto &p = s.add(Draw::Spark, crl::time(rng.range(380, 640)));
			s.aim(p, s.around(), rng.range(0.12, 0.34));
			p.drag = true;
			p.size = rng.range(1.8, 3.6) * u;
		}
		break;
	case Click::Hearts:
		for (auto i = 0, n = s.count(6); i != n; ++i) {
			auto &p = s.add(Draw::Heart, crl::time(rng.range(750, 1100)));
			s.aim(p, s.up(kPi * 0.65), rng.range(0.05, 0.13));
			p.gravity = -0.00005 * u;
			p.wobble = 3 * u;
			p.size = rng.range(8, 15) * u;
			p.rotation = rng.range(-20, 20);
			p.spin = rng.range(-0.03, 0.03);
		}
		break;
	case Click::Stars:
		for (auto i = 0, n = s.count(9); i != n; ++i) {
			auto &p = s.add(Draw::Star, crl::time(rng.range(600, 900)));
			s.aim(p, s.around(), rng.range(0.1, 0.26));
			p.drag = true;
			p.size = rng.range(6, 11) * u;
			p.spin = rng.range(-0.4, 0.4);
		}
		break;
	case Click::Confetti:
		for (auto i = 0, n = s.count(22); i != n; ++i) {
			auto &p = s.add(Draw::Confetti, crl::time(rng.range(900, 1250)));
			s.aim(p, s.up(kPi * 0.9), rng.range(0.12, 0.26));
			p.drag = true;
			p.gravity = 0.00016 * u;
			p.wobble = 2 * u;
			p.size = rng.range(4.5, 8) * u;
			p.spin = rng.range(-0.5, 0.5);
			p.color = Festive(rng, s.mode());
		}
		break;
	case Click::Firework: {
		const auto n = s.count(18);
		for (auto i = 0; i != n; ++i) {
			auto &p = s.add(Draw::Ray, crl::time(rng.range(650, 850)));
			const auto angle = (i / double(n)) * 2 * kPi + rng.range(-0.12, 0.12);
			s.aim(p, angle, rng.range(0.24, 0.32));
			p.drag = true;
			p.gravity = 0.00018 * u;
			p.size = 2.2 * u;
			p.color = Light(Festive(rng, s.mode()), 0.15);
		}
		auto &flash = s.add(Draw::Flash, 320);
		flash.size = 18 * u;
	} break;
	case Click::Bubbles:
		for (auto i = 0, n = s.count(8); i != n; ++i) {
			auto &p = s.add(Draw::Bubble, crl::time(rng.range(800, 1200)));
			s.aim(p, s.up(kPi * 0.8), rng.range(0.04, 0.11));
			p.gravity = -0.00004 * u;
			p.wobble = 4 * u;
			p.size = rng.range(5, 11) * u;
		}
		break;
	case Click::Snow:
		for (auto i = 0, n = s.count(10); i != n; ++i) {
			auto &p = s.add(Draw::Snow, crl::time(rng.range(900, 1300)));
			s.aim(p, s.around(), rng.range(0.06, 0.15));
			p.drag = true;
			p.gravity = 0.00006 * u;
			p.wobble = 3 * u;
			p.size = rng.range(5, 9) * u;
			p.spin = rng.range(-0.12, 0.12);
			p.color = Light(p.color, 0.45);
		}
		break;
	case Click::Petals:
		for (auto i = 0, n = s.count(10); i != n; ++i) {
			auto &p = s.add(Draw::Petal, crl::time(rng.range(1000, 1400)));
			s.aim(p, s.up(kPi * 1.2), rng.range(0.08, 0.18));
			p.drag = true;
			p.gravity = 0.0001 * u;
			p.wobble = 6 * u;
			p.size = rng.range(6, 10) * u;
			p.spin = rng.range(-0.3, 0.3);
			if (s.mode() == 0) {
				p.color = Mix(Hex("#FFB7C5"), Hex("#FF6F9C"), rng());
			}
		}
		break;
	case Click::Pulse: {
		auto &pulse = s.add(Draw::Pulse, 480);
		pulse.size = 40 * u;
		const auto color = pulse.color;
		auto &ring = s.add(Draw::Ring, 520, 40);
		ring.size = 46 * u;
		ring.color = color;
	} break;
	case Click::Pixels:
		for (auto i = 0, n = s.count(16); i != n; ++i) {
			auto &p = s.add(Draw::Pixel, crl::time(rng.range(500, 800)));
			s.aim(p, s.around(), rng.range(0.1, 0.26));
			p.drag = true;
			p.gravity = 0.00012 * u;
			p.size = std::round(rng.range(3, 6)) * u;
		}
		break;
	case Click::Rainbow: {
		auto &outer = s.add(Draw::RainbowRing, 620);
		outer.size = 40 * u;
		const auto rotation = outer.rotation;
		auto &inner = s.add(Draw::RainbowRing, 560, 90);
		inner.size = 26 * u;
		inner.rotation = rotation + 90;
	} break;
	case Click::Lightning: {
		const auto n = s.count(6);
		for (auto i = 0; i != n; ++i) {
			auto &p = s.add(Draw::Bolt, crl::time(rng.range(260, 340)));
			const auto angle = (i / double(n)) * 2 * kPi + rng.range(-0.3, 0.3);
			s.aim(p, angle, 1.);
			p.size = rng.range(26, 40) * u;
			p.color = Light(p.color, 0.25);
		}
		auto &flash = s.add(Draw::Flash, 260);
		flash.size = 14 * u;
	} break;
	case Click::None:
	case Click::Count:
		break;
	}
}

void SpawnType(Spawner &s, Type kind, const QString &letter) {
	auto &rng = s.rng();
	const auto u = s.u();
	switch (kind) {
	case Type::Sparks:
	case Type::Rainbow:
		for (auto i = 0, n = s.count(5); i != n; ++i) {
			auto &p = s.add(Draw::Spark, crl::time(rng.range(320, 520)));
			s.aim(p, s.around(), rng.range(0.06, 0.18));
			p.drag = true;
			p.size = rng.range(1.4, 2.6) * u;
			if (kind == Type::Rainbow) {
				p.color = QColor::fromHsvF(float(rng()), 0.75f, 1.f);
			}
		}
		break;
	case Type::Hearts:
		for (auto i = 0, n = s.count(2); i != n; ++i) {
			auto &p = s.add(Draw::Heart, crl::time(rng.range(650, 900)));
			s.aim(p, s.up(kPi * 0.5), rng.range(0.04, 0.08));
			p.gravity = -0.00004 * u;
			p.wobble = 2 * u;
			p.size = rng.range(6, 9) * u;
			p.rotation = rng.range(-20, 20);
		}
		break;
	case Type::Stars:
		for (auto i = 0, n = s.count(3); i != n; ++i) {
			auto &p = s.add(Draw::Star, crl::time(rng.range(500, 700)));
			s.aim(p, s.around(), rng.range(0.05, 0.14));
			p.drag = true;
			p.size = rng.range(4, 7) * u;
			p.spin = rng.range(-0.4, 0.4);
		}
		break;
	case Type::Confetti:
		for (auto i = 0, n = s.count(6); i != n; ++i) {
			auto &p = s.add(Draw::Confetti, crl::time(rng.range(700, 950)));
			s.aim(p, s.up(kPi * 0.9), rng.range(0.07, 0.16));
			p.drag = true;
			p.gravity = 0.00014 * u;
			p.size = rng.range(3.5, 6) * u;
			p.spin = rng.range(-0.5, 0.5);
			p.color = Festive(rng, s.mode());
		}
		break;
	case Type::Letters: {
		const auto text = letter.trimmed();
		if (text.isEmpty()) {
			break;
		}
		auto &p = s.add(Draw::Letter, crl::time(rng.range(650, 800)));
		p.velocity = QPointF(rng.range(-0.02, 0.02), -rng.range(0.06, 0.09)) * u;
		p.size = 17 * u;
		p.rotation = rng.range(-14, 14);
		p.spin = rng.range(-0.02, 0.02);
		p.text = text.left(2);
	} break;
	case Type::Bubbles:
		for (auto i = 0, n = s.count(3); i != n; ++i) {
			auto &p = s.add(Draw::Bubble, crl::time(rng.range(700, 950)));
			s.aim(p, s.up(kPi * 0.6), rng.range(0.03, 0.07));
			p.gravity = -0.00003 * u;
			p.wobble = 3 * u;
			p.size = rng.range(3.5, 7) * u;
		}
		break;
	case Type::Embers:
		for (auto i = 0, n = s.count(4); i != n; ++i) {
			auto &p = s.add(Draw::Ember, crl::time(rng.range(500, 800)));
			p.velocity = QPointF(
				rng.range(-0.03, 0.03),
				-rng.range(0.05, 0.11)) * u;
			p.wobble = 2 * u;
			p.size = rng.range(1.5, 2.8) * u;
			p.color = Warm(rng, s.mode());
		}
		break;
	case Type::Snow:
		for (auto i = 0, n = s.count(3); i != n; ++i) {
			auto &p = s.add(Draw::Snow, crl::time(rng.range(800, 1100)));
			p.velocity = QPointF(rng.range(-0.03, 0.03), rng.range(0.01, 0.04)) * u;
			p.gravity = 0.00003 * u;
			p.wobble = 3 * u;
			p.size = rng.range(4, 7) * u;
			p.spin = rng.range(-0.1, 0.1);
			p.color = Light(p.color, 0.45);
		}
		break;
	case Type::Pixels:
		for (auto i = 0, n = s.count(4); i != n; ++i) {
			auto &p = s.add(Draw::Pixel, crl::time(rng.range(450, 650)));
			s.aim(p, s.up(kPi * 1.3), rng.range(0.06, 0.14));
			p.drag = true;
			p.gravity = 0.0002 * u;
			p.size = std::round(rng.range(2, 4)) * u;
		}
		break;
	case Type::Flash: {
		auto &flash = s.add(Draw::Flash, 320);
		flash.size = 11 * u;
		const auto color = flash.color;
		auto &ring = s.add(Draw::Ring, 360);
		ring.size = 14 * u;
		ring.color = color;
	} break;
	case Type::Petals:
		for (auto i = 0, n = s.count(3); i != n; ++i) {
			auto &p = s.add(Draw::Petal, crl::time(rng.range(800, 1100)));
			s.aim(p, s.up(kPi * 1.2), rng.range(0.05, 0.1));
			p.drag = true;
			p.gravity = 0.00008 * u;
			p.wobble = 4 * u;
			p.size = rng.range(4.5, 7) * u;
			p.spin = rng.range(-0.3, 0.3);
			if (s.mode() == 0) {
				p.color = Mix(Hex("#FFB7C5"), Hex("#FF6F9C"), rng());
			}
		}
		break;
	case Type::None:
	case Type::Count:
		break;
	}
}

[[nodiscard]] QPainterPath HeartPath(QPointF center, double size) {
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
	return path;
}

[[nodiscard]] QPainterPath StarPath(double radius) {
	auto path = QPainterPath();
	for (auto i = 0; i != 10; ++i) {
		const auto r = (i % 2) ? radius * 0.45 : radius;
		const auto angle = -kPi / 2 + i * kPi / 5;
		const auto point = QPointF(std::cos(angle) * r, std::sin(angle) * r);
		if (!i) {
			path.moveTo(point);
		} else {
			path.lineTo(point);
		}
	}
	path.closeSubpath();
	return path;
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha * color.alphaF(), 0., 1.));
	return color;
}

[[nodiscard]] double EaseOut(double t) {
	return 1. - std::pow(1. - t, 3);
}

void PaintParticle(QPainter &p, const Particle &particle, crl::time now) {
	const auto age = double(now - particle.born);
	if (age < 0.) {
		return;
	}
	const auto t = std::clamp(age / particle.life, 0., 1.);
	const auto u = particle.unit;
	auto position = particle.origin;
	if (particle.drag) {
		const auto travel = particle.life * (1. - std::pow(1. - t, 2.)) / 2.;
		position += particle.velocity * travel;
	} else {
		position += particle.velocity * age;
	}
	position.ry() += 0.5 * particle.gravity * age * age;
	if (particle.wobble != 0.) {
		position.rx() += std::sin(age / 240. + particle.seed * 2 * kPi)
			* particle.wobble;
	}
	const auto fade = (t < 0.65) ? 1. : std::max(0., (1. - t) / 0.35);
	const auto grow = std::min(1., t * 6.);
	const auto rotation = particle.rotation + particle.spin * age;
	const auto color = Alpha(particle.color, fade);

	p.setPen(Qt::NoPen);
	switch (particle.draw) {
	case Draw::Spark:
	case Draw::Ember: {
		auto alpha = fade;
		if (particle.draw == Draw::Ember) {
			alpha *= 0.7 + 0.3 * std::sin(age / 38. + particle.seed * 10.);
		}
		const auto r = particle.size * (1. - t * 0.55);
		p.setBrush(Alpha(particle.color, alpha * 0.3));
		p.drawEllipse(position, r * 2.4, r * 2.4);
		p.setBrush(Alpha(particle.color, alpha));
		p.drawEllipse(position, r, r);
		p.setBrush(Alpha(QColor(255, 255, 255), alpha * 0.7));
		p.drawEllipse(position, r * 0.45, r * 0.45);
	} break;
	case Draw::Heart: {
		const auto size = particle.size * (0.5 + 0.5 * grow);
		p.save();
		p.translate(position);
		p.rotate(rotation);
		p.setBrush(color);
		p.drawPath(HeartPath(QPointF(), size));
		p.setBrush(Alpha(QColor(255, 255, 255), fade * 0.45));
		p.drawEllipse(
			QPointF(-size * 0.2, -size * 0.22),
			size * 0.12,
			size * 0.08);
		p.restore();
	} break;
	case Draw::Star: {
		const auto radius = particle.size * (0.4 + 0.6 * grow) * (1. - t * 0.3);
		p.save();
		p.translate(position);
		p.rotate(rotation);
		p.setBrush(Alpha(particle.color, fade * 0.25));
		p.drawPath(StarPath(radius * 1.6));
		p.setBrush(color);
		p.drawPath(StarPath(radius));
		p.restore();
	} break;
	case Draw::Confetti: {
		p.save();
		p.translate(position);
		p.rotate(rotation);
		const auto w = particle.size;
		const auto h = particle.size
			* (0.25 + 0.5 * std::abs(std::sin(age / 110. + particle.seed * 5.)));
		p.setBrush(color);
		p.drawRoundedRect(QRectF(-w / 2, -h / 2, w, h), u * 0.8, u * 0.8);
		p.restore();
	} break;
	case Draw::Bubble: {
		const auto r = particle.size * (0.5 + 0.5 * grow);
		p.setBrush(Alpha(particle.color, fade * 0.16));
		p.setPen(QPen(Alpha(particle.color, fade * 0.9), std::max(1., r * 0.14)));
		p.drawEllipse(position, r, r);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(QColor(255, 255, 255), fade * 0.8));
		p.drawEllipse(
			position + QPointF(-r * 0.38, -r * 0.38),
			r * 0.22,
			r * 0.16);
	} break;
	case Draw::Snow: {
		const auto r = particle.size * (0.5 + 0.5 * grow);
		p.save();
		p.translate(position);
		p.rotate(rotation);
		p.setPen(QPen(color, std::max(1., r * 0.16), Qt::SolidLine, Qt::RoundCap));
		for (auto i = 0; i != 3; ++i) {
			p.drawLine(QPointF(-r, 0), QPointF(r, 0));
			p.drawLine(QPointF(r * 0.55, 0), QPointF(r * 0.8, -r * 0.25));
			p.drawLine(QPointF(r * 0.55, 0), QPointF(r * 0.8, r * 0.25));
			p.drawLine(QPointF(-r * 0.55, 0), QPointF(-r * 0.8, -r * 0.25));
			p.drawLine(QPointF(-r * 0.55, 0), QPointF(-r * 0.8, r * 0.25));
			p.rotate(60);
		}
		p.restore();
	} break;
	case Draw::Petal: {
		const auto w = particle.size;
		const auto h = particle.size * (0.35
			+ 0.25 * std::abs(std::sin(age / 160. + particle.seed * 4.)));
		p.save();
		p.translate(position);
		p.rotate(rotation);
		auto fill = QLinearGradient(QPointF(-w / 2, 0), QPointF(w / 2, 0));
		fill.setColorAt(0., color);
		fill.setColorAt(1., Alpha(Light(particle.color, 0.5), fade));
		p.setBrush(fill);
		auto path = QPainterPath();
		path.moveTo(-w / 2, 0);
		path.quadTo(0, -h, w / 2, 0);
		path.quadTo(0, h, -w / 2, 0);
		p.drawPath(path);
		p.restore();
	} break;
	case Draw::Pixel: {
		const auto size = particle.size;
		const auto snap = std::max(1., u * 2.);
		const auto x = std::round(position.x() / snap) * snap;
		const auto y = std::round(position.y() / snap) * snap;
		p.setBrush(color);
		p.drawRect(QRectF(x - size / 2, y - size / 2, size, size));
		p.setBrush(Alpha(QColor(255, 255, 255), fade * 0.35));
		p.drawRect(QRectF(x - size / 2, y - size / 2, size / 2, size / 2));
	} break;
	case Draw::Letter: {
		auto font = QFont(st::semiboldFont->f);
		font.setPixelSize(std::max(8, int(particle.size * (0.7 + 0.3 * grow))));
		p.setFont(font);
		p.save();
		p.translate(position);
		p.rotate(rotation);
		const auto box = QRectF(-particle.size, -particle.size, particle.size * 2, particle.size * 2);
		p.setPen(Alpha(particle.color, fade * 0.35));
		p.drawText(box.translated(0, u), Qt::AlignCenter, particle.text);
		p.setPen(color);
		p.drawText(box, Qt::AlignCenter, particle.text);
		p.restore();
	} break;
	case Draw::Bolt: {
		const auto length = particle.size * std::min(1., t * 4.);
		const auto direction = particle.velocity / std::max(
			std::hypot(particle.velocity.x(), particle.velocity.y()),
			0.0001);
		const auto normal = QPointF(-direction.y(), direction.x());
		auto path = QPainterPath();
		const auto start = particle.origin + direction * u * 5.;
		path.moveTo(start);
		const auto steps = 4;
		for (auto i = 1; i <= steps; ++i) {
			const auto along = length * i / double(steps);
			const auto side = (i == steps)
				? 0.
				: std::sin(particle.seed * 30. + i * 2.1) * u * 4.;
			path.lineTo(start + direction * along + normal * side);
		}
		p.setBrush(Qt::NoBrush);
		const auto width = std::max(0.8, u * 2.2 * (1. - t));
		p.setPen(QPen(Alpha(particle.color, fade * 0.35), width * 3., Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
		p.setPen(QPen(Alpha(Light(particle.color, 0.6), fade), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		p.drawPath(path);
	} break;
	case Draw::Ray: {
		const auto speed = std::hypot(particle.velocity.x(), particle.velocity.y());
		const auto direction = (speed > 0.)
			? (particle.velocity / speed)
			: QPointF(1, 0);
		const auto tail = particle.size * 5. * (1. - t);
		const auto width = std::max(0.6, particle.size * (1. - t * 0.7));
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(position - direction * tail, position);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(QColor(255, 255, 255), fade * 0.9));
		p.drawEllipse(position, width * 0.7, width * 0.7);
	} break;
	case Draw::Ring:
	case Draw::RainbowRing: {
		const auto r = particle.size * (0.2 + 0.8 * EaseOut(t));
		const auto width = u * 2.6 * (1. - t) + 0.6;
		p.setBrush(Qt::NoBrush);
		if (particle.draw == Draw::RainbowRing) {
			auto gradient = QConicalGradient(position, rotation);
			for (auto i = 0; i <= 6; ++i) {
				gradient.setColorAt(
					i / 6.,
					Alpha(QColor::fromHsvF(float((i % 6) / 6.), 0.75f, 1.f), 1. - t));
			}
			p.setPen(QPen(QBrush(gradient), width));
		} else {
			p.setPen(QPen(Alpha(particle.color, (1. - t) * 0.9), width));
		}
		p.drawEllipse(position, r, r);
	} break;
	case Draw::Pulse: {
		const auto r = particle.size * EaseOut(t);
		auto gradient = QRadialGradient(position, std::max(r, 1.));
		gradient.setColorAt(0., Alpha(particle.color, (1. - t) * 0.05));
		gradient.setColorAt(0.7, Alpha(particle.color, (1. - t) * 0.35));
		gradient.setColorAt(1., Alpha(particle.color, 0.));
		p.setBrush(gradient);
		p.drawEllipse(position, r, r);
	} break;
	case Draw::Flash: {
		const auto r = particle.size * (0.5 + 0.5 * EaseOut(t));
		auto gradient = QRadialGradient(position, std::max(r, 1.));
		gradient.setColorAt(0., Alpha(QColor(255, 255, 255), (1. - t) * 0.9));
		gradient.setColorAt(0.35, Alpha(particle.color, (1. - t) * 0.6));
		gradient.setColorAt(1., Alpha(particle.color, 0.));
		p.setBrush(gradient);
		p.drawEllipse(position, r, r);
	} break;
	}
}

[[nodiscard]] Params CurrentParams() {
	const auto size = std::clamp(Extera::Int(u"tristegram_fx_size"_q), 0, 2);
	const auto amount = std::clamp(
		Extera::Int(u"tristegram_fx_amount"_q),
		0,
		2);
	return {
		.unit = (style::Scale() / 100.)
			* ((size == 0) ? 0.72 : (size == 2) ? 1.45 : 1.),
		.amount = (amount == 0) ? 0.6 : (amount == 2) ? 1.7 : 1.,
		.colorMode = std::clamp(Extera::Int(u"tristegram_fx_color"_q), 0, 4),
	};
}

[[nodiscard]] Rng LiveRng() {
	return Rng{ base::RandomValue<uint32>() };
}

// A small see-through window on top of everything for one burst.
class FxWindow final : public QWidget {
public:
	FxWindow()
	: QWidget(nullptr, Qt::ToolTip
		| Qt::FramelessWindowHint
		| Qt::NoDropShadowWindowHint
		| Qt::WindowStaysOnTopHint
		| Qt::WindowTransparentForInput
		| Qt::WindowDoesNotAcceptFocus)
	, _animation([=] { tick(); }) {
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_ShowWithoutActivating);
		setAttribute(Qt::WA_X11DoNotAcceptFocus);
		setFocusPolicy(Qt::NoFocus);
		setProperty("tristegramEffects", true);
	}

	[[nodiscard]] bool busy() const {
		return !_particles.empty();
	}
	[[nodiscard]] crl::time used() const {
		return _used;
	}

	void place(QPoint center, int side) {
		const auto geometry = QRect(
			center - QPoint(side / 2, side / 2),
			QSize(side, side));
		if (geometry != this->geometry()) {
			_particles.clear();
			setGeometry(geometry);
		}
	}

	template <typename Callback>
	void spawn(QPoint global, Callback &&callback) {
		const auto now = crl::now();
		_used = now;
		auto rng = LiveRng();
		auto spawner = Spawner(
			_particles,
			QPointF(global - geometry().topLeft()),
			now,
			rng,
			CurrentParams());
		callback(spawner);
		if (int(_particles.size()) > kMaxParticles) {
			_particles.erase(
				_particles.begin(),
				_particles.begin() + (_particles.size() - kMaxParticles));
		}
		if (isHidden()) {
			show();
		}
		raise();
		if (!_animation.animating()) {
			_animation.start();
		}
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.fillRect(e->rect(), Qt::transparent);
		p.setCompositionMode(QPainter::CompositionMode_SourceOver);
		auto hq = PainterHighQualityEnabler(p);
		const auto now = crl::now();
		for (const auto &particle : _particles) {
			PaintParticle(p, particle, now);
		}
	}

private:
	void tick() {
		const auto now = crl::now();
		_particles.erase(ranges::remove_if(_particles, [&](const Particle &p) {
			return (now - p.born) >= p.life;
		}), _particles.end());
		if (_particles.empty()) {
			_animation.stop();
			hide();
			return;
		}
		update();
	}

	std::vector<Particle> _particles;
	Ui::Animations::Basic _animation;
	crl::time _used = 0;

};

std::vector<QPointer<FxWindow>> &Windows() {
	static auto result = std::vector<QPointer<FxWindow>>();
	return result;
}

[[nodiscard]] FxWindow *WindowFor(QPoint global) {
	const auto side = int(std::ceil(kSide * CurrentParams().unit));
	auto &list = Windows();
	list.erase(ranges::remove_if(list, [](const QPointer<FxWindow> &w) {
		return !w;
	}), list.end());
	for (const auto &window : list) {
		if (window->busy()
			&& window->width() == side
			&& (window->geometry().center() - global).manhattanLength()
				< side / 4) {
			return window.data();
		}
	}
	auto chosen = (FxWindow*)nullptr;
	for (const auto &window : list) {
		if (!window->busy()) {
			chosen = window.data();
			break;
		}
	}
	if (!chosen && int(list.size()) < kMaxWindows) {
		static auto connected = false;
		if (!connected) {
			connected = true;
			QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] {
				for (const auto &window : Windows()) {
					delete window.data();
				}
				Windows().clear();
			});
		}
		list.push_back(new FxWindow());
		chosen = list.back().data();
	}
	if (!chosen) {
		chosen = ranges::min_element(list, ranges::less(), [](const auto &w) {
			return w->used();
		})->data();
	}
	chosen->place(global, side);
	return chosen;
}

[[nodiscard]] bool Enabled() {
	return !Extera::Bool(u"tristegram_eco"_q);
}

void OnClick(QPoint global) {
	const auto kind = std::clamp(
		Extera::Int(u"tristegram_fx_click"_q),
		0,
		int(Click::Count) - 1);
	if (!kind || !Enabled()) {
		return;
	}
	PlayClickEffect(global, kind);
}

[[nodiscard]] std::optional<QPoint> CaretOf(QWidget *focused) {
	if (!focused) {
		return std::nullopt;
	}
	if (const auto edit = qobject_cast<QTextEdit*>(focused)) {
		return edit->viewport()->mapToGlobal(edit->cursorRect().center());
	}
	const auto rect = focused->inputMethodQuery(
		Qt::ImCursorRectangle).toRectF();
	if (rect.isNull() || !rect.isValid()) {
		return std::nullopt;
	}
	return focused->mapToGlobal(rect.center().toPoint());
}

void OnType(QPointer<QWidget> focused, QString text) {
	const auto kind = std::clamp(
		Extera::Int(u"tristegram_fx_type"_q),
		0,
		int(Type::Count) - 1);
	if (!kind || !Enabled()) {
		return;
	}
	// Wait for the letter to be inserted, so the caret is after it.
	crl::on_main([=] {
		const auto caret = CaretOf(focused.data());
		if (!caret) {
			return;
		}
		WindowFor(*caret)->spawn(*caret, [&](Spawner &s) {
			SpawnType(s, Type(kind), text);
		});
	});
}

class Filter final : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *e) override {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			if (!watched->isWidgetType()) {
				return false;
			}
			const auto mouse = static_cast<QMouseEvent*>(e);
			if (mouse->button() != Qt::LeftButton) {
				return false;
			}
			const auto global = mouse->globalPosition().toPoint();
			// The press goes to the parents too, play it once.
			if (mouse->timestamp() == _lastPress && global == _lastPoint) {
				return false;
			}
			_lastPress = mouse->timestamp();
			_lastPoint = global;
			if (!watched->property("tristegramEffects").toBool()) {
				OnClick(global);
			}
		} else if (type == QEvent::KeyPress) {
			if (watched != QApplication::focusWidget()) {
				return false;
			}
			const auto key = static_cast<QKeyEvent*>(e);
			const auto text = key->text();
			if (!text.isEmpty() && text.at(0).isPrint()) {
				OnType(static_cast<QWidget*>(watched), text);
			}
		}
		return false;
	}

private:
	quint64 _lastPress = 0;
	QPoint _lastPoint;

};

void PaintPreview(
		QPainter &p,
		QRectF rect,
		Fn<void(Spawner&)> spawn,
		crl::time at) {
	auto list = std::vector<Particle>();
	auto rng = Rng{ 0x7A11E5U };
	auto params = Params{
		.unit = std::min(rect.width(), rect.height()) / 100.,
		.amount = 1.,
		.colorMode = std::clamp(Extera::Int(u"tristegram_fx_color"_q), 0, 4),
	};
	auto spawner = Spawner(list, rect.center(), 0, rng, params);
	spawn(spawner);
	p.save();
	p.setClipRect(rect.marginsAdded(QMarginsF(6, 6, 6, 6)));
	auto hq = PainterHighQualityEnabler(p);
	for (const auto &particle : list) {
		PaintParticle(p, particle, at);
	}
	p.restore();
}

void PaintNone(QPainter &p, QRectF rect) {
	auto hq = PainterHighQualityEnabler(p);
	const auto r = std::min(rect.width(), rect.height()) * 0.3;
	p.setPen(QPen(st::windowSubTextFg, std::max(1.5, r * 0.14)));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(rect.center(), r, r);
	p.drawLine(
		rect.center() + QPointF(-r * 0.7, r * 0.7),
		rect.center() + QPointF(r * 0.7, -r * 0.7));
}

} // namespace

void InstallInputEffects() {
	static auto installed = false;
	if (installed) {
		return;
	}
	installed = true;
	qApp->installEventFilter(new Filter(qApp));
}

int ClickEffectsCount() {
	return int(Click::Count);
}

QString ClickEffectName(int index) {
	switch (Click(index)) {
	case Click::None: return tr::lng_tgfx_none(tr::now);
	case Click::Ripple: return tr::lng_tgfx_ripple(tr::now);
	case Click::Double: return tr::lng_tgfx_double(tr::now);
	case Click::Sparks: return tr::lng_tgfx_sparks(tr::now);
	case Click::Hearts: return tr::lng_tgfx_hearts(tr::now);
	case Click::Stars: return tr::lng_tgfx_stars(tr::now);
	case Click::Confetti: return tr::lng_tgfx_confetti(tr::now);
	case Click::Firework: return tr::lng_tgfx_firework(tr::now);
	case Click::Bubbles: return tr::lng_tgfx_bubbles(tr::now);
	case Click::Snow: return tr::lng_tgfx_snow(tr::now);
	case Click::Petals: return tr::lng_tgfx_petals(tr::now);
	case Click::Pulse: return tr::lng_tgfx_pulse(tr::now);
	case Click::Pixels: return tr::lng_tgfx_pixels(tr::now);
	case Click::Rainbow: return tr::lng_tgfx_rainbow(tr::now);
	case Click::Lightning: return tr::lng_tgfx_lightning(tr::now);
	case Click::Count: break;
	}
	return QString();
}

int TypeEffectsCount() {
	return int(Type::Count);
}

QString TypeEffectName(int index) {
	switch (Type(index)) {
	case Type::None: return tr::lng_tgfx_none(tr::now);
	case Type::Sparks: return tr::lng_tgfx_sparks(tr::now);
	case Type::Hearts: return tr::lng_tgfx_hearts(tr::now);
	case Type::Stars: return tr::lng_tgfx_stars(tr::now);
	case Type::Confetti: return tr::lng_tgfx_confetti(tr::now);
	case Type::Letters: return tr::lng_tgfx_letters(tr::now);
	case Type::Bubbles: return tr::lng_tgfx_bubbles(tr::now);
	case Type::Embers: return tr::lng_tgfx_embers(tr::now);
	case Type::Snow: return tr::lng_tgfx_snow(tr::now);
	case Type::Pixels: return tr::lng_tgfx_pixels(tr::now);
	case Type::Rainbow: return tr::lng_tgfx_rainbow(tr::now);
	case Type::Flash: return tr::lng_tgfx_flash(tr::now);
	case Type::Petals: return tr::lng_tgfx_petals(tr::now);
	case Type::Count: break;
	}
	return QString();
}

void PaintClickEffectPreview(QPainter &p, QRectF rect, int index) {
	const auto kind = Click(std::clamp(index, 0, int(Click::Count) - 1));
	if (kind == Click::None) {
		PaintNone(p, rect);
		return;
	}
	const auto at = [&]() -> crl::time {
		switch (kind) {
		case Click::Ripple:
		case Click::Double:
		case Click::Pulse:
		case Click::Rainbow: return 300;
		case Click::Lightning: return 110;
		case Click::Firework: return 230;
		case Click::Hearts:
		case Click::Bubbles:
		case Click::Snow:
		case Click::Petals: return 420;
		default: return 260;
		}
	}();
	PaintPreview(p, rect, [&](Spawner &s) { SpawnClick(s, kind); }, at);
}

void PaintTypeEffectPreview(QPainter &p, QRectF rect, int index) {
	const auto kind = Type(std::clamp(index, 0, int(Type::Count) - 1));
	if (kind == Type::None) {
		PaintNone(p, rect);
		return;
	}
	// A caret with the effect coming from it.
	const auto caret = QRectF(
		rect.center().x() - 1,
		rect.center().y() - rect.height() * 0.18,
		2,
		rect.height() * 0.36);
	p.fillRect(caret, st::windowSubTextFg);
	const auto at = (kind == Type::Flash) ? crl::time(120) : crl::time(300);
	PaintPreview(p, rect, [&](Spawner &s) {
		for (auto i = 0; i != 3; ++i) {
			SpawnType(s, kind, u"Aa"_q.mid(i % 2, 1));
		}
	}, at);
}

void PlayClickEffect(QPoint global, int index) {
	const auto kind = Click(std::clamp(index, 0, int(Click::Count) - 1));
	if (kind == Click::None) {
		return;
	}
	WindowFor(global)->spawn(global, [&](Spawner &s) {
		SpawnClick(s, kind);
	});
}

} // namespace Tristegram
