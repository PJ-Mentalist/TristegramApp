/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// The Tristegram settings window: a sidebar with colored sections on the
// left and the chosen section with previews, capsules and tiles on the
// right. It replaces the main Telegram settings page.
void ShowSettingsWindow(not_null<Window::SessionController*> controller);

// Called when the main settings page is requested. Shows the Tristegram
// window instead and returns true, unless the classic page was asked for
// from inside the window.
[[nodiscard]] bool InterceptMainSettings(
	not_null<Window::SessionController*> controller);

} // namespace Tristegram
