/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "ui/tristegram_accent.h"

#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtGui/QLinearGradient>

namespace Ui::TristegramAccent {
namespace {

Fn<Gradient()> GradientProvider;
Fn<bool(const QString &key)> FlagProvider;

} // namespace

void SetGradientProvider(Fn<Gradient()> provider) {
	GradientProvider = std::move(provider);
}

void SetFlagProvider(Fn<bool(const QString &key)> provider) {
	FlagProvider = std::move(provider);
}

Gradient Current() {
	if (GradientProvider) {
		auto result = GradientProvider();
		if (result.from.isValid() && result.to.isValid()) {
			return result;
		}
	}
	const auto color = st::windowBgActive->c;
	return { color, color };
}

bool Flag(const QString &key) {
	return FlagProvider ? FlagProvider(key) : false;
}

void FillGradient(QPainter &p, const QRectF &rect, double radius) {
	const auto colors = Current();
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., colors.from);
	gradient.setColorAt(1., colors.to);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	const auto r = (radius < 0.) ? (rect.height() / 2.) : radius;
	p.drawRoundedRect(rect, r, r);
}

} // namespace Ui::TristegramAccent
