/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>
#include <crl/crl_time.h>

class QPainter;

namespace Tristegram {

struct LogoStyle {
	QColor from;
	QColor to;
	bool glow = true;
	double angle = 135.; // gradient direction, degrees
	double glowPower = 0.6; // 0..1
};

// Index of the "custom colors" choice, after the presets.
[[nodiscard]] int LogoCustomIndex();

// Number of logo color presets, see LogoPreset().
[[nodiscard]] int LogoPresetsCount();
[[nodiscard]] LogoStyle LogoPreset(int index);

// Style chosen in Tristegram settings. `now` drives the optional
// gradient rotation and glow pulse.
[[nodiscard]] LogoStyle CurrentLogoStyle(crl::time now = 0);
[[nodiscard]] bool LogoAnimationEnabled();
[[nodiscard]] bool LogoAmbientAnimated(); // pulse or gradient rotation

// Paints the vector Tristegram logo (circle, T, orbits and arrow) into
// the square `rect`. `progress` runs from 0 to 1 during the tap
// animation; 0 and 1 both give the logo at rest.
void PaintLogo(
	QPainter &p,
	const QRectF &rect,
	const LogoStyle &style,
	double progress = 0.);

} // namespace Tristegram
