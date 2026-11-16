/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>
#include <QtGui/QImage>

class QPainter;

namespace Tristegram::MusicSkins {

// How the player in the music window looks, "tristegram_player_skin".
enum class Skin {
	Vinyl,
	Cassette,
	Raven,
	Drops,
	Ripples,
	Orbits,
	Cover,
};
inline constexpr auto kCount = 7;

[[nodiscard]] Skin Current();
void Choose(Skin skin);
[[nodiscard]] QString Name(Skin skin);

struct Args {
	QImage cover;
	QString title;
	QColor from;
	QColor to;
	double seconds = 0.; // time played, stands still on pause
	double progress = 0.; // of the track, 0..1
	bool playing = false;
	uint64 seed = 0;
};

// Paints every skin but the vinyl, which lives with its tone arm in the
// music window, into the square `rect`.
void Paint(QPainter &p, QRectF rect, Skin skin, const Args &args);

} // namespace Tristegram::MusicSkins
