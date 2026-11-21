/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Videos and round video messages can play in a small window over
// everything else.
[[nodiscard]] bool CanOpenInPip(not_null<HistoryItem*> item);
void OpenInPip(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

// The media viewer asks whether to start right in the small window.
[[nodiscard]] bool TakePipRequest();

} // namespace Tristegram
