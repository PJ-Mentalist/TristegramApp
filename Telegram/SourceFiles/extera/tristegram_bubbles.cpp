/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_bubbles.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ui/painter.h"
#include "styles/style_basic.h"

#include <QtGui/QPainterPath>

namespace Tristegram {

bool QuoteCardsEnabled() {
	return Extera::Bool(u"tristegram_reply_cards"_q);
}

void PaintQuoteCard(
		QPainter &p,
		const QRect &rect,
		const style::QuoteStyle &st,
		const QColor &tint) {
	if (rect.isEmpty() || !QuoteCardsEnabled()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = double(st.radius);
	const auto outline = std::max(st.outline, 1);
	const auto style = CurrentLogoStyle();

	p.save();
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(rect), radius, radius);
	p.setClipPath(clip);

	// Gradient side bar over the sender colored one.
	auto bar = QLinearGradient(
		QPointF(rect.x(), rect.y()),
		QPointF(rect.x(), rect.y() + rect.height()));
	bar.setColorAt(0., style.from);
	bar.setColorAt(1., style.to);
	p.fillRect(QRect(rect.x(), rect.y(), outline, rect.height()), bar);

	// A soft light sheen at the top makes it read as a raised card.
	auto sheen = QLinearGradient(
		QPointF(rect.x(), rect.y()),
		QPointF(rect.x(), rect.y() + rect.height()));
	sheen.setColorAt(0., QColor(255, 255, 255, 26));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
	p.fillRect(
		QRect(rect.x() + outline, rect.y(), rect.width() - outline, rect.height()),
		sheen);
	p.restore();

	// Thin border in the quote color.
	auto border = tint;
	border.setAlphaF(0.22);
	p.setPen(QPen(border, 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
		radius,
		radius);
}

bool GlassReactionsEnabled() {
	return Extera::Bool(u"tristegram_glass_reactions"_q);
}

void PaintGlassPill(QPainter &p, const QRect &fill, bool accent) {
	if (fill.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto rect = QRectF(fill);
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	if (accent) {
		const auto style = CurrentLogoStyle();
		auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
		gradient.setColorAt(0., style.from);
		gradient.setColorAt(1., style.to);
		p.setBrush(gradient);
		p.drawRoundedRect(rect, radius, radius);
	}
	auto sheen = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	sheen.setColorAt(0., QColor(255, 255, 255, accent ? 70 : 55));
	sheen.setColorAt(0.55, QColor(255, 255, 255, 8));
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.setBrush(sheen);
	p.drawRoundedRect(rect, radius, radius);

	auto edge = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	edge.setColorAt(0., QColor(255, 255, 255, 90));
	edge.setColorAt(1., QColor(255, 255, 255, 18));
	p.setPen(QPen(QBrush(edge), 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(
		rect.adjusted(0.5, 0.5, -0.5, -0.5),
		radius - 0.5,
		radius - 0.5);
	p.setPen(Qt::NoPen);
}

} // namespace Tristegram
