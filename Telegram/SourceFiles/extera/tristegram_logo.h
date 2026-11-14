/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>

class QPainter;

namespace Tristegram {

struct LogoStyle {
	QColor from;
	QColor to;
	bool glow = true;
};

// Number of logo color presets, see LogoPreset().
[[nodiscard]] int LogoPresetsCount();
[[nodiscard]] LogoStyle LogoPreset(int index);

// Style chosen in Tristegram settings.
[[nodiscard]] LogoStyle CurrentLogoStyle();
[[nodiscard]] bool LogoAnimationEnabled();

// Paints the vector Tristegram logo (circle, T, orbits and arrow) into
// the square `rect`. `progress` runs from 0 to 1 during the tap
// animation; 0 and 1 both give the logo at rest.
void PaintLogo(
	QPainter &p,
	const QRectF &rect,
	const LogoStyle &style,
	double progress = 0.);

} // namespace Tristegram
