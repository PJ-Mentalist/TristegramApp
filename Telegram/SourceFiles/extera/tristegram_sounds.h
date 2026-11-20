/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Tristegram::Sounds {

// Short interface sounds, made in code, "tristegram_sounds":
// 0 none, 1 soft, 2 bubbles, 3 retro, 4 glass.
enum class Event {
	Send,
	Open,
};
void Play(Event event);

[[nodiscard]] std::vector<QString> PackNames();

} // namespace Tristegram::Sounds
