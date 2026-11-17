/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <optional>

namespace rpl {
class lifetime;
} // namespace rpl

namespace Tristegram::Activity {

// What a Tristegram user does right now, shared through the Tristegram
// activity server: music, a video, code or a game.
struct Status {
	QString kind; // music, video, coding, game
	QString title;
	QString subtitle;
	QString app;
};

// Sends my own status every half a minute while sharing is on.
void Init(rpl::lifetime &lifetime);

// The last known status of a user, asks the server again when it is old.
[[nodiscard]] std::optional<Status> Lookup(uint64 userId);

// Fires the user id when a status arrives or changes.
[[nodiscard]] rpl::producer<uint64> Updates();

// "listens to Love Story - Taylor Swift" with an emoji in front.
[[nodiscard]] QString Describe(const Status &status);

} // namespace Tristegram::Activity
