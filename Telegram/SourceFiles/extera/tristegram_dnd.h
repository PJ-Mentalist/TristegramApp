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

// "Do not disturb, except...": no notifications until a time, only the
// chosen people get through.
[[nodiscard]] bool DndActive();
[[nodiscard]] bool DndBlocks(not_null<PeerData*> peer);
[[nodiscard]] bool DndAllowed(not_null<PeerData*> peer);
void DndToggleAllowed(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<> DndChanges();

void ShowDndBox(not_null<Window::SessionController*> controller);

} // namespace Tristegram
