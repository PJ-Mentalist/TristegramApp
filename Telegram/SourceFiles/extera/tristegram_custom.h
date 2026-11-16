/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Data {
class Thread;
} // namespace Data

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

// The user's own themes: the current look (base theme, accent, logo,
// bubbles, icons, names, background) saved under a name.
[[nodiscard]] std::vector<QString> CustomThemeNames();
[[nodiscard]] QColor CustomThemeAccent(int index);
[[nodiscard]] bool CustomThemeNight(int index);
[[nodiscard]] std::vector<QColor> CustomThemeColors(int index);
void SaveCustomTheme(const QString &name);
void ApplyCustomTheme(
	not_null<Window::SessionController*> controller,
	int index);
void RemoveCustomTheme(int index);

// A text code to send to a chat: another Tristegram installs the theme
// from the message context menu.
[[nodiscard]] QString CustomThemeCode(int index);
[[nodiscard]] bool IsCustomThemeCode(const QString &text);
bool ImportCustomTheme(
	not_null<Window::SessionController*> controller,
	const QString &text);

// The accent of the current day or night theme.
void ApplyAccentColor(
	not_null<Window::SessionController*> controller,
	QColor accent);
[[nodiscard]] QColor CurrentAccentColor();

// Picks a chat, sends the text there, then calls `after` for it.
void SendToChosenChat(
	not_null<Window::SessionController*> controller,
	const QString &text,
	Fn<void(not_null<Data::Thread*>)> after = nullptr);

// A box with one field.
void AskText(
	not_null<Window::SessionController*> controller,
	const QString &title,
	const QString &placeholder,
	const QString &initial,
	Fn<void(QString)> done);

// Box with a color editor; calls `done` with the chosen color.
void ChooseColor(
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	QColor current,
	Fn<void(QColor)> done);

} // namespace Tristegram
