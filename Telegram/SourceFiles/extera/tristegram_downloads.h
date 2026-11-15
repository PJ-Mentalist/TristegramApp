/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// The Tristegram downloads window: overall progress, every file that is
// being downloaded with its speed and time left, and recent downloads
// that can be opened or shown in their folder.
void ShowDownloadsWindow(not_null<Window::SessionController*> controller);

} // namespace Tristegram
