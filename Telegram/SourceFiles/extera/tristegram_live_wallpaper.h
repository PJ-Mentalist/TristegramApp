/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>

#include <vector>

class QPainter;

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Animated chat wallpapers drawn over the usual background: flying doves,
// stars, bubbles, a coding agent terminal, an aurora, or your own GIF/MP4.
// The scenes take their colors from the background gradient.
enum class LiveWallpaperKind {
	None,
	Doves,
	Stars,
	Bubbles,
	Code,
	Aurora,
	Video,
};

[[nodiscard]] bool LiveWallpaperEnabled();

// Widgets that paint the chat background, repainted while a scene plays.
void RegisterLiveWallpaperWidget(not_null<QWidget*> widget);

void PaintLiveWallpaper(
	QPainter &p,
	const std::vector<QColor> &colors,
	QSize fill,
	QRect clip);

void ChooseLiveWallpaperFile(not_null<Window::SessionController*> controller);

} // namespace Tristegram
