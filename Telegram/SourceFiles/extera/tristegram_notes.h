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

// A private note for a chat, only on this computer: "owes 500",
// "birthday 12.03". It is shown next to the name in the chat header.
[[nodiscard]] QString ChatNote(not_null<PeerData*> peer);
void SetChatNote(not_null<PeerData*> peer, const QString &text);
[[nodiscard]] rpl::producer<> ChatNotesChanged();

void EditChatNote(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

// The yellow sticker with the note, painted after the name.
[[nodiscard]] int ChatNoteChipWidth(const QString &note, int maxWidth);
void PaintChatNoteChip(
	QPainter &p,
	QRect rect,
	const QString &note);

} // namespace Tristegram
