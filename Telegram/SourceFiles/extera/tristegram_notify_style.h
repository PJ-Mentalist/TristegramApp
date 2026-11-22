/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <crl/crl_time.h>

class QPainter;
class QPainterPath;

namespace Tristegram {

// Styles of the Tristegram notification windows (not the system ones):
// "tristegram_notify_style" 0 is the classic Telegram look.
[[nodiscard]] int NotifyStylesCount();
[[nodiscard]] QString NotifyStyleName(int style);
[[nodiscard]] int NotifyStyle();
[[nodiscard]] bool NotifyStyleAnimated(int style);

// "tristegram_notify_appear": 0 fade, 1 slide, 2 pop, 3 drop.
[[nodiscard]] int NotifyAppear();
[[nodiscard]] crl::time NotifyAppearDuration();

[[nodiscard]] QPainterPath NotifyShape(QRectF rect);

// The whole window background in the style.
void PaintNotifyBackground(
	QPainter &p,
	QRectF rect,
	int style,
	crl::time now,
	bool over = false);

// Moves or scales the painter while the window appears, `progress` 0..1.
void ApplyNotifyAppear(
	QPainter &p,
	QRectF rect,
	double progress,
	bool fromRight);

// A little notification for the settings tiles and the preview.
void PaintNotifyStylePreview(
	QPainter &p,
	QRectF rect,
	int style,
	crl::time now = 0);

} // namespace Tristegram
