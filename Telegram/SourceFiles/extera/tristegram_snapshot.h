/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "data/data_types.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// A picture of the chosen messages on the chat background, like a
// quote card: copy it, save it or paste it into any chat.
void ShowChatSnapshot(
	not_null<Window::SessionController*> controller,
	MessageIdsList ids);

} // namespace Tristegram
