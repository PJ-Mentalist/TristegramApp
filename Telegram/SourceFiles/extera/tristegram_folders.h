/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Creates Telegram folders "Private", "Groups", "Channels" and "Bots",
// shown as tabs above the chat list. Existing folders with the same
// title are kept as they are.
void CreateTypeTabs(not_null<Window::SessionController*> controller);

} // namespace Tristegram
