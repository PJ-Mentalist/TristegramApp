/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "base/object_ptr.h"

class HistoryItem;

namespace Ui {
class RpWidget;
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// The music window: a spinning vinyl with the current track, a
// SoundCloud-like wave to seek, "My music", recent tracks and playlists
// that can be created, renamed and deleted.
void ShowMusicWindow(not_null<Window::SessionController*> controller);

// A capsule for the chat header: a small spinning disc with the track,
// play / pause and next. A click opens the music window.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateNowPlayingPill(
	QWidget *parent,
	not_null<Window::SessionController*> controller);

// "Add to playlist" items for a message with a song.
void AddPlaylistMenuActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item);

// "Install the theme" and "Save the playlist" for messages with codes
// another Tristegram sent.
void AddShareImportActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	not_null<Window::SessionController*> controller);

} // namespace Tristegram
