/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "extera/tristegram_activity.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Finds the song a friend listens to in Telegram music and plays it
// from the same moment.
void ListenTogether(
	not_null<Window::SessionController*> controller,
	const Activity::Status &status);

} // namespace Tristegram
