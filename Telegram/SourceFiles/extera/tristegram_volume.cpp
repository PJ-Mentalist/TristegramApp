/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_volume.h"

#include <algorithm>
#include <atomic>

namespace Tristegram {
namespace {

std::atomic<int> Boost = 100;

} // namespace

void SetSongBoost(int percent) {
	Boost.store(std::clamp(percent, 100, 1000));
}

double SongBoost() {
	return Boost.load() / 100.;
}

} // namespace Tristegram
