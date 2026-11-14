/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Tristegram {

// Live card with the logo, glow, gradient title and message bubbles.
// Repaints itself when any Tristegram look setting changes.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateLivePreview(QWidget *parent);

struct TileGridDescriptor {
	int count = 0;
	Fn<QString(int)> name;
	Fn<void(QPainter&, QRect, int)> paintSwatch; // swatch inside rect
	Fn<int()> selected;
	Fn<void(int)> chosen;
	QString configKey; // repaint on its changes
};

// Grid of rounded tiles with a swatch and a caption, like in the
// Tristegram prototype: hover lift, selection ring.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateTileGrid(
	QWidget *parent,
	TileGridDescriptor &&descriptor);

} // namespace Tristegram
