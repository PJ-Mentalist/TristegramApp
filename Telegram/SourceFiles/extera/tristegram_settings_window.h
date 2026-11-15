/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "settings/settings_type.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// The Tristegram settings window: a sidebar with colored sections on the
// left and the chosen section with previews, capsules and tiles on the
// right. It replaces the main Telegram settings page.
void ShowSettingsWindow(not_null<Window::SessionController*> controller);

// Called when a settings page is requested. For the main page and the
// classic Chat Settings page shows the Tristegram window instead and
// returns true, unless the page was asked for from inside the window.
[[nodiscard]] bool InterceptSettings(
	not_null<Window::SessionController*> controller,
	const Settings::Type &type);

} // namespace Tristegram
