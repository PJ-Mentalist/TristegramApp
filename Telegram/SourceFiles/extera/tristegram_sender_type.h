/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class PeerData;

namespace Tristegram {

// Who wrote a message in a group: a person, a bot, a channel or the
// group itself (an anonymous admin). A small icon after the name.
enum class SenderType {
	None,
	Person,
	Bot,
	Channel,
	Group,
};

[[nodiscard]] SenderType SenderTypeOf(PeerData *peer);
[[nodiscard]] int SenderTypeIconSize();
[[nodiscard]] QString SenderTypeTooltip(SenderType type);
void PaintSenderTypeIcon(
	QPainter &p,
	QRect rect,
	SenderType type,
	QColor nameColor);

} // namespace Tristegram
