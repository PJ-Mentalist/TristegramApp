/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Finds chats, groups, channels and bots that were quiet for a long time
// and archives or leaves the chosen ones in one go.
void ShowChatCleaner(not_null<Window::SessionController*> controller);

} // namespace Tristegram
