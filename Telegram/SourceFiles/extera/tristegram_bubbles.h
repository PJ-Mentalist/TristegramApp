/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QPainter;
class QRect;
class QRectF;
class QColor;

namespace style {
struct QuoteStyle;
} // namespace style

namespace Tristegram {

// Reply quotes and link previews as cards with a gradient side bar.
[[nodiscard]] bool QuoteCardsEnabled();
void PaintQuoteCard(
	QPainter &p,
	const QRect &rect,
	const style::QuoteStyle &st,
	const QColor &tint);

// Reaction pills with a glass sheen; accent pills in the logo gradient.
[[nodiscard]] bool GlassReactionsEnabled();
void PaintGlassPill(QPainter &p, const QRect &fill, bool accent);

// Media viewer: control backgrounds as glass circles.
[[nodiscard]] bool GlassViewerEnabled();
void PaintGlassCircle(
	QPainter &p,
	const QRect &rect,
	const QColor &base,
	double sheenScale);

} // namespace Tristegram
