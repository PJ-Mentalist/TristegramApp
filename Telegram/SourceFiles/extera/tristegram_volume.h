/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Tristegram {

// Music louder than 100%: 100 is the usual sound, 1000 is ten times.
// Read from the audio thread, so it is kept in an atomic.
void SetSongBoost(int percent);
[[nodiscard]] double SongBoost();

} // namespace Tristegram
