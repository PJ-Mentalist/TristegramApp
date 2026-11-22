/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class DocumentData;

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// A title, an artist and a cover of a song, changed only on this
// computer. They are shown in the chat, in the player and in playlists.
struct SongEdit {
	QString title;
	QString performer;
	QString cover; // a picture file in tdata/tristegram_covers
};

[[nodiscard]] std::optional<SongEdit> SongEditFor(uint64 documentId);
[[nodiscard]] rpl::producer<> SongEditsChanged();

// The changed cover, loaded once and kept in memory.
[[nodiscard]] QImage SongEditCover(uint64 documentId);

// Saves a picture as a square cover in tdata, returns its path.
[[nodiscard]] QString SaveSquareCover(const QImage &original, int size);

// A box with the cover, the title and the artist of the song.
void EditSong(
	not_null<Window::SessionController*> controller,
	not_null<DocumentData*> document);

} // namespace Tristegram
