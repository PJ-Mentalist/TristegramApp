/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QImage>

#include <optional>

namespace Tristegram::SystemMedia {

// What another app plays now: Spotify, Yandex Music, SoundCloud or
// YouTube in a browser, anything that shows in the Windows media flyout.
struct Track {
	QString app;
	QString title;
	QString artist;
	QImage cover;
	crl::time position = 0;
	crl::time duration = 0;
	bool playing = false;
};

[[nodiscard]] bool Supported();

// Asks the system on a background thread, `done` is called on main.
void Request(Fn<void(std::optional<Track>)> done);

enum class Command {
	Toggle,
	Next,
	Previous,
};
void Send(Command command);

} // namespace Tristegram::SystemMedia
