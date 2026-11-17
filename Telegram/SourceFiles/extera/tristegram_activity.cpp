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
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"

#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#ifdef Q_OS_WIN
#include <windows.h>
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
	QString title;
};

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
	const auto process = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION,
		FALSE,
		pid);
	if (process) {
		wchar_t path[MAX_PATH * 2] = { 0 };
		auto size = DWORD(MAX_PATH * 2);
		if (QueryFullProcessImageNameW(process, 0, path, &size)) {
			result.exe = QFileInfo(
				QString::fromWCharArray(path, int(size))).fileName().toLower();
		}
		CloseHandle(process);
	}
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

void Report() {
	auto &state = Get();
	if (!Extera::Bool(u"tristegram_activity_share"_q)) {
		if (!state.lastSent.isEmpty()) {
			state.lastSent = QString();
			Send(Status());
		}
		return;
	}
	if (state.reporting) {
		return;
	}
	if (auto music = TristegramMusic()) {
		Send(*music);
		return;
	}
	const auto foreground = ForegroundApp();
	if (const auto game = GameApp(foreground.exe); !game.isEmpty()) {
		Send({ .kind = u"game"_q, .title = game, .app = game });
		return;
	}
	if (const auto coding = CodingApp(foreground.exe); !coding.isEmpty()) {
		Send({
			.kind = u"coding"_q,
			.title = ProjectFromTitle(foreground.title),
			.app = coding,
		});
		return;
	}
	state.reporting = true;
	SystemMedia::Request([](std::optional<SystemMedia::Track> track) {
		Get().reporting = false;
		if (track && track->playing && !track->title.isEmpty()) {
			Send({
				.kind = track->artist.isEmpty() ? u"video"_q : u"music"_q,
				.title = track->title,
				.subtitle = track->artist,
				.app = track->app,
			});
		} else {
			Send(Status());
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
		: (status.title + u" — "_q + status.subtitle);
	if (status.kind == u"music"_q) {
		return QString::fromUtf8("\xF0\x9F\x8E\xA7 ")
			+ tr::lng_tga_music(tr::now, lt_name, both);
	} else if (status.kind == u"video"_q) {
		return QString::fromUtf8("\xF0\x9F\x8E\xAC ")
			+ tr::lng_tga_video(tr::now, lt_name, both);
	} else if (status.kind == u"coding"_q) {
		return QString::fromUtf8("\xF0\x9F\x92\xBB ")
			+ (status.title.isEmpty()
				? tr::lng_tga_coding_app(tr::now, lt_name, status.app)
				: tr::lng_tga_coding(
					tr::now,
					lt_name,
					status.title,
					lt_app,
					status.app));
	} else if (status.kind == u"game"_q) {
		return QString::fromUtf8("\xF0\x9F\x8E\xAE ")
			+ tr::lng_tga_game(tr::now, lt_name, status.title);
	}
	return QString();
}

} // namespace Tristegram::Activity
