/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_sender_type.h"

#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

#include <QtGui/QPainterPath>

namespace Tristegram {
namespace {

[[nodiscard]] QColor TypeColor(SenderType type, QColor nameColor) {
	switch (type) {
	case SenderType::Bot: return QColor(0x3D, 0x9B, 0xFF);
	case SenderType::Channel: return QColor(0xF5, 0x9E, 0x2B);
	case SenderType::Group: return QColor(0x34, 0xC7, 0x59);
	case SenderType::Person:
	case SenderType::None: break;
	}
	return nameColor;
}

void PaintPerson(QPainter &p, QPointF c, double g, double shift) {
	const auto x = c.x() + shift;
	p.drawEllipse(QPointF(x, c.y() - g * 0.2), g * 0.19, g * 0.19);
	auto shoulders = QPainterPath();
	const auto body = QRectF(x - g * 0.34, c.y() + g * 0.08, g * 0.68, g * 0.7);
	shoulders.arcMoveTo(body, 180.);
	shoulders.arcTo(body, 180., -180.);
	p.drawPath(shoulders);
}

} // namespace

SenderType SenderTypeOf(PeerData *peer) {
	if (!peer || !Extera::Bool(u"tristegram_sender_icons"_q)) {
		return SenderType::None;
	}
	if (const auto user = peer->asUser()) {
		if (user->isBot()) {
			return SenderType::Bot;
		}
		return Extera::Bool(u"tristegram_sender_icons_people"_q)
			? SenderType::Person
			: SenderType::None;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast()
			? SenderType::Channel
			: SenderType::Group;
	} else if (peer->isChat()) {
		return SenderType::Group;
	}
	return SenderType::None;
}

int SenderTypeIconSize() {
	return st::msgNameFont->height;
}

QString SenderTypeTooltip(SenderType type) {
	switch (type) {
	case SenderType::Person: return tr::lng_tgst_person(tr::now);
	case SenderType::Bot: return tr::lng_tgst_bot(tr::now);
	case SenderType::Channel: return tr::lng_tgst_channel(tr::now);
	case SenderType::Group: return tr::lng_tgst_group(tr::now);
	case SenderType::None: break;
	}
	return QString();
}

void PaintSenderTypeIcon(
		QPainter &p,
		QRect rect,
		SenderType type,
		QColor nameColor) {
	if (type == SenderType::None || rect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto color = TypeColor(type, nameColor);
	const auto r = QRectF(rect);
	const auto size = std::min(r.width(), r.height());
	const auto square = QRectF(
		r.x() + (r.width() - size) / 2.,
		r.y() + (r.height() - size) / 2.,
		size,
		size);

	// A soft tinted tile under the glyph.
	auto tint = color;
	tint.setAlphaF(0.16);
	p.setPen(Qt::NoPen);
	p.setBrush(tint);
	p.drawRoundedRect(square, size * 0.3, size * 0.3);

	const auto c = square.center();
	const auto g = size * 0.66;
	auto pen = QPen(
		color,
		std::max(1.2, size * 0.085),
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);

	switch (type) {
	case SenderType::Person:
		PaintPerson(p, c, g, 0.);
		break;
	case SenderType::Group: {
		const auto small = g * 0.86;
		PaintPerson(p, QPointF(c.x(), c.y() + g * 0.04), small, -g * 0.2);
		PaintPerson(p, QPointF(c.x(), c.y() + g * 0.04), small, g * 0.2);
	} break;
	case SenderType::Bot: {
		const auto head = QRectF(
			c.x() - g * 0.4,
			c.y() - g * 0.14,
			g * 0.8,
			g * 0.56);
		p.drawRoundedRect(head, g * 0.16, g * 0.16);
		p.drawLine(
			QPointF(c.x(), head.y()),
			QPointF(c.x(), head.y() - g * 0.2));
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(
			QPointF(c.x(), head.y() - g * 0.27),
			g * 0.08,
			g * 0.08);
		const auto eyes = head.center().y();
		p.drawEllipse(QPointF(c.x() - g * 0.17, eyes), g * 0.075, g * 0.075);
		p.drawEllipse(QPointF(c.x() + g * 0.17, eyes), g * 0.075, g * 0.075);
	} break;
	case SenderType::Channel: {
		// A megaphone.
		auto horn = QPainterPath();
		horn.moveTo(c.x() - g * 0.4, c.y() - g * 0.13);
		horn.lineTo(c.x() - g * 0.06, c.y() - g * 0.13);
		horn.lineTo(c.x() + g * 0.38, c.y() - g * 0.4);
		horn.lineTo(c.x() + g * 0.38, c.y() + g * 0.4);
		horn.lineTo(c.x() - g * 0.06, c.y() + g * 0.13);
		horn.lineTo(c.x() - g * 0.4, c.y() + g * 0.13);
		horn.closeSubpath();
		auto fill = color;
		fill.setAlphaF(0.35);
		p.setBrush(fill);
		p.drawPath(horn);
		p.setBrush(Qt::NoBrush);
		p.drawLine(
			QPointF(c.x() - g * 0.24, c.y() + g * 0.13),
			QPointF(c.x() - g * 0.14, c.y() + g * 0.42));
	} break;
	case SenderType::None:
		break;
	}
}

} // namespace Tristegram
