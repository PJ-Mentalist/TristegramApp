/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Ready-made looks: Tristegram, Embers, Ocean, Sakura, Neon, Forest,
// Graphite. Each one is a built-in theme with its own accent color.
[[nodiscard]] std::vector<QString> ThemePresetNames();
void ApplyThemePreset(
	not_null<Window::SessionController*> controller,
	int index);

[[nodiscard]] QColor ThemePresetAccent(int index);
[[nodiscard]] bool ThemePresetNight(int index);

// Animated four-color gradient chat backgrounds.
[[nodiscard]] std::vector<QColor> BackgroundPresetColors(int index);
[[nodiscard]] std::vector<QString> BackgroundPresetNames();
void ApplyBackgroundPreset(
	not_null<Window::SessionController*> controller,
	int index);

// Gradient chat background from the user's own two to four colors.
void ApplyBackgroundColors(
	not_null<Window::SessionController*> controller,
	const std::vector<QColor> &colors);

// Box with a color editor; calls `done` with the chosen color.
void ChooseColor(
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	QColor current,
	Fn<void(QColor)> done);

} // namespace Tristegram
