/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QPainter;

namespace Tristegram {

// Custom mouse cursors over all Tristegram windows: the arrow, the hand
// over buttons and the text beam are replaced by vector drawn ones.
// "tristegram_cursor_theme": 0 the system cursors, then the presets,
// then CursorCustomIndex() (own colors and shape) and CursorFileIndex()
// (own picture).
void InstallCustomCursor();

[[nodiscard]] int CursorThemesCount();
[[nodiscard]] int CursorCustomIndex();
[[nodiscard]] int CursorFileIndex();
[[nodiscard]] QString CursorThemeName(int index);
// `role`: 0 the arrow, 1 over buttons, 2 over text.
void PaintCursorThemePreview(
	QPainter &p,
	QRectF rect,
	int index,
	int role = 0);

[[nodiscard]] int CursorShapesCount();
[[nodiscard]] QString CursorShapeName(int shape);
void PaintCursorShapePreview(QPainter &p, QRectF rect, int shape);

// The picture of the "own picture" cursor, it is copied into tdata.
// The hot spot is given in parts of the picture size, from 0 to 1.
bool SetCursorPicture(const QImage &image);
void SetCursorPictureHotSpot(QPointF part);
[[nodiscard]] QImage CursorPicture();
[[nodiscard]] QPointF CursorPictureHotSpot();

} // namespace Tristegram
