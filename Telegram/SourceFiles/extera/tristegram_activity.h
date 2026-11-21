/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QImage>

#include <optional>
#include <vector>

class QPainter;

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
	QString icon; // a small PNG of the game in base64
	QString steam; // the Steam app id of the game
	int64 position = 0; // where the song is, in ms
	int64 duration = 0;
	crl::time received = 0; // when the position was true
};

// Sends my own status every half a minute while sharing is on.
void Init(rpl::lifetime &lifetime);

// The last known status of a user, asks the server again when it is old.
[[nodiscard]] std::optional<Status> Lookup(uint64 userId);

// Fires the user id when a status arrives or changes.
[[nodiscard]] rpl::producer<uint64> Updates();

// "listens to Love Story - Taylor Swift".
[[nodiscard]] QString Describe(const Status &status);

// An animated icon for the kind of the activity, `now` moves it.
// With `icon` the picture of the game is drawn instead.
void PaintIcon(
	QPainter &p,
	QRectF rect,
	const QString &kind,
	crl::time now,
	const QString &icon = QString());

// The picture of a game from its icon in base64, cached.
[[nodiscard]] QImage IconImage(const QString &icon);

// The wide Steam picture of a game, loads it the first time.
[[nodiscard]] QImage SteamHeader(const QString &appId);
[[nodiscard]] rpl::producer<> ImagesUpdated();

// What is known about a user now, without asking the server.
[[nodiscard]] std::optional<Status> Cached(uint64 userId);

// Where the song of the status is now, in ms.
[[nodiscard]] int64 CurrentPosition(const Status &status);

// Games the user picked: the exe file name in lower case and the name
// to show, for games the app does not know.
[[nodiscard]] std::vector<std::pair<QString, QString>> CustomGames();
void AddCustomGame(const QString &exe, const QString &name);
void RemoveCustomGame(const QString &exe);

// Local statistics of my own activity: games, music, code, video.
struct StatEntry {
	QString key;
	QString kind;
	QString name;
	int64 seconds = 0;
	QString icon;
	QString steam;
};
enum class Period {
	Today,
	Week,
	All,
};
[[nodiscard]] std::vector<StatEntry> Stats(Period period);
// Seconds a day for the last `days` days, the oldest first.
[[nodiscard]] std::vector<int64> DailyTotals(int days);

// The owner of Tristegram may correct the total hours of an entry.
[[nodiscard]] bool IsOwner();
void SetTotalHours(const QString &key, double hours);

// What I do right now and since when.
struct Current {
	Status status;
	crl::time since = 0;
};
[[nodiscard]] std::optional<Current> Own();
[[nodiscard]] rpl::producer<> OwnChanges();

// Windows on the screen now, to pick a game from them.
struct RunningWindow {
	QString exe;
	QString title;
};
[[nodiscard]] std::vector<RunningWindow> RunningWindows();

} // namespace Tristegram::Activity
