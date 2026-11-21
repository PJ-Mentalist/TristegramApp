/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// A floating dock at the bottom of the window, like on a Mac: folders
// and pinned chats, the icons grow under the mouse. It slides up when
// the mouse comes to the bottom edge, "tristegram_dock".
void InstallDock(
	not_null<QWidget*> parent,
	not_null<Window::SessionController*> controller);

// Choose and order what is in the dock: chats, folders and buttons.
void ShowDockEditor(not_null<Window::SessionController*> controller);

// For "Add to dock" in the chat menu.
[[nodiscard]] bool IsPeerInDock(not_null<PeerData*> peer);
void TogglePeerInDock(not_null<PeerData*> peer);

} // namespace Tristegram
