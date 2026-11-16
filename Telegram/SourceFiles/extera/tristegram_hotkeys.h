/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class History;
class HistoryItem;

namespace rpl {
class lifetime;
} // namespace rpl

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram::Hotkeys {

// Handles the app-wide Tristegram shortcuts: music, settings, downloads,
// night mode, hide mode, screen recording.
void Init(rpl::lifetime &lifetime);

// The newest loaded message of the chat: mine, someone else's or any.
enum class Whose {
	Any,
	Mine,
	Others,
};
[[nodiscard]] HistoryItem *LastMessage(
	not_null<History*> history,
	Whose whose);

// Deletes my newest message for everyone where it can be done.
bool DeleteLastOwn(
	not_null<Window::SessionController*> controller,
	not_null<History*> history);

// Copies the text of the newest message.
bool CopyLast(
	not_null<Window::SessionController*> controller,
	not_null<History*> history);

} // namespace Tristegram::Hotkeys
