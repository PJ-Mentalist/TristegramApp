/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Registers fonts the user added through Tristegram settings.
// Must run before the interface fonts are started.
void RegisterUserFonts();

// Asks for a TTF / OTF file, installs it for Tristegram and makes it
// the interface font after a restart.
void ChooseUserFont(not_null<Window::SessionController*> controller);

// Font families to offer as tiles: popular ones that are installed on
// this computer plus the fonts the user added.
[[nodiscard]] std::vector<QString> AvailableFontFamilies();

// The interface font family, empty for the default one.
[[nodiscard]] QString CurrentFontFamily();

// Makes `family` the interface font (empty for the default one) and
// offers a restart.
void ApplyFontFamily(
	not_null<Window::SessionController*> controller,
	const QString &family);

} // namespace Tristegram
