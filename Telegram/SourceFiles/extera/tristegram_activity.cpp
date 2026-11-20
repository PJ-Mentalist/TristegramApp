/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_activity.h"

#include "base/random.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_document.h"
#include "extera/extera_config.h"
#include "extera/tristegram_system_media.h"
#include "extera/tristegram_logo.h"
#include "ui/painter.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"

#include <QtCore/QDate>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif // Q_OS_WIN

namespace Tristegram::Activity {
namespace {

constexpr auto kServer = "https://tristegram-activity.vercel.app/api/activity";
constexpr auto kReportPeriod = crl::time(30'000);
constexpr auto kKeepAlive = crl::time(90'000);
constexpr auto kLookupFresh = crl::time(30'000);

struct Known {
	std::optional<Status> status;
	crl::time requested = 0;
};

struct State {
	QNetworkAccessManager *manager = nullptr;
	base::flat_map<uint64, Known> known;
	rpl::event_stream<uint64> updates;
	base::Timer timer;
	QString lastSent;
	crl::time lastSentAt = 0;
	bool reporting = false;

	// Local statistics of my own activity.
	std::optional<Status> own;
	crl::time ownSince = 0;
	crl::time trackedAt = 0;
	crl::time savedAt = 0;
	bool statsLoaded = false;
	QJsonObject stats;
	rpl::event_stream<> ownChanges;
};

[[nodiscard]] State &Get() {
	static const auto result = new State();
	return *result;
}

[[nodiscard]] QNetworkAccessManager *Manager() {
	auto &state = Get();
	if (!state.manager) {
		state.manager = new QNetworkAccessManager(qApp);
	}
	return state.manager;
}

[[nodiscard]] QString Key() {
	auto key = Extera::String(u"tristegram_activity_key"_q);
	if (key.size() < 16) {
		key = QString::number(base::RandomValue<uint64>(), 16)
			+ QString::number(base::RandomValue<uint64>(), 16);
		Extera::Config::Instance().setString(
			u"tristegram_activity_key"_q,
			key);
	}
	return key;
}

[[nodiscard]] uint64 MyId() {
	const auto &domain = Core::App().domain();
	if (!domain.started()) {
		return 0;
	}
	const auto &account = domain.active();
	return account.sessionExists()
		? account.session().userId().bare
		: 0;
}

struct Foreground {
	QString exe;
	QString path;
	QString title;
};

#ifdef Q_OS_WIN
[[nodiscard]] QString ProcessPath(DWORD pid) {
	auto result = QString();
	const auto process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		pid);
	if (process) {
		wchar_t path[MAX_PATH * 2] = { 0 };
		auto size = DWORD(MAX_PATH * 2);
		if (QueryFullProcessImageNameW(process, 0, path, &size)) {
			result = QString::fromWCharArray(path, int(size));
		}
		CloseHandle(process);
	}
	return result;
}
#endif // Q_OS_WIN

[[nodiscard]] Foreground ForegroundApp() {
#ifdef Q_OS_WIN
	const auto window = GetForegroundWindow();
	if (!window) {
		return {};
	}
	auto pid = DWORD(0);
	GetWindowThreadProcessId(window, &pid);
	if (!pid) {
		return {};
	}
	auto result = Foreground();
	result.path = QDir::fromNativeSeparators(ProcessPath(pid));
	result.exe = QFileInfo(result.path).fileName().toLower();
	wchar_t title[512] = { 0 };
	const auto length = GetWindowTextW(window, title, 511);
	result.title = QString::fromWCharArray(title, std::max(length, 0));
	return result;
#else // Q_OS_WIN
	return {};
#endif // Q_OS_WIN
}

[[nodiscard]] QString CodingApp(const QString &exe) {
	static const auto apps = base::flat_map<QString, QString>{
		{ u"code.exe"_q, u"Visual Studio Code"_q },
		{ u"cursor.exe"_q, u"Cursor"_q },
		{ u"windsurf.exe"_q, u"Windsurf"_q },
		{ u"devenv.exe"_q, u"Visual Studio"_q },
		{ u"idea64.exe"_q, u"IntelliJ IDEA"_q },
		{ u"pycharm64.exe"_q, u"PyCharm"_q },
		{ u"webstorm64.exe"_q, u"WebStorm"_q },
		{ u"clion64.exe"_q, u"CLion"_q },
		{ u"rider64.exe"_q, u"Rider"_q },
		{ u"goland64.exe"_q, u"GoLand"_q },
		{ u"studio64.exe"_q, u"Android Studio"_q },
		{ u"sublime_text.exe"_q, u"Sublime Text"_q },
		{ u"notepad++.exe"_q, u"Notepad++"_q },
		{ u"zed.exe"_q, u"Zed"_q },
		{ u"robloxstudiobeta.exe"_q, u"Roblox Studio"_q },
		{ u"unity.exe"_q, u"Unity"_q },
		{ u"godot.exe"_q, u"Godot"_q },
	};
	const auto i = apps.find(exe);
	return (i != end(apps)) ? i->second : QString();
}

[[nodiscard]] QString GameApp(const QString &exe) {
	static const auto games = base::flat_map<QString, QString>{
		{ u"cs2.exe"_q, u"Counter-Strike 2"_q },
		{ u"dota2.exe"_q, u"Dota 2"_q },
		{ u"robloxplayerbeta.exe"_q, u"Roblox"_q },
		{ u"minecraft.windows.exe"_q, u"Minecraft"_q },
		{ u"javaw.exe"_q, u"Minecraft"_q },
		{ u"gta5.exe"_q, u"GTA V"_q },
		{ u"gta5_enhanced.exe"_q, u"GTA V"_q },
		{ u"valorant-win64-shipping.exe"_q, u"Valorant"_q },
		{ u"league of legends.exe"_q, u"League of Legends"_q },
		{ u"fortniteclient-win64-shipping.exe"_q, u"Fortnite"_q },
		{ u"r5apex.exe"_q, u"Apex Legends"_q },
		{ u"overwatch.exe"_q, u"Overwatch 2"_q },
		{ u"genshinimpact.exe"_q, u"Genshin Impact"_q },
		{ u"starrail.exe"_q, u"Honkai: Star Rail"_q },
		{ u"zenlesszonezero.exe"_q, u"Zenless Zone Zero"_q },
		{ u"osu!.exe"_q, u"osu!"_q },
		{ u"terraria.exe"_q, u"Terraria"_q },
		{ u"rocketleague.exe"_q, u"Rocket League"_q },
		{ u"eldenring.exe"_q, u"Elden Ring"_q },
		{ u"cyberpunk2077.exe"_q, u"Cyberpunk 2077"_q },
		{ u"rustclient.exe"_q, u"Rust"_q },
		{ u"pubg.exe"_q, u"PUBG"_q },
		{ u"tslgame.exe"_q, u"PUBG"_q },
		{ u"brawlhalla.exe"_q, u"Brawlhalla"_q },
		{ u"deadbydaylight-win64-shipping.exe"_q, u"Dead by Daylight"_q },
	};
	const auto i = games.find(exe);
	return (i != end(games)) ? i->second : QString();
}

// "file.cpp - project - Visual Studio Code" gives "project".
[[nodiscard]] QString ProjectFromTitle(const QString &title) {
	const auto parts = title.split(u" - "_q, Qt::SkipEmptyParts);
	return (parts.size() >= 3) ? parts[parts.size() - 2].trimmed() : QString();
}

[[nodiscard]] base::flat_map<QString, QString> LoadCustomGames() {
	auto result = base::flat_map<QString, QString>();
	const auto object = QJsonDocument::fromJson(
		Extera::String(u"tristegram_activity_games"_q).toUtf8()).object();
	for (auto i = object.begin(); i != object.end(); ++i) {
		result.emplace(i.key().toLower(), i.value().toString());
	}
	return result;
}

void SaveCustomGames(const base::flat_map<QString, QString> &games) {
	auto object = QJsonObject();
	for (const auto &[exe, name] : games) {
		object.insert(exe, name);
	}
	Extera::Config::Instance().setString(
		u"tristegram_activity_games"_q,
		QString::fromUtf8(
			QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

struct Game {
	QString name;
	bool novel = false;
};

// The folder right after a store's games folder is the game name:
// steamapps/common/<Game>, Epic Games/<Game>, GOG Games/<Game> and so on.
[[nodiscard]] QString StoreGameName(const QString &path) {
	const auto lower = path.toLower();
	for (const auto marker : {
			"/steamapps/common/",
			"/epic games/",
			"/gog games/",
			"/gog galaxy/games/",
			"/itch/apps/",
			"/xboxgames/",
			"/riot games/",
			"/ubisoft game launcher/games/",
			"/ea games/",
			"/battle.net/" }) {
		const auto at = lower.indexOf(QString::fromLatin1(marker));
		if (at < 0) {
			continue;
		}
		const auto start = at + int(qstrlen(marker));
		const auto end = path.indexOf('/', start);
		const auto name = path.mid(start, (end > start) ? (end - start) : -1);
		if (!name.isEmpty()
			&& !name.contains(u"steamworks"_q, Qt::CaseInsensitive)
			&& !name.contains(u"redist"_q, Qt::CaseInsensitive)) {
			return name;
		}
	}
	return QString();
}

// Ren'Py visual novels have a "renpy" folder next to the exe.
[[nodiscard]] bool IsRenPy(const QString &path) {
	const auto folder = QFileInfo(path).absolutePath();
	return QDir(folder + u"/renpy"_q).exists()
		|| (QDir(folder + u"/game"_q).exists()
			&& QFileInfo::exists(folder + u"/lib"_q));
}

[[nodiscard]] std::optional<Game> DetectGame(
		const QString &exe,
		const QString &path,
		const QString &title) {
	if (exe.isEmpty()) {
		return std::nullopt;
	}
	const auto custom = LoadCustomGames();
	if (const auto i = custom.find(exe); i != end(custom)) {
		return Game{ .name = i->second };
	}
	if (const auto known = GameApp(exe); !known.isEmpty()) {
		return Game{ .name = known };
	}
	if (exe.contains(u"crashhandler"_q)
		|| exe.contains(u"crashreport"_q)
		|| exe.contains(u"launcher"_q)
		|| exe.contains(u"unins"_q)) {
		return std::nullopt;
	}
	if (!path.isEmpty() && IsRenPy(path)) {
		const auto name = title.trimmed().isEmpty()
			? QFileInfo(path).completeBaseName()
			: title.trimmed();
		return Game{ .name = name, .novel = true };
	}
	if (const auto store = StoreGameName(path); !store.isEmpty()) {
		return Game{ .name = store };
	}
	return std::nullopt;
}

// A game running in the background, minimized or on another screen.
[[nodiscard]] std::optional<Game> BackgroundGame() {
#ifdef Q_OS_WIN
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return std::nullopt;
	}
	auto entry = PROCESSENTRY32W();
	entry.dwSize = sizeof(entry);
	auto result = std::optional<Game>();
	const auto custom = LoadCustomGames();
	if (Process32FirstW(snapshot, &entry)) {
		do {
			const auto exe = QString::fromWCharArray(entry.szExeFile).toLower();
			if (custom.contains(exe) || !GameApp(exe).isEmpty()) {
				result = DetectGame(exe, QString(), QString());
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	// Looking into every process path is the expensive part, do it
	// rarely and remember what was found.
	static auto storeCheckedAt = crl::time(0);
	static auto storeGame = std::optional<Game>();
	const auto now = crl::now();
	if (!result && storeCheckedAt && now - storeCheckedAt < 120'000) {
		CloseHandle(snapshot);
		return storeGame;
	}
	if (!result) {
		storeCheckedAt = now;
		storeGame = std::nullopt;
	}
	if (!result && Process32FirstW(snapshot, &entry)) {
		do {
			const auto exe = QString::fromWCharArray(entry.szExeFile).toLower();
			if (exe.endsWith(u".exe"_q) && entry.th32ProcessID > 4) {
				const auto path = QDir::fromNativeSeparators(
					ProcessPath(entry.th32ProcessID));
				if (!StoreGameName(path).isEmpty()) {
					result = DetectGame(exe, path, QString());
					if (result) {
						storeGame = result;
						break;
					}
				}
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return result;
#else // Q_OS_WIN
	return std::nullopt;
#endif // Q_OS_WIN
}

[[nodiscard]] Status GameStatus(const Game &game) {
	return Status{
		.kind = u"game"_q,
		.title = game.name,
		.subtitle = game.novel ? u"novel"_q : QString(),
		.app = game.name,
	};
}

[[nodiscard]] std::optional<Status> TristegramMusic() {
	const auto instance = Media::Player::instance();
	const auto state = instance->getState(AudioMsgId::Type::Song);
	if (!state.id
		|| Media::Player::IsStoppedOrStopping(state.state)
		|| Media::Player::IsPausedOrPausing(state.state)) {
		return std::nullopt;
	}
	const auto document = state.id.audio();
	if (!document) {
		return std::nullopt;
	}
	auto result = Status{ .kind = u"music"_q, .app = u"Tristegram"_q };
	if (const auto song = document->song()) {
		result.title = song->title;
		result.subtitle = song->performer;
	}
	if (result.title.isEmpty()) {
		result.title = QFileInfo(document->filename()).completeBaseName();
	}
	return result;
}

void Send(const Status &status) {
	const auto id = MyId();
	if (!id) {
		return;
	}
	auto object = QJsonObject();
	object.insert(u"id"_q, QString::number(id));
	object.insert(u"key"_q, Key());
	object.insert(u"kind"_q, status.kind);
	object.insert(u"title"_q, status.title);
	object.insert(u"subtitle"_q, status.subtitle);
	object.insert(u"app"_q, status.app);
	const auto body = QJsonDocument(object).toJson(QJsonDocument::Compact);
	auto &state = Get();
	const auto now = crl::now();
	if (state.lastSent == QString::fromUtf8(body)
		&& now - state.lastSentAt < kKeepAlive) {
		return;
	}
	state.lastSent = QString::fromUtf8(body);
	state.lastSentAt = now;
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kServer)));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	const auto reply = Manager()->post(request, body);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
	});
}

[[nodiscard]] QString TodayKey() {
	return QDate::currentDate().toString(Qt::ISODate);
}

[[nodiscard]] QString StatKey(const Status &status) {
	const auto name = (status.kind == u"game"_q)
		? status.title
		: status.app;
	return status.kind + ':' + name.toLower();
}

[[nodiscard]] QString StatName(const Status &status) {
	return (status.kind == u"game"_q) ? status.title : status.app;
}

void LoadStats() {
	auto &state = Get();
	if (state.statsLoaded) {
		return;
	}
	state.statsLoaded = true;
	state.stats = QJsonDocument::fromJson(
		Extera::String(u"tristegram_activity_stats"_q).toUtf8()).object();
}

void SaveStats() {
	auto &state = Get();
	state.savedAt = crl::now();
	// Days older than two months are not needed for the charts.
	const auto oldest = QDate::currentDate().addDays(-62);
	for (auto i = state.stats.begin(); i != state.stats.end(); ++i) {
		auto entry = i.value().toObject();
		auto days = entry.value(u"d"_q).toObject();
		auto changed = false;
		for (const auto &day : days.keys()) {
			if (QDate::fromString(day, Qt::ISODate) < oldest) {
				days.remove(day);
				changed = true;
			}
		}
		if (changed) {
			entry.insert(u"d"_q, days);
			i.value() = entry;
		}
	}
	Extera::Config::Instance().setString(
		u"tristegram_activity_stats"_q,
		QString::fromUtf8(
			QJsonDocument(state.stats).toJson(QJsonDocument::Compact)));
}

// Counts the time since the previous report for what I do now.
void Track(const std::optional<Status> &status) {
	auto &state = Get();
	LoadStats();
	const auto now = crl::now();
	const auto same = (state.own.has_value() == status.has_value())
		&& (!status || StatKey(*state.own) == StatKey(*status));
	if (state.own && state.trackedAt) {
		const auto seconds = std::clamp(
			(now - state.trackedAt) / 1000,
			crl::time(0),
			crl::time(90));
		if (seconds > 0) {
			const auto key = StatKey(*state.own);
			auto entry = state.stats.value(key).toObject();
			entry.insert(u"k"_q, state.own->kind);
			entry.insert(u"n"_q, StatName(*state.own));
			entry.insert(
				u"t"_q,
				entry.value(u"t"_q).toDouble() + double(seconds));
			auto days = entry.value(u"d"_q).toObject();
			const auto today = TodayKey();
			days.insert(today, days.value(today).toDouble() + double(seconds));
			entry.insert(u"d"_q, days);
			state.stats.insert(key, entry);
		}
	}
	state.trackedAt = status ? now : 0;
	if (!same) {
		state.own = status;
		state.ownSince = status ? now : 0;
		SaveStats();
		state.ownChanges.fire({});
	} else if (status) {
		state.own = status;
		if (now - state.savedAt > 120'000) {
			SaveStats();
		}
	}
}

void Publish(const Status &status) {
	Track(status.kind.isEmpty()
		? std::optional<Status>()
		: std::optional<Status>(status));
	auto &state = Get();
	if (Extera::Bool(u"tristegram_activity_share"_q)) {
		Send(status);
	} else if (!state.lastSent.isEmpty()) {
		state.lastSent = QString();
		Send(Status());
	}
}

void Report() {
	auto &state = Get();
	if (state.reporting) {
		return;
	}
	if (auto music = TristegramMusic()) {
		Publish(*music);
		return;
	}
	const auto foreground = ForegroundApp();
	if (const auto game = DetectGame(
			foreground.exe,
			foreground.path,
			foreground.title)) {
		Publish(GameStatus(*game));
		return;
	}
	if (const auto coding = CodingApp(foreground.exe); !coding.isEmpty()) {
		Publish({
			.kind = u"coding"_q,
			.title = ProjectFromTitle(foreground.title),
			.app = coding,
		});
		return;
	}
	if (const auto game = BackgroundGame()) {
		Publish(GameStatus(*game));
		return;
	}
	state.reporting = true;
	SystemMedia::Request([](std::optional<SystemMedia::Track> track) {
		Get().reporting = false;
		if (track && track->playing && !track->title.isEmpty()) {
			Publish({
				.kind = track->artist.isEmpty() ? u"video"_q : u"music"_q,
				.title = track->title,
				.subtitle = track->artist,
				.app = track->app,
			});
		} else {
			Publish(Status());
		}
	});
}

void RequestStatus(uint64 userId) {
	auto &known = Get().known[userId];
	known.requested = crl::now();
	auto url = QUrl(QString::fromLatin1(kServer));
	auto query = QUrlQuery();
	query.addQueryItem(u"ids"_q, QString::number(userId));
	url.setQuery(query);
	const auto reply = Manager()->get(QNetworkRequest(url));
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			return;
		}
		const auto object = QJsonDocument::fromJson(
			reply->readAll()).object();
		const auto value = object.value(QString::number(userId)).toObject();
		auto status = std::optional<Status>();
		if (!value.isEmpty()) {
			status = Status{
				.kind = value.value(u"kind"_q).toString(),
				.title = value.value(u"title"_q).toString(),
				.subtitle = value.value(u"subtitle"_q).toString(),
				.app = value.value(u"app"_q).toString(),
			};
		}
		auto &entry = Get().known[userId];
		const auto changed = (entry.status.has_value() != status.has_value())
			|| (status
				&& (entry.status->kind != status->kind
					|| entry.status->title != status->title
					|| entry.status->subtitle != status->subtitle
					|| entry.status->app != status->app));
		entry.status = status;
		if (changed) {
			Get().updates.fire_copy(userId);
		}
	});
}

} // namespace

void Init(rpl::lifetime &lifetime) {
	auto &state = Get();
	state.timer.setCallback([] { Report(); });
	state.timer.callEach(kReportPeriod);
	crl::on_main([] { Report(); });
	Extera::Config::Instance().boolChanges(
		u"tristegram_activity_share"_q
	) | rpl::on_next([](bool) {
		Report();
	}, lifetime);
	Media::Player::instance()->trackChanged(
	) | rpl::on_next([](AudioMsgId::Type type) {
		if (type == AudioMsgId::Type::Song) {
			crl::on_main([] { Report(); });
		}
	}, lifetime);
}

std::optional<Status> Lookup(uint64 userId) {
	if (!userId || !Extera::Bool(u"tristegram_activity_show"_q)) {
		return std::nullopt;
	}
	auto &state = Get();
	const auto i = state.known.find(userId);
	const auto now = crl::now();
	if (i == end(state.known) || now - i->second.requested > kLookupFresh) {
		RequestStatus(userId);
	}
	const auto j = state.known.find(userId);
	return (j != end(state.known)) ? j->second.status : std::nullopt;
}

rpl::producer<uint64> Updates() {
	return Get().updates.events();
}

QString Describe(const Status &status) {
	const auto both = status.subtitle.isEmpty()
		? status.title
		: (status.title + u" \u2014 "_q + status.subtitle);
	if (status.kind == u"music"_q) {
		return tr::lng_tga_music(tr::now, lt_name, both);
	} else if (status.kind == u"video"_q) {
		return tr::lng_tga_video(tr::now, lt_name, both);
	} else if (status.kind == u"coding"_q) {
		return status.title.isEmpty()
			? tr::lng_tga_coding_app(tr::now, lt_name, status.app)
			: tr::lng_tga_coding(
				tr::now,
				lt_name,
				status.title,
				lt_app,
				status.app);
	} else if (status.kind == u"game"_q) {
		return (status.subtitle == u"novel"_q)
			? tr::lng_tga_novel(tr::now, lt_name, status.title)
			: tr::lng_tga_game(tr::now, lt_name, status.title);
	}
	return QString();
}

void PaintIcon(
		QPainter &p,
		QRectF rect,
		const QString &kind,
		crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto style = CurrentLogoStyle();
	auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
	fill.setColorAt(0., style.from);
	fill.setColorAt(1., style.to);
	const auto t = now / 1000.;
	const auto c = rect.center();
	const auto s = rect.width() / 2.;
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	if (kind == u"music"_q) {
		// Four bars jumping to the beat.
		const auto w = rect.width() / 7.;
		for (auto i = 0; i != 4; ++i) {
			const auto phase = 0.5 + 0.5 * std::sin(t * (7. + i * 1.7) + i * 1.3);
			const auto h = rect.height() * (0.25 + 0.75 * phase);
			p.drawRoundedRect(
				QRectF(rect.x() + w * (0.5 + i * 1.7), rect.bottom() - h, w, h),
				w / 2.,
				w / 2.);
		}
	} else if (kind == u"video"_q) {
		// A screen with a pulsing play sign.
		const auto screen = rect.marginsRemoved(QMarginsF(0, s * 0.25, 0, s * 0.25));
		p.drawRoundedRect(screen, s * 0.25, s * 0.25);
		const auto pulse = 0.85 + 0.15 * std::sin(t * 5.);
		auto play = QPainterPath();
		play.moveTo(c.x() - s * 0.25 * pulse, c.y() - s * 0.35 * pulse);
		play.lineTo(c.x() + s * 0.38 * pulse, c.y());
		play.lineTo(c.x() - s * 0.25 * pulse, c.y() + s * 0.35 * pulse);
		play.closeSubpath();
		p.setBrush(QColor(255, 255, 255));
		p.drawPath(play);
	} else if (kind == u"coding"_q) {
		// Angle brackets with a blinking cursor between them.
		auto pen = QPen(QBrush(fill), std::max(1.5, s * 0.22));
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawPolyline(QPolygonF({
			QPointF(c.x() - s * 0.45, c.y() - s * 0.5),
			QPointF(c.x() - s * 0.95, c.y()),
			QPointF(c.x() - s * 0.45, c.y() + s * 0.5) }));
		p.drawPolyline(QPolygonF({
			QPointF(c.x() + s * 0.45, c.y() - s * 0.5),
			QPointF(c.x() + s * 0.95, c.y()),
			QPointF(c.x() + s * 0.45, c.y() + s * 0.5) }));
		if (std::fmod(t, 1.) < 0.55) {
			p.drawLine(
				QPointF(c.x(), c.y() - s * 0.55),
				QPointF(c.x(), c.y() + s * 0.55));
		}
	} else if (kind == u"game"_q) {
		// A gamepad rocking a little, its buttons light up in turn.
		p.translate(c);
		p.rotate(8. * std::sin(t * 3.));
		auto pad = QPainterPath();
		pad.addRoundedRect(QRectF(-s, -s * 0.5, s * 2, s * 1.1), s * 0.5, s * 0.5);
		p.drawPath(pad);
		p.setBrush(QColor(255, 255, 255));
		const auto dot = s * 0.14;
		p.drawRect(QRectF(-s * 0.62, -dot / 2., dot * 3, dot));
		p.drawRect(QRectF(-s * 0.62 + dot, -dot * 1.5, dot, dot * 3));
		for (auto i = 0; i != 2; ++i) {
			const auto on = (int(t * 3.) % 2) == i;
			p.setBrush(QColor(255, 255, 255, on ? 255 : 120));
			p.drawEllipse(QPointF(s * (0.35 + i * 0.3), -s * 0.08 + i * s * 0.2), dot, dot);
		}
	}
	p.restore();
}

std::vector<StatEntry> Stats(Period period) {
	LoadStats();
	const auto &stats = Get().stats;
	const auto today = QDate::currentDate();
	auto result = std::vector<StatEntry>();
	for (auto i = stats.begin(); i != stats.end(); ++i) {
		const auto entry = i.value().toObject();
		auto seconds = int64(0);
		if (period == Period::All) {
			seconds = int64(entry.value(u"t"_q).toDouble());
		} else {
			const auto days = entry.value(u"d"_q).toObject();
			const auto count = (period == Period::Today) ? 1 : 7;
			for (auto d = 0; d != count; ++d) {
				seconds += int64(days.value(
					today.addDays(-d).toString(Qt::ISODate)).toDouble());
			}
		}
		if (seconds > 0) {
			result.push_back({
				.key = i.key(),
				.kind = entry.value(u"k"_q).toString(),
				.name = entry.value(u"n"_q).toString(),
				.seconds = seconds,
			});
		}
	}
	ranges::sort(result, ranges::greater(), &StatEntry::seconds);
	return result;
}

std::vector<int64> DailyTotals(int days) {
	LoadStats();
	const auto &stats = Get().stats;
	const auto today = QDate::currentDate();
	auto result = std::vector<int64>(std::max(days, 0), 0);
	for (auto i = stats.begin(); i != stats.end(); ++i) {
		const auto list = i.value().toObject().value(u"d"_q).toObject();
		for (auto d = 0; d != days; ++d) {
			result[days - 1 - d] += int64(list.value(
				today.addDays(-d).toString(Qt::ISODate)).toDouble());
		}
	}
	return result;
}

bool IsOwner() {
	return (MyId() == 6545694088ULL);
}

void SetTotalHours(const QString &key, double hours) {
	if (!IsOwner()) {
		return;
	}
	LoadStats();
	auto &state = Get();
	auto entry = state.stats.value(key).toObject();
	if (entry.isEmpty()) {
		return;
	}
	entry.insert(u"t"_q, std::max(hours, 0.) * 3600.);
	state.stats.insert(key, entry);
	SaveStats();
	state.ownChanges.fire({});
}

std::optional<Current> Own() {
	const auto &state = Get();
	if (!state.own) {
		return std::nullopt;
	}
	return Current{ .status = *state.own, .since = state.ownSince };
}

rpl::producer<> OwnChanges() {
	return Get().ownChanges.events();
}

std::vector<std::pair<QString, QString>> CustomGames() {
	auto result = std::vector<std::pair<QString, QString>>();
	for (const auto &[exe, name] : LoadCustomGames()) {
		result.emplace_back(exe, name);
	}
	return result;
}

void AddCustomGame(const QString &exe, const QString &name) {
	auto games = LoadCustomGames();
	games[exe.toLower()] = name;
	SaveCustomGames(games);
	crl::on_main([] { Report(); });
}

void RemoveCustomGame(const QString &exe) {
	auto games = LoadCustomGames();
	games.remove(exe.toLower());
	SaveCustomGames(games);
}

std::vector<RunningWindow> RunningWindows() {
	auto result = std::vector<RunningWindow>();
#ifdef Q_OS_WIN
	EnumWindows([](HWND window, LPARAM data) -> BOOL {
		const auto list = reinterpret_cast<std::vector<RunningWindow>*>(data);
		if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
			return TRUE;
		}
		wchar_t title[512] = { 0 };
		const auto length = GetWindowTextW(window, title, 511);
		if (length <= 0) {
			return TRUE;
		}
		auto pid = DWORD(0);
		GetWindowThreadProcessId(window, &pid);
		const auto exe = QFileInfo(ProcessPath(pid)).fileName().toLower();
		if (exe.isEmpty()
			|| exe == u"explorer.exe"_q
			|| exe == u"tristegram.exe"_q
			|| exe == u"telegram.exe"_q
			|| exe == u"textinputhost.exe"_q
			|| exe == u"applicationframehost.exe"_q) {
			return TRUE;
		}
		const auto already = ranges::any_of(*list, [&](const RunningWindow &w) {
			return w.exe == exe;
		});
		if (!already) {
			list->push_back({ exe, QString::fromWCharArray(title, length) });
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&result));
#endif // Q_OS_WIN
	return result;
}

} // namespace Tristegram::Activity
