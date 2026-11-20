/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_music.h"

#include "apiwrap.h"
#include "api/api_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "extera/tristegram_volume.h"
#include "core/file_utilities.h"
#include "storage/localimageloader.h"
#include "extera/tristegram_custom.h"
#include "extera/tristegram_music_skins.h"
#include "extera/tristegram_system_media.h"
#include "settings.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "extera/extera_config.h"
#include "extera/extera_full_player.h"
#include "extera/tristegram_logo.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/effects/animations.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QConicalGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Tristegram {
namespace {

using Type = AudioMsgId::Type;

constexpr auto kRecentLimit = 50;
constexpr auto kSpinPeriod = 3600.; // ms per turn of the record

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] style::font Font(int size, bool bold = false) {
	return style::font(
		Px(size),
		bold ? style::FontFlags(style::FontFlag::Bold) : style::FontFlags(),
		0);
}

[[nodiscard]] QString FormatTime(crl::time ms) {
	const auto seconds = std::max(int(ms / 1000), 0);
	return QString::number(seconds / 60)
		+ ':'
		+ QString::number(seconds % 60).rightJustified(2, '0');
}

struct Track {
	PeerId peer = 0;
	MsgId msg = 0;
	DocumentId document = 0;
	QString title;
	QString performer;
	int duration = 0;
	bool saved = false;
};

[[nodiscard]] bool SameTrack(const Track &a, const Track &b) {
	if (a.saved || b.saved) {
		return (a.document == b.document);
	}
	return (a.peer == b.peer) && (a.msg == b.msg);
}

struct Playlist {
	QString name;
	std::vector<Track> tracks;
	QString cover; // a picture file chosen for the playlist
};

[[nodiscard]] QString CoversFolder() {
	return cWorkingDir() + u"tdata/tristegram_covers/"_q;
}

// Copies a picture as a small square cover, returns its path.
[[nodiscard]] QString SaveCover(const QImage &original) {
	if (original.isNull()) {
		return QString();
	}
	const auto side = std::min(original.width(), original.height());
	const auto square = original.copy(
		(original.width() - side) / 2,
		(original.height() - side) / 2,
		side,
		side).scaled(
			320,
			320,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	QDir().mkpath(CoversFolder());
	const auto path = CoversFolder()
		+ QString::number(base::RandomValue<uint64>(), 16)
		+ u".jpg"_q;
	return square.save(path, "JPG", 90) ? path : QString();
}

[[nodiscard]] const std::vector<QString> &MusicExtensions() {
	// What the player can open: the ffmpeg build has these demuxers.
	static const auto result = std::vector<QString>{
		u"mp3"_q, u"m4a"_q, u"m4b"_q, u"aac"_q, u"ogg"_q, u"oga"_q,
		u"opus"_q, u"flac"_q, u"wav"_q, u"wave"_q, u"mka"_q, u"mp2"_q,
		u"mpga"_q,
	};
	return result;
}

[[nodiscard]] bool IsMusicFile(const QString &path) {
	const auto suffix = QFileInfo(path).suffix().toLower();
	return ranges::contains(MusicExtensions(), suffix);
}

[[nodiscard]] QString MusicFilter() {
	auto masks = QStringList();
	for (const auto &extension : MusicExtensions()) {
		masks.push_back(u"*."_q + extension);
	}
	return tr::lng_tgm_upload_filter(tr::now)
		+ u" ("_q
		+ masks.join(' ')
		+ u");;"_q
		+ FileDialog::AllFilesFilter();
}

// A picture in an album folder: cover.jpg, folder.png and so on.
[[nodiscard]] QString FindFolderCover(const QDir &dir) {
	const auto images = dir.entryInfoList(
		{ u"*.jpg"_q, u"*.jpeg"_q, u"*.png"_q, u"*.webp"_q, u"*.bmp"_q },
		QDir::Files,
		QDir::Name);
	for (const auto &info : images) {
		const auto name = info.completeBaseName().toLower();
		if (name.contains(u"cover"_q)
			|| name.contains(u"folder"_q)
			|| name.contains(u"front"_q)
			|| name.contains(u"album"_q)) {
			return info.absoluteFilePath();
		}
	}
	return images.isEmpty() ? QString() : images.front().absoluteFilePath();
}

[[nodiscard]] std::pair<QString, QString> DescribeSong(
		not_null<DocumentData*> document) {
	auto title = QString();
	auto performer = QString();
	if (const auto song = document->song()) {
		title = song->title;
		performer = song->performer;
	}
	if (title.isEmpty()) {
		title = document->filename();
		const auto dot = title.lastIndexOf('.');
		if (dot > 0) {
			title = title.mid(0, dot);
		}
	}
	if (performer.isEmpty()) {
		const auto split = title.indexOf(u" - "_q);
		if (split > 0) {
			performer = title.mid(0, split).trimmed();
			title = title.mid(split + 3).trimmed();
		}
	}
	if (title.isEmpty()) {
		title = tr::lng_tgm_unknown(tr::now);
	}
	return { title, performer };
}

[[nodiscard]] DocumentData *SongOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document && document->isAudioFile()) ? document : nullptr;
}

[[nodiscard]] std::optional<Track> TrackFromItem(
		not_null<HistoryItem*> item) {
	const auto document = SongOf(item);
	if (!document) {
		return std::nullopt;
	}
	const auto [title, performer] = DescribeSong(document);
	auto result = Track();
	result.peer = item->history()->peer->id;
	result.msg = item->id;
	result.document = document->id;
	result.title = title;
	result.performer = performer;
	result.duration = int(document->duration() / 1000);
	result.saved = !item->isRegular();
	return result;
}

[[nodiscard]] QJsonObject Serialize(const Track &track) {
	auto result = QJsonObject();
	result.insert(u"p"_q, QString::number(SerializePeerId(track.peer)));
	result.insert(u"m"_q, QString::number(track.msg.bare));
	result.insert(u"d"_q, QString::number(track.document));
	result.insert(u"t"_q, track.title);
	result.insert(u"a"_q, track.performer);
	result.insert(u"l"_q, track.duration);
	result.insert(u"s"_q, track.saved);
	return result;
}

[[nodiscard]] Track Deserialize(const QJsonObject &object) {
	auto result = Track();
	result.peer = DeserializePeerId(object.value(u"p"_q).toString().toULongLong());
	result.msg = MsgId(object.value(u"m"_q).toString().toLongLong());
	result.document = object.value(u"d"_q).toString().toULongLong();
	result.title = object.value(u"t"_q).toString();
	result.performer = object.value(u"a"_q).toString();
	result.duration = object.value(u"l"_q).toInt();
	result.saved = object.value(u"s"_q).toBool();
	return result;
}

[[nodiscard]] QJsonArray SerializeList(const std::vector<Track> &list) {
	auto result = QJsonArray();
	for (const auto &track : list) {
		result.push_back(Serialize(track));
	}
	return result;
}

[[nodiscard]] std::vector<Track> DeserializeList(const QJsonArray &array) {
	auto result = std::vector<Track>();
	for (const auto &value : array) {
		auto track = Deserialize(value.toObject());
		if (track.document) {
			result.push_back(std::move(track));
		}
	}
	return result;
}

void PlayItem(not_null<HistoryItem*> item) {
	if (const auto document = SongOf(item)) {
		Media::Player::instance()->play(AudioMsgId(document, item->fullId()));
	}
}

void PlayTrack(not_null<Main::Session*> session, const Track &track) {
	if (track.saved) {
		const auto &list = session->data().savedMusic().list(
			session->userPeerId());
		for (const auto item : list) {
			const auto document = SongOf(item);
			if (document && document->id == track.document) {
				PlayItem(item);
				return;
			}
		}
		return;
	}
	if (const auto item = session->data().message(track.peer, track.msg)) {
		PlayItem(item);
		return;
	}
	const auto peer = session->data().peer(track.peer);
	const auto weak = base::make_weak(session.get());
	session->api().requestMessageData(peer, track.msg, [=] {
		if (const auto strong = weak.get()) {
			const auto item = strong->data().message(track.peer, track.msg);
			if (item) {
				PlayItem(item);
			}
		}
	});
}

[[nodiscard]] std::optional<Track> CurrentTrack() {
	const auto current = Media::Player::instance()->current(Type::Song);
	const auto document = current.audio();
	if (!document) {
		return std::nullopt;
	}
	if (const auto item = document->owner().message(current.contextId())) {
		return TrackFromItem(item);
	}
	const auto [title, performer] = DescribeSong(document);
	auto result = Track();
	result.document = document->id;
	result.title = title;
	result.performer = performer;
	result.duration = int(document->duration() / 1000);
	return result;
}

// Playlists, recent tracks and the queue, kept in the Extera config.
class Library final {
public:
	static Library &Instance() {
		// Never destroyed: it listens to the player until the app quits.
		static const auto result = new Library();
		return *result;
	}

	[[nodiscard]] const std::vector<Playlist> &playlists() const {
		return _playlists;
	}
	[[nodiscard]] const std::vector<Track> &recent() const {
		return _recent;
	}
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

	void create(const QString &name) {
		_playlists.push_back({ name, {} });
		save();
	}
	void rename(int index, const QString &name) {
		if (index >= 0 && index < int(_playlists.size())) {
			_playlists[index].name = name;
			save();
		}
	}
	void remove(int index) {
		if (index >= 0 && index < int(_playlists.size())) {
			_playlists.erase(_playlists.begin() + index);
			save();
		}
	}
	void setCover(int index, const QString &path) {
		if (index >= 0 && index < int(_playlists.size())) {
			_playlists[index].cover = path;
			_covers.remove(path);
			save();
		}
	}
	[[nodiscard]] QImage cover(int index) {
		if (index < 0 || index >= int(_playlists.size())) {
			return QImage();
		}
		const auto &path = _playlists[index].cover;
		if (path.isEmpty()) {
			return QImage();
		}
		const auto i = _covers.find(path);
		if (i != end(_covers)) {
			return i->second;
		}
		auto image = QImage(path).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
		_covers.emplace(path, image);
		return image;
	}
	[[nodiscard]] int find(const QString &name) const {
		for (auto i = 0; i != int(_playlists.size()); ++i) {
			if (_playlists[i].name == name) {
				return i;
			}
		}
		return -1;
	}
	// Creates a playlist if there is none with this name.
	int ensure(const QString &name, const QString &cover = QString()) {
		auto index = find(name);
		if (index < 0) {
			_playlists.push_back({ name, {}, cover });
			index = int(_playlists.size()) - 1;
			save();
		} else if (!cover.isEmpty() && _playlists[index].cover.isEmpty()) {
			setCover(index, cover);
		}
		return index;
	}

	// Songs sent to Saved Messages go to a playlist once they are sent.
	void expectUpload(
			not_null<Main::Session*> session,
			const QString &fileName,
			const QString &playlist) {
		_uploads.push_back({ fileName, playlist });
		if (_uploadsSessions.contains(session)) {
			return;
		}
		_uploadsSessions.emplace(session);
		const auto weak = base::make_weak(session.get());
		session->data().itemIdChanged(
		) | rpl::on_next([=](const Data::Session::IdChange &change) {
			const auto strong = weak.get();
			if (!strong || _uploads.empty()) {
				return;
			}
			const auto item = strong->data().message(change.newId);
			if (!item || !item->history()->peer->isSelf()) {
				return;
			}
			const auto document = SongOf(item);
			if (!document) {
				return;
			}
			const auto name = document->filename();
			const auto i = ranges::find_if(_uploads, [&](const Upload &u) {
				return (u.fileName == name);
			});
			if (i == end(_uploads)) {
				return;
			}
			const auto playlist = i->playlist;
			_uploads.erase(i);
			if (auto track = TrackFromItem(item)) {
				add(ensure(playlist), *track);
			}
		}, _lifetime);
	}
	void add(int index, const Track &track) {
		if (index < 0 || index >= int(_playlists.size())) {
			return;
		}
		auto &tracks = _playlists[index].tracks;
		const auto already = ranges::any_of(tracks, [&](const Track &t) {
			return SameTrack(t, track);
		});
		if (!already) {
			tracks.push_back(track);
			save();
		}
	}
	void removeTrack(int index, int position) {
		if (index < 0 || index >= int(_playlists.size())) {
			return;
		}
		auto &tracks = _playlists[index].tracks;
		if (position >= 0 && position < int(tracks.size())) {
			tracks.erase(tracks.begin() + position);
			save();
		}
	}
	void pushRecent(const Track &track) {
		_recent.erase(ranges::remove_if(_recent, [&](const Track &t) {
			return SameTrack(t, track);
		}), _recent.end());
		_recent.insert(_recent.begin(), track);
		if (_recent.size() > kRecentLimit) {
			_recent.resize(kRecentLimit);
		}
		saveRecent();
	}

	// Plays the list from the given position and follows it to the end.
	void playQueue(
			not_null<Main::Session*> session,
			std::vector<Track> list,
			int index) {
		if (index < 0 || index >= int(list.size())) {
			return;
		}
		_queue = std::move(list);
		_queueIndex = index;
		_session = base::make_weak(session.get());
		PlayTrack(session, _queue[_queueIndex]);
	}
	bool queueStep(int delta) {
		const auto session = _session.get();
		const auto current = CurrentTrack();
		if (!session || !current || _queue.empty()) {
			return false;
		}
		const auto i = ranges::find_if(_queue, [&](const Track &t) {
			return SameTrack(t, *current);
		});
		if (i == _queue.end()) {
			return false;
		}
		const auto next = int(i - _queue.begin()) + delta;
		if (next < 0 || next >= int(_queue.size())) {
			return false;
		}
		_queueIndex = next;
		PlayTrack(session, _queue[next]);
		return true;
	}

	struct StatEntry {
		QString title;
		QString performer;
		int64 ms = 0;
		int plays = 0;
	};
	[[nodiscard]] int64 statsTotal() const {
		return _statsTotal;
	}
	[[nodiscard]] int statsPlays() const {
		auto result = 0;
		for (const auto &[key, entry] : _stats) {
			result += entry.plays;
		}
		return result;
	}
	[[nodiscard]] std::vector<StatEntry> topTracks(int limit) const {
		auto result = std::vector<StatEntry>();
		for (const auto &[key, entry] : _stats) {
			result.push_back(entry);
		}
		ranges::sort(result, ranges::greater(), &StatEntry::ms);
		if (int(result.size()) > limit) {
			result.resize(limit);
		}
		return result;
	}
	[[nodiscard]] std::vector<StatEntry> topArtists(int limit) const {
		auto map = base::flat_map<QString, StatEntry>();
		for (const auto &[key, entry] : _stats) {
			const auto name = entry.performer.isEmpty()
				? tr::lng_tgm_unknown_artist(tr::now)
				: entry.performer;
			auto &artist = map[name.toLower()];
			artist.performer = name;
			artist.ms += entry.ms;
			artist.plays += entry.plays;
		}
		auto result = std::vector<StatEntry>();
		for (const auto &[key, entry] : map) {
			result.push_back(entry);
		}
		ranges::sort(result, ranges::greater(), &StatEntry::ms);
		if (int(result.size()) > limit) {
			result.resize(limit);
		}
		return result;
	}

	void ensureTracking() {
		if (_tracking) {
			return;
		}
		_tracking = true;
		const auto instance = Media::Player::instance();
		instance->trackChanged(
		) | rpl::filter(
			rpl::mappers::_1 == Type::Song
		) | rpl::on_next([=] {
			const auto track = CurrentTrack();
			if (track && track->peer) {
				pushRecent(*track);
			}
			statsTrackChanged(track);
		}, _lifetime);
		instance->updatedNotifier(
		) | rpl::filter([](const Media::Player::TrackState &state) {
			return (state.id.type() == Type::Song);
		}) | rpl::on_next([=](const Media::Player::TrackState &state) {
			statsTick(state);
		}, _lifetime);
		statsTrackChanged(CurrentTrack());
		instance->updatedNotifier(
		) | rpl::filter([](const Media::Player::TrackState &state) {
			return (state.id.type() == Type::Song)
				&& Media::Player::IsStoppedAtEnd(state.state);
		}) | rpl::on_next([=] {
			// Our playlist goes on, even if the chat has other songs.
			if (_queue.empty()) {
				return;
			}
			crl::on_main([=] {
				queueStep(1);
			});
		}, _lifetime);
	}

private:
	Library() {
		load();
		loadStats();
	}

	void statsTrackChanged(const std::optional<Track> &track) {
		_statsTick = 0;
		if (!track) {
			_statsKey = QString();
			return;
		}
		const auto key = (track->performer + u" - "_q + track->title).toLower();
		if (key == _statsKey) {
			return;
		}
		_statsKey = key;
		auto &entry = _stats[key];
		entry.title = track->title;
		entry.performer = track->performer;
		++entry.plays;
		saveStats();
	}
	void statsTick(const Media::Player::TrackState &state) {
		const auto playing = !Media::Player::IsStoppedOrStopping(state.state)
			&& !Media::Player::IsPausedOrPausing(state.state);
		const auto now = crl::now();
		if (playing && _statsTick && !_statsKey.isEmpty()) {
			const auto delta = std::clamp(now - _statsTick, crl::time(0), crl::time(3000));
			_stats[_statsKey].ms += delta;
			_statsTotal += delta;
			if (now - _statsSaved > 30000) {
				saveStats();
			}
		}
		_statsTick = playing ? now : 0;
	}
	void loadStats() {
		const auto object = QJsonDocument::fromJson(
			Extera::String(u"tristegram_music_stats"_q).toUtf8()).object();
		_statsTotal = int64(object.value(u"total"_q).toDouble());
		for (const auto &value : object.value(u"t"_q).toArray()) {
			const auto row = value.toArray();
			if (row.size() < 4) {
				continue;
			}
			auto entry = StatEntry{
				.title = row[0].toString(),
				.performer = row[1].toString(),
				.ms = int64(row[2].toDouble()),
				.plays = row[3].toInt(),
			};
			_stats.emplace(
				(entry.performer + u" - "_q + entry.title).toLower(),
				std::move(entry));
		}
	}
	void saveStats() {
		_statsSaved = crl::now();
		auto list = topTracks(500);
		auto array = QJsonArray();
		for (const auto &entry : list) {
			array.push_back(QJsonArray{
				entry.title,
				entry.performer,
				double(entry.ms),
				entry.plays,
			});
		}
		auto object = QJsonObject();
		object.insert(u"total"_q, double(_statsTotal));
		object.insert(u"t"_q, array);
		Extera::Config::Instance().setString(
			u"tristegram_music_stats"_q,
			QString::fromUtf8(
				QJsonDocument(object).toJson(QJsonDocument::Compact)));
		_changes.fire({});
	}

	void load() {
		const auto playlists = QJsonDocument::fromJson(
			Extera::String(u"tristegram_playlists"_q).toUtf8()).array();
		for (const auto &value : playlists) {
			const auto object = value.toObject();
			auto playlist = Playlist();
			playlist.name = object.value(u"name"_q).toString();
			playlist.tracks = DeserializeList(
				object.value(u"tracks"_q).toArray());
			playlist.cover = object.value(u"cover"_q).toString();
			if (!playlist.name.isEmpty()) {
				_playlists.push_back(std::move(playlist));
			}
		}
		_recent = DeserializeList(QJsonDocument::fromJson(
			Extera::String(u"tristegram_music_recent"_q).toUtf8()).array());
	}
	void save() {
		auto array = QJsonArray();
		for (const auto &playlist : _playlists) {
			auto object = QJsonObject();
			object.insert(u"name"_q, playlist.name);
			object.insert(u"tracks"_q, SerializeList(playlist.tracks));
			if (!playlist.cover.isEmpty()) {
				object.insert(u"cover"_q, playlist.cover);
			}
			array.push_back(object);
		}
		Extera::Config::Instance().setString(
			u"tristegram_playlists"_q,
			QString::fromUtf8(
				QJsonDocument(array).toJson(QJsonDocument::Compact)));
		_changes.fire({});
	}
	void saveRecent() {
		Extera::Config::Instance().setString(
			u"tristegram_music_recent"_q,
			QString::fromUtf8(QJsonDocument(
				SerializeList(_recent)).toJson(QJsonDocument::Compact)));
		_changes.fire({});
	}

	struct Upload {
		QString fileName;
		QString playlist;
	};

	std::vector<Playlist> _playlists;
	std::vector<Track> _recent;
	std::vector<Upload> _uploads;
	base::flat_map<QString, StatEntry> _stats;
	QString _statsKey;
	int64 _statsTotal = 0;
	crl::time _statsTick = 0;
	crl::time _statsSaved = 0;
	base::flat_set<not_null<Main::Session*>> _uploadsSessions;
	base::flat_map<QString, QImage> _covers;
	std::vector<Track> _queue;
	int _queueIndex = -1;
	base::weak_ptr<Main::Session> _session;
	bool _tracking = false;
	rpl::event_stream<> _changes;
	rpl::lifetime _lifetime;

};

// The cover of the track that plays now, loaded on demand.
class Cover final {
public:
	bool set(DocumentData *document, FullMsgId context) {
		if (document == _document) {
			return false;
		}
		_document = document;
		_image = QImage();
		_media = document ? document->createMediaView() : nullptr;
		if (_media) {
			_media->thumbnailWanted(Data::FileOriginMessage(context));
		}
		check();
		return true;
	}
	bool check() {
		if (!_media || !_image.isNull()) {
			return false;
		}
		if (const auto image = _media->thumbnail()) {
			_image = image->original().convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
			return true;
		}
		return false;
	}
	[[nodiscard]] const QImage &image() const {
		return _image;
	}

private:
	DocumentData *_document = nullptr;
	std::shared_ptr<Data::DocumentMedia> _media;
	QImage _image;

};

[[nodiscard]] QColor LabelColor(uint64 seed) {
	static const auto colors = std::vector<QColor>{
		QColor(0xF0, 0x7A, 0x0C),
		QColor(0xE8, 0x31, 0x8A),
		QColor(0x5B, 0x5C, 0xF6),
		QColor(0x30, 0xB0, 0xC7),
		QColor(0x34, 0xC7, 0x59),
		QColor(0xAF, 0x52, 0xDE),
		QColor(0xFF, 0x45, 0x3A),
	};
	return colors[seed % colors.size()];
}

// A vinyl record: grooves, the cover as a label, a stationary sheen.
void PaintVinyl(
		QPainter &p,
		QRectF rect,
		const QImage &cover,
		double angle,
		uint64 seed) {
	auto hq = PainterHighQualityEnabler(p);
	const auto center = rect.center();
	const auto radius = rect.width() / 2.;

	auto body = QRadialGradient(center, radius);
	body.setColorAt(0., QColor(0x2C, 0x2C, 0x32));
	body.setColorAt(0.7, QColor(0x17, 0x17, 0x1B));
	body.setColorAt(1., QColor(0x0B, 0x0B, 0x0E));
	p.setPen(Qt::NoPen);
	p.setBrush(body);
	p.drawEllipse(rect);

	// Grooves.
	p.setBrush(Qt::NoBrush);
	const auto grooves = std::max(6, int(radius / Px(4)));
	for (auto i = 0; i != grooves; ++i) {
		const auto r = radius * (0.42 + 0.55 * i / double(grooves));
		p.setPen(QPen(
			QColor(255, 255, 255, (i % 3 == 0) ? 20 : 10),
			std::max(1., radius / 120.)));
		p.drawEllipse(center, r, r);
	}

	// The label turns with the record.
	const auto label = radius * 0.38;
	p.save();
	p.translate(center);
	p.rotate(angle);
	const auto labelRect = QRectF(-label, -label, label * 2, label * 2);
	auto clip = QPainterPath();
	clip.addEllipse(labelRect);
	p.setClipPath(clip);
	if (!cover.isNull()) {
		const auto side = std::min(cover.width(), cover.height());
		const auto source = QRect(
			(cover.width() - side) / 2,
			(cover.height() - side) / 2,
			side,
			side);
		p.drawImage(labelRect, cover, source);
	} else {
		const auto base = LabelColor(seed);
		auto gradient = QLinearGradient(
			labelRect.topLeft(),
			labelRect.bottomRight());
		gradient.setColorAt(0., base.lighter(125));
		gradient.setColorAt(1., base.darker(125));
		p.fillRect(labelRect, gradient);
		p.setPen(QPen(QColor(255, 255, 255, 60), std::max(1., label / 30.)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QPointF(), label * 0.72, label * 0.72);
		// A note on the label, so the turning is visible.
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 230));
		const auto s = label * 0.5;
		p.drawEllipse(QPointF(-s * 0.35, s * 0.45), s * 0.26, s * 0.2);
		p.drawRect(QRectF(-s * 0.13, -s * 0.55, s * 0.12, s * 1.0));
		p.drawRect(QRectF(-s * 0.13, -s * 0.55, s * 0.5, s * 0.14));
	}
	p.restore();

	// The hole in the middle.
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x0B, 0x0B, 0x0E));
	p.drawEllipse(center, radius * 0.045, radius * 0.045);

	// Light that does not turn with the record.
	auto sheen = QConicalGradient(center, 45.);
	sheen.setColorAt(0.00, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.08, QColor(255, 255, 255, 34));
	sheen.setColorAt(0.16, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.50, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.58, QColor(255, 255, 255, 26));
	sheen.setColorAt(0.66, QColor(255, 255, 255, 0));
	sheen.setColorAt(1.00, QColor(255, 255, 255, 0));
	p.setBrush(sheen);
	auto ring = QPainterPath();
	ring.addEllipse(rect);
	ring.addEllipse(center, label, label);
	p.drawPath(ring);
}

// A SoundCloud-like wave: bars from the bottom with a faint reflection.
void PaintWave(
		QPainter &p,
		QRectF rect,
		uint64 seed,
		double progress,
		double hover) {
	auto hq = PainterHighQualityEnabler(p);
	const auto bar = double(Px(3));
	const auto gap = double(Px(2));
	const auto count = std::max(int(rect.width() / (bar + gap)), 8);
	const auto baseline = rect.y() + rect.height() * 0.72;
	const auto top = rect.height() * 0.72;
	const auto bottom = rect.height() * 0.26;
	const auto colors = CurrentLogoStyle();
	auto played = QLinearGradient(rect.topLeft(), rect.topRight());
	played.setColorAt(0., colors.from);
	played.setColorAt(1., colors.to);
	auto x = seed ? seed : uint64(0x9E3779B97F4A7C15ULL);
	auto previous = 0.5;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		x = x * 6364136223846793005ULL + 1442695040888963407ULL;
		const auto random = ((x >> 33) % 1000) / 1000.;
		const auto envelope = 0.55
			+ 0.45 * std::sin(3.14159 * (i + 0.5) / count);
		const auto value = std::clamp(
			(0.35 + 0.65 * (0.55 * random + 0.45 * previous)) * envelope,
			0.12,
			1.);
		previous = random;
		const auto left = rect.x() + i * (bar + gap);
		const auto position = (i + 0.5) / count;
		const auto isPlayed = (position <= progress);
		const auto isHover = (hover >= 0.)
			&& (position <= hover)
			&& !isPlayed;
		if (isPlayed) {
			p.setBrush(played);
		} else {
			p.setBrush(Alpha(st::windowFg->c, isHover ? 0.38 : 0.2));
		}
		const auto up = top * value;
		p.drawRoundedRect(
			QRectF(left, baseline - up, bar, up),
			bar / 2.,
			bar / 2.);
		p.setOpacity(0.35);
		const auto down = bottom * value;
		p.drawRoundedRect(
			QRectF(left, baseline + Px(1), bar, down),
			bar / 2.,
			bar / 2.);
		p.setOpacity(1.);
	}
}

void PaintPlayIcon(QPainter &p, QRectF rect, bool playing, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto c = rect.center();
	const auto s = rect.width() / 2.;
	if (playing) {
		const auto w = s * 0.32;
		const auto h = s * 1.1;
		p.drawRoundedRect(QRectF(c.x() - w * 1.35, c.y() - h / 2, w, h), w / 3, w / 3);
		p.drawRoundedRect(QRectF(c.x() + w * 0.35, c.y() - h / 2, w, h), w / 3, w / 3);
	} else {
		auto path = QPainterPath();
		path.moveTo(c.x() - s * 0.38, c.y() - s * 0.6);
		path.lineTo(c.x() + s * 0.62, c.y());
		path.lineTo(c.x() - s * 0.38, c.y() + s * 0.6);
		path.closeSubpath();
		p.drawPath(path);
	}
}

void PaintSkipIcon(QPainter &p, QRectF rect, bool forward, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto c = rect.center();
	const auto s = rect.width() / 2.;
	const auto dir = forward ? 1. : -1.;
	for (auto k = 0; k != 2; ++k) {
		const auto shift = (k == 0 ? -0.42 : 0.18) * s * dir;
		auto path = QPainterPath();
		path.moveTo(c.x() + shift - s * 0.3 * dir, c.y() - s * 0.42);
		path.lineTo(c.x() + shift + s * 0.3 * dir, c.y());
		path.lineTo(c.x() + shift - s * 0.3 * dir, c.y() + s * 0.42);
		path.closeSubpath();
		p.drawPath(path);
	}
	const auto barX = c.x() + s * 0.5 * dir - (forward ? 0. : s * 0.12);
	p.drawRoundedRect(
		QRectF(barX, c.y() - s * 0.42, s * 0.12, s * 0.84),
		s * 0.05,
		s * 0.05);
}

void PaintMoreIcon(QPainter &p, QRectF rect, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto c = rect.center();
	const auto r = rect.width() * 0.07;
	for (auto i = -1; i <= 1; ++i) {
		p.drawEllipse(QPointF(c.x() + i * r * 3.2, c.y()), r, r);
	}
}

void PaintEqualizer(QPainter &p, QRectF rect, crl::time now, bool playing) {
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	auto gradient = QLinearGradient(rect.bottomLeft(), rect.topLeft());
	gradient.setColorAt(0., colors.from);
	gradient.setColorAt(1., colors.to);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	const auto w = rect.width() / 5.;
	for (auto i = 0; i != 3; ++i) {
		const auto phase = playing
			? (0.5 + 0.5 * std::sin(now / (170. + i * 60.) + i * 1.7))
			: 0.3;
		const auto h = rect.height() * (0.25 + 0.75 * phase);
		p.drawRoundedRect(
			QRectF(rect.x() + i * w * 2, rect.bottom() - h, w, h),
			w / 2,
			w / 2);
	}
}

[[nodiscard]] bool IsPlaying() {
	const auto state = Media::Player::instance()->getState(Type::Song);
	return state.id
		&& !Media::Player::IsStoppedOrStopping(state.state)
		&& !Media::Player::IsPausedOrPausing(state.state);
}

[[nodiscard]] double SpinAngle(crl::time now, crl::time &last, double &angle) {
	if (IsPlaying()) {
		if (last) {
			angle += 360. * double(now - last) / kSpinPeriod;
			angle = std::fmod(angle, 360.);
		}
		last = now;
	} else {
		last = 0;
	}
	return angle;
}

void ShowPlaylistNameBox(
		not_null<Window::SessionController*> controller,
		const QString &value,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(value.isEmpty()
			? tr::lng_tgm_create()
			: tr::lng_tgm_rename());
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_tgm_name(),
			value));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			const auto name = field->getLastText().trimmed();
			if (name.isEmpty()) {
				field->showError();
				return;
			}
			const auto callback = done;
			box->closeBox();
			callback(name);
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->addButton(
			value.isEmpty() ? tr::lng_tgm_create_button() : tr::lng_tgm_save(),
			submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void UploadMusic(
		not_null<Window::SessionController*> controller,
		const QStringList &paths,
		const QString &playlist) {
	auto files = QStringList();
	for (const auto &path : paths) {
		if (IsMusicFile(path)) {
			files.push_back(path);
		}
	}
	if (files.isEmpty()) {
		controller->showToast(tr::lng_tgm_upload_none(tr::now));
		return;
	}
	const auto session = &controller->session();
	auto list = Storage::PrepareMediaList(
		files,
		st::sendMediaPreviewSize,
		session->premium());
	if (list.error != Ui::PreparedList::Error::None) {
		controller->showToast(tr::lng_tgm_upload_failed(tr::now));
		return;
	}
	auto &library = Library::Instance();
	library.ensure(playlist);
	for (const auto &file : list.files) {
		library.expectUpload(
			session,
			QFileInfo(file.path).fileName(),
			playlist);
	}
	const auto history = session->data().history(session->userPeerId());
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	session->api().sendFiles(
		std::move(list),
		SendMediaType::File,
		nullptr,
		action);
	controller->showToast(tr::lng_tgm_uploading(tr::now));
}

void ChooseMusicFiles(
		not_null<Window::SessionController*> controller,
		const QString &playlist) {
	FileDialog::GetOpenPaths(
		Core::App().getFileDialogParent(),
		tr::lng_tgm_upload_files(tr::now),
		MusicFilter(),
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			UploadMusic(controller, result.paths, playlist);
		}));
}

// A folder becomes a playlist with its name and its cover picture.
void ChooseMusicFolder(not_null<Window::SessionController*> controller) {
	FileDialog::GetFolder(
		Core::App().getFileDialogParent(),
		tr::lng_tgm_upload_album(tr::now),
		QString(),
		crl::guard(controller, [=](QString &&folder) {
			const auto dir = QDir(folder);
			auto paths = QStringList();
			const auto entries = dir.entryInfoList(QDir::Files, QDir::Name);
			for (const auto &info : entries) {
				if (IsMusicFile(info.absoluteFilePath())) {
					paths.push_back(info.absoluteFilePath());
				}
			}
			if (paths.isEmpty()) {
				controller->showToast(tr::lng_tgm_upload_none(tr::now));
				return;
			}
			auto name = dir.dirName().trimmed();
			if (name.isEmpty()) {
				name = tr::lng_tgm_uploaded(tr::now);
			}
			const auto coverFile = FindFolderCover(dir);
			const auto cover = coverFile.isEmpty()
				? QString()
				: SaveCover(QImage(coverFile));
			Library::Instance().ensure(name, cover);
			UploadMusic(controller, paths, name);
		}));
}

void ChoosePlaylistCover(
		not_null<Window::SessionController*> controller,
		int index) {
	FileDialog::GetOpenPath(
		Core::App().getFileDialogParent(),
		tr::lng_tgm_cover_choose(tr::now),
		FileDialog::ImagesOrAllFilter(),
		crl::guard(controller, [=](FileDialog::OpenResult &&result) {
			auto image = !result.remoteContent.isEmpty()
				? QImage::fromData(result.remoteContent)
				: result.paths.isEmpty()
				? QImage()
				: QImage(result.paths.front());
			const auto path = SaveCover(image);
			if (path.isEmpty()) {
				controller->showToast(tr::lng_tgm_cover_failed(tr::now));
				return;
			}
			Library::Instance().setCover(index, path);
		}));
}

constexpr auto kPlaylistCodePrefix = "tristegram-playlist:";

// Sends the code message and then the songs themselves, so another
// Tristegram can make the same playlist from that chat.
void SharePlaylist(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto &playlists = Library::Instance().playlists();
	if (index < 0 || index >= int(playlists.size())) {
		return;
	}
	const auto playlist = playlists[index];
	const auto session = &controller->session();
	auto items = HistoryItemsList();
	for (const auto &track : playlist.tracks) {
		if (track.saved) {
			for (const auto item : session->data().savedMusic().list(
					session->userPeerId())) {
				const auto document = SongOf(item);
				if (document && document->id == track.document) {
					items.push_back(item);
					break;
				}
			}
		} else if (const auto item = session->data().message(
				track.peer,
				track.msg)) {
			items.push_back(item);
		}
	}
	if (items.empty()) {
		controller->showToast(tr::lng_tgm_share_empty(tr::now));
		return;
	}
	auto object = QJsonObject();
	object.insert(u"name"_q, playlist.name);
	object.insert(u"count"_q, int(items.size()));
	const auto code = QString::fromLatin1(kPlaylistCodePrefix)
		+ QString::fromLatin1(QJsonDocument(object).toJson(
			QJsonDocument::Compact).toBase64(
				QByteArray::Base64UrlEncoding
				| QByteArray::OmitTrailingEquals));
	const auto text = tr::lng_tgm_share_text(tr::now)
		+ u" «"_q
		+ playlist.name
		+ u"» · "_q
		+ QString::number(items.size())
		+ '\n'
		+ code;
	auto ids = MessageIdsList();
	for (const auto item : items) {
		ids.push_back(item->fullId());
	}
	SendToChosenChat(controller, text, [=](not_null<Data::Thread*> thread) {
		auto resolved = HistoryItemsList();
		for (const auto &id : ids) {
			if (const auto item = thread->session().data().message(id)) {
				resolved.push_back(item);
			}
		}
		if (!resolved.empty()) {
			thread->session().api().forwardMessages(
				Data::ResolvedForwardDraft{ .items = std::move(resolved) },
				Api::SendAction(thread));
		}
	});
}

[[nodiscard]] std::optional<QJsonObject> PlaylistCode(
		not_null<HistoryItem*> item) {
	const auto text = item->originalText().text;
	const auto prefix = QString::fromLatin1(kPlaylistCodePrefix);
	const auto start = text.indexOf(prefix);
	if (start < 0) {
		return std::nullopt;
	}
	auto end = start + prefix.size();
	while (end < text.size()
		&& (text[end].isLetterOrNumber()
			|| text[end] == '-'
			|| text[end] == '_')) {
		++end;
	}
	const auto encoded = text.mid(
		start + prefix.size(),
		end - start - prefix.size());
	const auto object = QJsonDocument::fromJson(QByteArray::fromBase64(
		encoded.toLatin1(),
		QByteArray::Base64UrlEncoding
			| QByteArray::OmitTrailingEquals)).object();
	if (object.value(u"name"_q).toString().isEmpty()) {
		return std::nullopt;
	}
	return object;
}

// The songs right after the code message, from the same sender.
void ImportPlaylist(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		const QJsonObject &code) {
	const auto count = std::clamp(code.value(u"count"_q).toInt(), 1, 500);
	const auto history = item->history();
	const auto from = item->from();
	auto tracks = std::vector<Track>();
	const auto last = item->id.bare + count * 4 + 20;
	for (auto id = item->id.bare + 1; id <= last; ++id) {
		const auto next = history->owner().message(
			history->peer->id,
			MsgId(id));
		if (!next || next->from() != from) {
			continue;
		}
		if (auto track = TrackFromItem(next)) {
			tracks.push_back(std::move(*track));
			if (int(tracks.size()) >= count) {
				break;
			}
		}
	}
	if (tracks.empty()) {
		controller->showToast(tr::lng_tgm_import_empty(tr::now));
		return;
	}
	auto &library = Library::Instance();
	auto name = code.value(u"name"_q).toString();
	if (library.find(name) >= 0) {
		auto i = 2;
		while (library.find(name + u" ("_q + QString::number(i) + ')') >= 0) {
			++i;
		}
		name += u" ("_q + QString::number(i) + ')';
	}
	const auto index = library.ensure(name);
	for (const auto &track : tracks) {
		library.add(index, track);
	}
	controller->showToast(tr::lng_tgm_import_done(tr::now));
}

enum class Source {
	Mine,
	Recent,
	Playlist,
	System,
	Upload,
	Stats,
};

constexpr auto kStatsSourceSaved = 1001;

[[nodiscard]] QString FormatListened(int64 ms) {
	const auto minutes = ms / 60000;
	const auto hours = minutes / 60;
	return hours
		? tr::lng_tgm_stats_hours(
			tr::now,
			lt_hours,
			QString::number(hours),
			lt_minutes,
			QString::number(minutes % 60))
		: tr::lng_tgm_stats_minutes(
			tr::now,
			lt_minutes,
			QString::number(minutes));
}

constexpr auto kSystemSourceSaved = 1000;

// The left column: my music, recent and playlists.
class Sidebar final : public Ui::RpWidget {
public:
	Sidebar(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<std::pair<Source, int>> chosen() const {
		return _chosen.events();
	}
	void choose(Source source, int index);
	[[nodiscard]] Source source() const {
		return _source;
	}
	[[nodiscard]] int index() const {
		return _index;
	}

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Row {
		Source source = Source::Mine;
		int index = -1; // -2 for "create playlist"
		QRect rect;
	};
	void relayout();
	[[nodiscard]] int rowAt(QPoint position) const;
	void showPlaylistMenu(int index);
	void showUploadMenu();

	const not_null<Window::SessionController*> _controller;
	std::vector<Row> _rows;
	int _labelTop = 0;
	int _over = -1;
	Source _source = Source::Mine;
	int _index = -1;
	rpl::event_stream<std::pair<Source, int>> _chosen;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

Sidebar::Sidebar(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	setMouseTracking(true);
	Library::Instance().changes(
	) | rpl::on_next([=] {
		const auto count = int(Library::Instance().playlists().size());
		if (_source == Source::Playlist && _index >= count) {
			choose(Source::Mine, -1);
		}
		resizeToWidth(width());
		update();
	}, lifetime());
}

void Sidebar::choose(Source source, int index) {
	_source = source;
	_index = index;
	update();
	_chosen.fire({ source, index });
}

int Sidebar::resizeGetHeight(int newWidth) {
	relayout();
	return _rows.empty() ? Px(100) : (_rows.back().rect.bottom() + Px(12));
}

void Sidebar::relayout() {
	_rows.clear();
	const auto side = Px(10);
	const auto w = width() - 2 * side;
	auto top = Px(8);
	const auto add = [&](Source source, int index) {
		_rows.push_back({ source, index, QRect(side, top, w, Px(44)) });
		top += Px(44) + Px(2);
	};
	add(Source::Mine, -1);
	add(Source::Recent, -1);
	add(Source::Stats, -1);
	if (SystemMedia::Supported()) {
		add(Source::System, -1);
	}
	top += Px(8);
	_labelTop = top;
	top += Px(26);
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(playlists.size()); ++i) {
		add(Source::Playlist, i);
	}
	add(Source::Playlist, -2);
	add(Source::Upload, -1);
}

int Sidebar::rowAt(QPoint position) const {
	for (auto i = 0; i != int(_rows.size()); ++i) {
		if (_rows[i].rect.contains(position)) {
			return i;
		}
	}
	return -1;
}

void Sidebar::mouseMoveEvent(QMouseEvent *e) {
	const auto over = rowAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void Sidebar::leaveEventHook(QEvent *e) {
	_over = -1;
	update();
}

void Sidebar::mousePressEvent(QMouseEvent *e) {
	const auto index = rowAt(e->pos());
	if (index < 0) {
		return;
	}
	const auto row = _rows[index];
	if (row.source == Source::Upload) {
		showUploadMenu();
		return;
	}
	if (row.source == Source::Playlist && row.index == -2) {
		if (e->button() == Qt::LeftButton) {
			ShowPlaylistNameBox(_controller, QString(), [=](QString name) {
				Library::Instance().create(name);
				choose(
					Source::Playlist,
					int(Library::Instance().playlists().size()) - 1);
			});
		}
		return;
	}
	if (e->button() == Qt::RightButton && row.source == Source::Playlist) {
		showPlaylistMenu(row.index);
		return;
	}
	choose(row.source, row.index);
}

void Sidebar::showPlaylistMenu(int index) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto controller = _controller;
	_menu->addAction(tr::lng_tgm_rename(tr::now), [=] {
		const auto &playlists = Library::Instance().playlists();
		if (index < 0 || index >= int(playlists.size())) {
			return;
		}
		ShowPlaylistNameBox(controller, playlists[index].name, [=](
				QString name) {
			Library::Instance().rename(index, name);
		});
	}, &st::menuIconEdit);
	_menu->addAction(tr::lng_tgm_cover_choose(tr::now), [=] {
		ChoosePlaylistCover(controller, index);
	}, &st::menuIconPhoto);
	{
		const auto &playlists = Library::Instance().playlists();
		if (index >= 0
			&& index < int(playlists.size())
			&& !playlists[index].cover.isEmpty()) {
			_menu->addAction(tr::lng_tgm_cover_remove(tr::now), [=] {
				Library::Instance().setCover(index, QString());
			}, &st::menuIconCancel);
		}
	}
	_menu->addAction(tr::lng_tgm_share(tr::now), [=] {
		SharePlaylist(controller, index);
	}, &st::menuIconShare);
	_menu->addAction(tr::lng_tgm_upload_here(tr::now), [=] {
		const auto &playlists = Library::Instance().playlists();
		if (index >= 0 && index < int(playlists.size())) {
			ChooseMusicFiles(controller, playlists[index].name);
		}
	}, &st::menuIconAdd);
	_menu->addAction(tr::lng_tgm_delete(tr::now), [=] {
		Library::Instance().remove(index);
	}, &st::menuIconDelete);
	_menu->popup(QCursor::pos());
}

void Sidebar::showUploadMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto controller = _controller;
	const auto playlist = [=] {
		const auto &playlists = Library::Instance().playlists();
		return (_source == Source::Playlist
			&& _index >= 0
			&& _index < int(playlists.size()))
			? playlists[_index].name
			: tr::lng_tgm_uploaded(tr::now);
	}();
	_menu->addAction(tr::lng_tgm_upload_files(tr::now), [=] {
		ChooseMusicFiles(controller, playlist);
	}, &st::menuIconAdd);
	_menu->addAction(tr::lng_tgm_upload_album(tr::now), [=] {
		ChooseMusicFolder(controller);
	}, &st::menuIconAddToFolder);
	_menu->popup(QCursor::pos());
}

void Sidebar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto &row = _rows[i];
		const auto create = ((row.source == Source::Playlist)
			&& (row.index == -2))
			|| (row.source == Source::Upload);
		const auto active = !create
			&& (row.source == _source)
			&& (row.source != Source::Playlist || row.index == _index);
		if (active) {
			auto gradient = QLinearGradient(
				row.rect.topLeft(),
				row.rect.topRight());
			gradient.setColorAt(0., colors.from);
			gradient.setColorAt(1., colors.to);
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawRoundedRect(QRectF(row.rect), Px(12), Px(12));
		} else if (i == _over) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.06));
			p.drawRoundedRect(QRectF(row.rect), Px(12), Px(12));
		}
		const auto tile = QRectF(
			row.rect.x() + Px(8),
			row.rect.y() + (row.rect.height() - Px(28)) / 2.,
			Px(28),
			Px(28));
		auto text = QString();
		auto sub = QString();
		auto color = QColor();
		switch (row.source) {
		case Source::Mine:
			text = tr::lng_tgm_mine(tr::now);
			color = QColor(0xFF, 0x2D, 0x55);
			break;
		case Source::Recent:
			text = tr::lng_tgm_recent(tr::now);
			color = QColor(0x0A, 0x84, 0xFF);
			sub = QString::number(Library::Instance().recent().size());
			break;
		case Source::Playlist:
			if (create) {
				text = tr::lng_tgm_create(tr::now);
				color = Alpha(st::windowFg->c, 0.1);
			} else {
				text = playlists[row.index].name;
				color = LabelColor(row.index + 3);
				sub = QString::number(playlists[row.index].tracks.size());
			}
			break;
		case Source::System:
			text = tr::lng_tgm_system(tr::now);
			color = QColor(0x30, 0xD1, 0x58);
			break;
		case Source::Upload:
			text = tr::lng_tgm_upload(tr::now);
			color = Alpha(st::windowFg->c, 0.1);
			break;
		case Source::Stats:
			text = tr::lng_tgm_stats(tr::now);
			color = QColor(0xBF, 0x5A, 0xF2);
			break;
		}
		const auto cover = (row.source == Source::Playlist && !create)
			? Library::Instance().cover(row.index)
			: QImage();
		p.setPen(Qt::NoPen);
		p.setBrush(active ? QColor(255, 255, 255, 60) : color);
		p.drawRoundedRect(tile, Px(8), Px(8));
		if (!cover.isNull()) {
			p.save();
			auto clip = QPainterPath();
			clip.addRoundedRect(tile, Px(8), Px(8));
			p.setClipPath(clip);
			p.drawImage(tile, cover);
			p.restore();
		}
		p.setPen(QPen(
			(create && !active) ? st::windowFg->c : QColor(255, 255, 255),
			Px(2),
			Qt::SolidLine,
			Qt::RoundCap));
		const auto c = tile.center();
		const auto s = tile.width() * 0.22;
		if (!cover.isNull()) {
		} else if (row.source == Source::Upload) {
			p.drawLine(QPointF(c.x(), c.y() + s), QPointF(c.x(), c.y() - s));
			p.drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x() - s * 0.7, c.y() - s * 0.3));
			p.drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x() + s * 0.7, c.y() - s * 0.3));
			p.drawLine(QPointF(c.x() - s, c.y() + s * 1.2), QPointF(c.x() + s, c.y() + s * 1.2));
		} else if (create) {
			p.drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
			p.drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x(), c.y() + s));
		} else if (row.source == Source::Stats) {
			// Three bars of a chart.
			for (auto k = 0; k != 3; ++k) {
				const auto h = s * (0.8 + 0.6 * ((k * 2) % 3));
				p.drawLine(
					QPointF(c.x() + (k - 1) * s * 0.8, c.y() + s),
					QPointF(c.x() + (k - 1) * s * 0.8, c.y() + s - h));
			}
		} else if (row.source == Source::System) {
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(
				QRectF(c.x() - s * 1.2, c.y() - s * 0.95, s * 2.4, s * 1.5),
				s * 0.2,
				s * 0.2);
			p.drawLine(QPointF(c.x(), c.y() + s * 0.55), QPointF(c.x(), c.y() + s * 1.0));
			p.drawLine(QPointF(c.x() - s * 0.6, c.y() + s * 1.1), QPointF(c.x() + s * 0.6, c.y() + s * 1.1));
		} else if (row.source == Source::Recent) {
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(c, s * 1.25, s * 1.25);
			p.drawLine(c, QPointF(c.x(), c.y() - s * 0.8));
			p.drawLine(c, QPointF(c.x() + s * 0.6, c.y()));
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255));
			p.drawEllipse(QPointF(c.x() - s * 0.4, c.y() + s * 0.7), s * 0.55, s * 0.42);
			p.drawRect(QRectF(c.x() - s * 0.05, c.y() - s * 1.1, s * 0.3, s * 1.8));
			p.drawRect(QRectF(c.x() - s * 0.05, c.y() - s * 1.1, s * 1.1, s * 0.32));
		}
		const auto left = int(tile.right()) + Px(10);
		const auto subWidth = sub.isEmpty()
			? 0
			: (st::normalFont->width(sub) + Px(10));
		p.setFont(active ? st::semiboldFont : st::normalFont);
		p.setPen(active ? QColor(255, 255, 255) : st::windowFg->c);
		const auto available = row.rect.right() - left - subWidth - Px(4);
		p.drawText(
			QRect(left, row.rect.y(), available, row.rect.height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			(active ? st::semiboldFont : st::normalFont)->elided(
				text,
				available));
		if (!sub.isEmpty()) {
			p.setFont(st::normalFont);
			p.setPen(active ? QColor(255, 255, 255, 200) : st::windowSubTextFg->c);
			p.drawText(
				QRect(left, row.rect.y(), row.rect.right() - left - Px(8), row.rect.height()),
				Qt::AlignRight | Qt::AlignVCenter,
				sub);
		}
	}
	const auto small = Font(11, true);
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(Px(20), _labelTop, width() - Px(30), Px(26)),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgm_playlists(tr::now).toUpper());
}

// Tracks of the chosen source.
class TrackList final : public Ui::RpWidget {
public:
	TrackList(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setSource(Source source, int index);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refresh();
	void showTrackMenu(int index);
	[[nodiscard]] int rowAt(QPoint position) const;
	[[nodiscard]] QRect moreRect(int index) const;

	// "On the computer": what Spotify or a browser plays now.
	void requestSystem();
	void paintSystem(QPainter &p);
	void paintStats(QPainter &p);
	[[nodiscard]] QRectF systemCard() const;
	[[nodiscard]] QRectF systemButton(int index) const;
	[[nodiscard]] int systemButtonAt(QPoint position) const;

	const not_null<Window::SessionController*> _controller;
	Source _source = Source::Mine;
	int _index = -1;
	std::vector<Track> _tracks;
	std::optional<SystemMedia::Track> _system;
	base::Timer _systemTimer;
	bool _systemLoading = false;
	bool _systemLoaded = false;
	int _systemOver = -1;
	int _over = -1;
	bool _overMore = false;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Ui::Animations::Basic _animation;

};

TrackList::TrackList(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _systemTimer([=] { requestSystem(); })
, _animation([=] { update(); }) {
	setMouseTracking(true);
	Library::Instance().changes(
	) | rpl::on_next([=] {
		if (_source != Source::Mine && _source != Source::System) {
			refresh();
		}
	}, lifetime());
	const auto session = &controller->session();
	session->data().savedMusic().changed(
	) | rpl::on_next([=] {
		if (_source == Source::Mine) {
			refresh();
		}
	}, lifetime());
	Media::Player::instance()->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &state) {
		return state.id.type() == Type::Song;
	}) | rpl::on_next([=] {
		if (IsPlaying()) {
			if (!_animation.animating()) {
				_animation.start();
			}
		} else {
			_animation.stop();
		}
		update();
	}, lifetime());
	if (IsPlaying()) {
		_animation.start();
	}
}

void TrackList::setSource(Source source, int index) {
	_source = source;
	_index = index;
	if (source == Source::System) {
		requestSystem();
		_systemTimer.callEach(2000);
	} else {
		_systemTimer.cancel();
	}
	if (source == Source::Mine) {
		const auto session = &_controller->session();
		const auto peerId = session->userPeerId();
		if (!session->data().savedMusic().fullyLoaded(peerId)) {
			session->data().savedMusic().loadMore(peerId);
		}
	}
	refresh();
}

void TrackList::refresh() {
	_tracks.clear();
	switch (_source) {
	case Source::Mine: {
		const auto session = &_controller->session();
		for (const auto item : session->data().savedMusic().list(
				session->userPeerId())) {
			if (auto track = TrackFromItem(item)) {
				track->saved = true;
				_tracks.push_back(std::move(*track));
			}
		}
	} break;
	case Source::Recent:
		_tracks = Library::Instance().recent();
		break;
	case Source::Playlist: {
		const auto &playlists = Library::Instance().playlists();
		if (_index >= 0 && _index < int(playlists.size())) {
			_tracks = playlists[_index].tracks;
		}
	} break;
	case Source::System:
	case Source::Upload:
	case Source::Stats:
		break;
	}
	_over = -1;
	resizeToWidth(width());
	update();
}

int TrackList::resizeGetHeight(int newWidth) {
	if (_source == Source::System) {
		return Px(300);
	} else if (_source == Source::Stats) {
		const auto &library = Library::Instance();
		const auto tracks = int(library.topTracks(10).size());
		const auto artists = int(library.topArtists(5).size());
		return Px(130) + Px(40) * 2 + (tracks + artists) * Px(40) + Px(20);
	}
	return std::max(int(_tracks.size()) * Px(54) + Px(12), Px(150));
}

void TrackList::requestSystem() {
	if (_systemLoading || (!isVisible() && _systemLoaded)) {
		return;
	}
	_systemLoading = true;
	SystemMedia::Request(crl::guard(this, [=](
			std::optional<SystemMedia::Track> track) {
		_systemLoading = false;
		_systemLoaded = true;
		_system = std::move(track);
		if (_source == Source::System) {
			update();
		}
	}));
}

void TrackList::paintStats(QPainter &p) {
	const auto &library = Library::Instance();
	const auto colors = CurrentLogoStyle();
	const auto card = QRectF(Px(16), Px(12), width() - Px(32), Px(110));
	{
		auto wash = QLinearGradient(card.topLeft(), card.bottomRight());
		wash.setColorAt(0., Alpha(colors.from, 0.22));
		wash.setColorAt(1., Alpha(colors.to, 0.08));
		p.setPen(Qt::NoPen);
		p.setBrush(wash);
		p.drawRoundedRect(card, Px(20), Px(20));
	}
	const auto small = Font(12);
	const auto big = Font(26, true);
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		card.adjusted(Px(20), Px(14), -Px(20), 0),
		Qt::AlignLeft | Qt::AlignTop,
		tr::lng_tgm_stats_total(tr::now));
	auto title = QLinearGradient(card.topLeft(), card.topRight());
	title.setColorAt(0., colors.from);
	title.setColorAt(1., colors.to);
	p.setFont(big->f);
	p.setPen(QPen(QBrush(title), 1.));
	p.drawText(
		card.adjusted(Px(20), Px(32), -Px(20), 0),
		Qt::AlignLeft | Qt::AlignTop,
		FormatListened(library.statsTotal()));
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		card.adjusted(Px(20), 0, -Px(20), -Px(14)),
		Qt::AlignLeft | Qt::AlignBottom,
		tr::lng_tgm_stats_plays(
			tr::now,
			lt_plays,
			QString::number(library.statsPlays())));

	auto top = int(card.bottom()) + Px(16);
	const auto section = [&](const QString &label) {
		p.setFont(Font(11, true)->f);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(Px(24), top, width() - Px(48), Px(28)),
			Qt::AlignLeft | Qt::AlignVCenter,
			label.toUpper());
		top += Px(32);
	};
	const auto row = [&](
			int index,
			const QString &main,
			const QString &sub,
			int64 ms,
			int64 max) {
		const auto left = Px(24);
		const auto right = width() - Px(24);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, top, Px(24), Px(36)),
			Qt::AlignLeft | Qt::AlignVCenter,
			QString::number(index + 1));
		const auto textLeft = left + Px(28);
		const auto timeText = FormatListened(ms);
		const auto timeWidth = small->width(timeText) + Px(8);
		const auto available = right - textLeft - timeWidth;
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(textLeft, top, available, Px(20)),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(main, available));
		if (!sub.isEmpty()) {
			p.setFont(small->f);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRect(textLeft, top + Px(18), available, Px(16)),
				Qt::AlignLeft | Qt::AlignVCenter,
				small->elided(sub, available));
		}
		p.setFont(small->f);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(right - timeWidth, top, timeWidth, Px(20)),
			Qt::AlignRight | Qt::AlignVCenter,
			timeText);
		// A thin bar of the share of the leader.
		const auto share = max ? (ms / double(max)) : 0.;
		const auto bar = QRectF(right - Px(60), top + Px(24), Px(60), Px(4));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.1));
		p.drawRoundedRect(bar, Px(2), Px(2));
		auto fill = QLinearGradient(bar.topLeft(), bar.topRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setBrush(fill);
		p.drawRoundedRect(
			QRectF(bar.x(), bar.y(), bar.width() * share, bar.height()),
			Px(2),
			Px(2));
		top += Px(40);
	};
	const auto tracks = library.topTracks(10);
	if (tracks.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(Px(24), top, width() - Px(48), Px(60)),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			tr::lng_tgm_stats_empty(tr::now));
		return;
	}
	section(tr::lng_tgm_stats_top_tracks(tr::now));
	for (auto i = 0; i != int(tracks.size()); ++i) {
		const auto &entry = tracks[i];
		row(i, entry.title, entry.performer, entry.ms, tracks.front().ms);
	}
	const auto artists = library.topArtists(5);
	section(tr::lng_tgm_stats_top_artists(tr::now));
	for (auto i = 0; i != int(artists.size()); ++i) {
		const auto &entry = artists[i];
		row(
			i,
			entry.performer,
			tr::lng_tgm_stats_plays(
				tr::now,
				lt_plays,
				QString::number(entry.plays)),
			entry.ms,
			artists.front().ms);
	}
}

QRectF TrackList::systemCard() const {
	return QRectF(Px(16), Px(12), width() - Px(32), Px(220));
}

QRectF TrackList::systemButton(int index) const {
	const auto card = systemCard();
	const auto cover = card.height() - Px(32);
	const auto left = card.x() + Px(16) + cover + Px(24);
	const auto play = Px(48);
	const auto small = Px(36);
	const auto y = card.bottom() - Px(16) - play;
	switch (index) {
	case 0: return QRectF(left, y + (play - small) / 2., small, small);
	case 1: return QRectF(left + small + Px(12), y, play, play);
	case 2: return QRectF(left + small + play + Px(24), y + (play - small) / 2., small, small);
	}
	return QRectF();
}

int TrackList::systemButtonAt(QPoint position) const {
	if (!_system) {
		return -1;
	}
	for (auto i = 0; i != 3; ++i) {
		if (systemButton(i).contains(position)) {
			return i;
		}
	}
	return -1;
}

void TrackList::paintSystem(QPainter &p) {
	const auto colors = CurrentLogoStyle();
	const auto card = systemCard();
	if (!_system) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect().marginsRemoved({ Px(24), Px(20), Px(24), Px(20) }),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			_systemLoaded
				? tr::lng_tgm_system_empty(tr::now)
				: tr::lng_tgm_system_loading(tr::now));
		return;
	}
	const auto &track = *_system;
	{
		auto wash = QLinearGradient(card.topLeft(), card.bottomRight());
		wash.setColorAt(0., Alpha(colors.from, 0.16));
		wash.setColorAt(1., Alpha(colors.to, 0.06));
		p.setPen(Qt::NoPen);
		p.setBrush(wash);
		p.drawRoundedRect(card, Px(20), Px(20));
	}
	const auto side = card.height() - Px(32);
	const auto coverRect = QRectF(card.x() + Px(16), card.y() + Px(16), side, side);
	{
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(coverRect, Px(14), Px(14));
		p.setClipPath(clip);
		if (!track.cover.isNull()) {
			const auto &cover = track.cover;
			const auto min = std::min(cover.width(), cover.height());
			p.drawImage(
				coverRect,
				cover,
				QRectF(
					(cover.width() - min) / 2.,
					(cover.height() - min) / 2.,
					min,
					min));
		} else {
			auto gradient = QLinearGradient(coverRect.topLeft(), coverRect.bottomRight());
			gradient.setColorAt(0., colors.from);
			gradient.setColorAt(1., colors.to);
			p.fillRect(coverRect, gradient);
		}
		p.restore();
	}
	const auto left = int(coverRect.right()) + Px(24);
	const auto available = int(card.right()) - left - Px(16);
	const auto small = Font(12);
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(left, int(card.y()) + Px(18), available, small->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		small->elided(
			tr::lng_tgm_system_from(tr::now) + ' ' + track.app,
			available));
	const auto big = Font(18, true);
	p.setFont(big->f);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(left, int(card.y()) + Px(24) + small->height, available, big->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		big->elided(
			track.title.isEmpty() ? tr::lng_tgm_unknown(tr::now) : track.title,
			available));
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(
			left,
			int(card.y()) + Px(28) + small->height + big->height,
			available,
			st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(
			track.artist.isEmpty()
				? tr::lng_tgm_unknown_artist(tr::now)
				: track.artist,
			available));
	if (track.duration > 0) {
		const auto y = card.y() + Px(36) + small->height + big->height
			+ st::normalFont->height + Px(8);
		const auto line = QRectF(left, y, available, Px(4));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.12));
		p.drawRoundedRect(line, Px(2), Px(2));
		auto played = QLinearGradient(line.topLeft(), line.topRight());
		played.setColorAt(0., colors.from);
		played.setColorAt(1., colors.to);
		p.setBrush(played);
		const auto progress = std::clamp(
			track.position / double(track.duration),
			0.,
			1.);
		p.drawRoundedRect(
			QRectF(line.x(), line.y(), line.width() * progress, line.height()),
			Px(2),
			Px(2));
	}
	for (auto i = 0; i != 3; ++i) {
		const auto r = systemButton(i);
		if (i == 1) {
			auto gradient = QLinearGradient(r.topLeft(), r.bottomRight());
			gradient.setColorAt(0., colors.from);
			gradient.setColorAt(1., colors.to);
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawEllipse(r);
			PaintPlayIcon(
				p,
				r.marginsRemoved(QMarginsF(Px(13), Px(13), Px(13), Px(13))),
				track.playing,
				QColor(255, 255, 255));
		} else {
			if (_systemOver == i) {
				p.setPen(Qt::NoPen);
				p.setBrush(Alpha(st::windowFg->c, 0.08));
				p.drawEllipse(r);
			}
			PaintSkipIcon(
				p,
				r.marginsRemoved(QMarginsF(Px(9), Px(9), Px(9), Px(9))),
				(i == 2),
				st::windowFg->c);
		}
	}
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRectF(card.x(), card.bottom() + Px(12), card.width(), Px(60)),
		Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
		tr::lng_tgm_system_about(tr::now));
}

int TrackList::rowAt(QPoint position) const {
	if (position.y() < 0 || position.x() < 0 || position.x() > width()) {
		return -1;
	}
	const auto index = position.y() / Px(54);
	return (index < int(_tracks.size())) ? index : -1;
}

QRect TrackList::moreRect(int index) const {
	const auto size = Px(30);
	return QRect(
		width() - Px(16) - size,
		index * Px(54) + (Px(54) - size) / 2,
		size,
		size);
}

void TrackList::mouseMoveEvent(QMouseEvent *e) {
	if (_source == Source::System) {
		const auto over = systemButtonAt(e->pos());
		if (_systemOver != over) {
			_systemOver = over;
			setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
			update();
		}
		return;
	}
	const auto over = rowAt(e->pos());
	const auto overMore = (over >= 0) && moreRect(over).contains(e->pos());
	if (_over != over || _overMore != overMore) {
		_over = over;
		_overMore = overMore;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void TrackList::leaveEventHook(QEvent *e) {
	_over = -1;
	_overMore = false;
	update();
}

void TrackList::mousePressEvent(QMouseEvent *e) {
	if (_source == Source::System) {
		const auto button = systemButtonAt(e->pos());
		if (button < 0 || e->button() != Qt::LeftButton) {
			return;
		}
		using Command = SystemMedia::Command;
		SystemMedia::Send((button == 0)
			? Command::Previous
			: (button == 1)
			? Command::Toggle
			: Command::Next);
		if (button == 1 && _system) {
			_system->playing = !_system->playing;
			update();
		}
		base::call_delayed(400, this, [=] { requestSystem(); });
		return;
	}
	const auto index = rowAt(e->pos());
	if (index < 0) {
		return;
	}
	if (e->button() == Qt::RightButton || moreRect(index).contains(e->pos())) {
		showTrackMenu(index);
		return;
	}
	Library::Instance().playQueue(&_controller->session(), _tracks, index);
}

void TrackList::showTrackMenu(int index) {
	if (index < 0 || index >= int(_tracks.size())) {
		return;
	}
	const auto track = _tracks[index];
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(playlists.size()); ++i) {
		if (_source == Source::Playlist && i == _index) {
			continue;
		}
		_menu->addAction(
			tr::lng_tgm_add_to(tr::now) + u" «"_q + playlists[i].name + u"»"_q,
			[=] { Library::Instance().add(i, track); },
			&st::menuIconAddToFolder);
	}
	const auto controller = _controller;
	_menu->addAction(tr::lng_tgm_new_with(tr::now), [=] {
		ShowPlaylistNameBox(controller, QString(), [=](QString name) {
			Library::Instance().create(name);
			Library::Instance().add(
				int(Library::Instance().playlists().size()) - 1,
				track);
		});
	}, &st::menuIconAdd);
	if (_source == Source::Playlist) {
		const auto playlist = _index;
		_menu->addAction(tr::lng_tgm_remove_from(tr::now), [=] {
			Library::Instance().removeTrack(playlist, index);
		}, &st::menuIconDelete);
	}
	if (!track.saved && track.peer) {
		_menu->addAction(tr::lng_tgm_show_in_chat(tr::now), [=] {
			const auto session = &controller->session();
			controller->showPeerHistory(
				session->data().peer(track.peer),
				Window::SectionShow::Way::Forward,
				track.msg);
		}, &st::menuIconShowInChat);
	}
	_menu->popup(QCursor::pos());
}

void TrackList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto now = crl::now();
	if (_source == Source::System) {
		paintSystem(p);
		return;
	} else if (_source == Source::Stats) {
		paintStats(p);
		return;
	}
	if (_tracks.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto text = (_source == Source::Mine)
			? tr::lng_tgm_empty_mine(tr::now)
			: (_source == Source::Recent)
			? tr::lng_tgm_empty_recent(tr::now)
			: tr::lng_tgm_empty_playlist(tr::now);
		p.drawText(
			rect().marginsRemoved({ Px(24), Px(20), Px(24), Px(20) }),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			text);
		return;
	}
	const auto current = CurrentTrack();
	const auto playing = IsPlaying();
	const auto small = Font(12);
	for (auto i = 0; i != int(_tracks.size()); ++i) {
		const auto &track = _tracks[i];
		const auto row = QRect(Px(8), i * Px(54), width() - Px(16), Px(54));
		const auto isCurrent = current && SameTrack(*current, track);
		if (isCurrent) {
			const auto colors = CurrentLogoStyle();
			auto gradient = QLinearGradient(row.topLeft(), row.topRight());
			gradient.setColorAt(0., Alpha(colors.from, 0.18));
			gradient.setColorAt(1., Alpha(colors.to, 0.06));
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawRoundedRect(QRectF(row).marginsRemoved({ 0, 2, 0, 2 }), Px(12), Px(12));
		} else if (i == _over) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.05));
			p.drawRoundedRect(QRectF(row).marginsRemoved({ 0, 2, 0, 2 }), Px(12), Px(12));
		}
		const auto disc = QRectF(
			row.x() + Px(8),
			row.y() + (row.height() - Px(38)) / 2.,
			Px(38),
			Px(38));
		PaintVinyl(p, disc, QImage(), isCurrent && playing ? (now / 10 % 360) : 0., track.document);
		const auto left = int(disc.right()) + Px(12);
		const auto right = moreRect(i).x() - Px(8);
		const auto durationText = track.duration
			? FormatTime(crl::time(track.duration) * 1000)
			: QString();
		const auto durationWidth = durationText.isEmpty()
			? 0
			: (small->width(durationText) + Px(10));
		auto textRight = right - durationWidth;
		if (isCurrent) {
			PaintEqualizer(
				p,
				QRectF(textRight - Px(16), row.y() + Px(19), Px(14), Px(16)),
				now,
				playing);
			textRight -= Px(22);
		}
		const auto textWidth = std::max(textRight - left, Px(40));
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(left, row.y() + Px(9), textWidth, st::semiboldFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(track.title, textWidth));
		p.setFont(small->f);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(
				left,
				row.y() + Px(11) + st::semiboldFont->height,
				textWidth,
				small->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			small->elided(
				track.performer.isEmpty()
					? tr::lng_tgm_unknown_artist(tr::now)
					: track.performer,
				textWidth));
		if (!durationText.isEmpty()) {
			p.drawText(
				QRect(right - durationWidth, row.y(), durationWidth, row.height()),
				Qt::AlignRight | Qt::AlignVCenter,
				durationText);
		}
		if (i == _over) {
			const auto more = moreRect(i);
			if (_overMore) {
				p.setPen(Qt::NoPen);
				p.setBrush(Alpha(st::windowFg->c, 0.1));
				p.drawEllipse(QRectF(more));
			}
			PaintMoreIcon(p, QRectF(more), st::windowSubTextFg->c);
		}
	}
}

// The whole window: sidebar, the record with controls, the list.
class MusicWindow final : public Ui::RpWidget {
public:
	MusicWindow(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close,
		int height);

protected:
	int resizeGetHeight(int newWidth) override {
		return _height;
	}
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Button {
		None,
		Previous,
		Play,
		Next,
		Lyrics,
		Skin,
		Wave,
	};
	void updateTrack();
	[[nodiscard]] QRect heroRect() const;
	[[nodiscard]] QRectF discRect() const;
	[[nodiscard]] QRectF waveRect() const;
	[[nodiscard]] QRectF buttonRect(Button button) const;
	[[nodiscard]] Button buttonAt(QPoint position) const;
	[[nodiscard]] crl::time position() const;
	[[nodiscard]] crl::time length() const;

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	const int _height = 0;
	const int _navWidth = 0;
	const not_null<Ui::ScrollArea*> _navScroll;
	const not_null<Sidebar*> _sidebar;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<TrackList*> _list;
	const not_null<Ui::IconButton*> _closeButton;
	Media::Player::TrackState _state;
	std::optional<Track> _track;
	Cover _cover;
	Ui::Animations::Basic _spin;
	Ui::Animations::Simple _arm;
	double _armTarget = 0.;
	crl::time _lastSpin = 0;
	double _angle = 0.;
	crl::time _lastPlayed = 0;
	double _played = 0.;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Button _over = Button::None;
	Button _pressed = Button::None;
	double _hover = -1.;
	double _seek = -1.;

};

MusicWindow::MusicWindow(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close,
	int height)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _height(height)
, _navWidth(Px(220))
, _navScroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _sidebar(_navScroll->setOwnedWidget(
	object_ptr<Sidebar>(_navScroll.get(), controller)))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _list(_scroll->setOwnedWidget(
	object_ptr<TrackList>(_scroll.get(), controller)))
, _closeButton(Ui::CreateChild<Ui::IconButton>(this, st::boxTitleClose))
, _spin([=] {
	update(heroRect());
}) {
	setMouseTracking(true);
	Library::Instance().ensureTracking();
	_closeButton->setClickedCallback([=] { _close(); });

	_sidebar->chosen(
	) | rpl::on_next([=](std::pair<Source, int> chosen) {
		_list->setSource(chosen.first, chosen.second);
		_scroll->scrollToY(0);
		Extera::Config::Instance().setInt(
			u"tristegram_music_source"_q,
			(chosen.first == Source::Mine)
				? 0
				: (chosen.first == Source::Recent)
				? 1
				: (chosen.first == Source::System)
				? kSystemSourceSaved
				: (chosen.first == Source::Stats)
				? kStatsSourceSaved
				: (2 + chosen.second));
	}, lifetime());

	const auto instance = Media::Player::instance();
	instance->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &state) {
		return state.id.type() == Type::Song;
	}) | rpl::on_next([=](const Media::Player::TrackState &state) {
		_state = state;
		updateTrack();
	}, lifetime());
	instance->trackChanged(
	) | rpl::filter(
		rpl::mappers::_1 == Type::Song
	) | rpl::on_next([=] {
		updateTrack();
	}, lifetime());
	controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (_cover.check()) {
			update(heroRect());
		}
	}, lifetime());
	_state = instance->getState(Type::Song);
	updateTrack();

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		_navScroll->setGeometry(0, Px(56), _navWidth, size.height() - Px(56));
		_sidebar->resizeToWidth(_navWidth);
		const auto hero = heroRect();
		_scroll->setGeometry(
			_navWidth,
			hero.y() + hero.height(),
			size.width() - _navWidth,
			size.height() - hero.y() - hero.height());
		_list->resizeToWidth(_scroll->width());
		_closeButton->moveToRight(Px(10), Px(10));
	}, lifetime());

	const auto saved = Extera::Int(u"tristegram_music_source"_q);
	const auto playlists = int(Library::Instance().playlists().size());
	if (saved == 1) {
		_sidebar->choose(Source::Recent, -1);
	} else if (saved == kStatsSourceSaved) {
		_sidebar->choose(Source::Stats, -1);
	} else if (saved == kSystemSourceSaved && SystemMedia::Supported()) {
		_sidebar->choose(Source::System, -1);
	} else if (saved >= 2 && saved - 2 < playlists) {
		_sidebar->choose(Source::Playlist, saved - 2);
	} else {
		_sidebar->choose(Source::Mine, -1);
	}
}

void MusicWindow::updateTrack() {
	_track = CurrentTrack();
	const auto current = Media::Player::instance()->current(Type::Song);
	_cover.set(current.audio(), current.contextId());
	const auto playing = IsPlaying();
	if (playing && !_spin.animating()) {
		_spin.start();
	} else if (!playing && _spin.animating()) {
		_spin.stop();
	}
	const auto armTo = playing ? 1. : 0.;
	if (_armTarget != armTo) {
		_arm.start(
			[=] { update(heroRect()); },
			_armTarget,
			armTo,
			500,
			anim::easeOutCubic);
		_armTarget = armTo;
	}
	update();
}

QRect MusicWindow::heroRect() const {
	return QRect(_navWidth, Px(56), width() - _navWidth, Px(220));
}

QRectF MusicWindow::discRect() const {
	const auto hero = heroRect();
	const auto size = Px(184);
	return QRectF(
		hero.x() + Px(24),
		hero.y() + (hero.height() - size) / 2.,
		size,
		size);
}

QRectF MusicWindow::waveRect() const {
	const auto hero = heroRect();
	const auto disc = discRect();
	const auto left = disc.right() + Px(40);
	return QRectF(
		left,
		hero.y() + Px(92),
		hero.right() - left - Px(24),
		Px(52));
}

QRectF MusicWindow::buttonRect(Button button) const {
	const auto wave = waveRect();
	const auto y = wave.bottom() + Px(26);
	const auto play = Px(48);
	const auto small = Px(36);
	const auto center = wave.x() + Px(96);
	switch (button) {
	case Button::Previous:
		return QRectF(center - play / 2. - Px(10) - small, y + (play - small) / 2., small, small);
	case Button::Play:
		return QRectF(center - play / 2., y, play, play);
	case Button::Next:
		return QRectF(center + play / 2. + Px(10), y + (play - small) / 2., small, small);
	case Button::Lyrics:
		return QRectF(wave.right() - small, y + (play - small) / 2., small, small);
	case Button::Skin:
		return QRectF(wave.right() - small * 2 - Px(8), y + (play - small) / 2., small, small);
	case Button::Wave:
		return wave;
	case Button::None:
		break;
	}
	return QRectF();
}

MusicWindow::Button MusicWindow::buttonAt(QPoint position) const {
	for (const auto button : {
			Button::Previous,
			Button::Play,
			Button::Next,
			Button::Lyrics,
			Button::Skin,
			Button::Wave }) {
		if (buttonRect(button).contains(position)) {
			return button;
		}
	}
	return Button::None;
}

crl::time MusicWindow::length() const {
	if (_state.length > 0 && _state.frequency > 0) {
		return _state.length * 1000 / _state.frequency;
	}
	return _track ? crl::time(_track->duration) * 1000 : 0;
}

crl::time MusicWindow::position() const {
	if (_seek >= 0.) {
		return crl::time(_seek * length());
	}
	return (_state.frequency > 0)
		? (_state.position * 1000 / _state.frequency)
		: 0;
}

void MusicWindow::mouseMoveEvent(QMouseEvent *e) {
	const auto over = buttonAt(e->pos());
	const auto wave = waveRect();
	const auto hover = (over == Button::Wave || _pressed == Button::Wave)
		? std::clamp((e->pos().x() - wave.x()) / wave.width(), 0., 1.)
		: -1.;
	if (_pressed == Button::Wave) {
		_seek = hover;
	}
	if (over != _over || hover != _hover) {
		_over = over;
		_hover = hover;
		setCursor((over != Button::None) ? style::cur_pointer : style::cur_default);
		update(heroRect());
	}
}

void MusicWindow::leaveEventHook(QEvent *e) {
	_over = Button::None;
	_hover = -1.;
	update(heroRect());
}

void MusicWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_pressed = buttonAt(e->pos());
	if (_pressed == Button::Wave && length() > 0) {
		const auto wave = waveRect();
		_seek = std::clamp((e->pos().x() - wave.x()) / wave.width(), 0., 1.);
		Media::Player::instance()->startSeeking(Type::Song);
		update(heroRect());
	}
}

void MusicWindow::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, Button::None);
	const auto instance = Media::Player::instance();
	if (pressed == Button::Wave) {
		if (_seek >= 0.) {
			instance->finishSeeking(Type::Song, _seek);
		}
		_seek = -1.;
		update(heroRect());
		return;
	}
	if (pressed == Button::None || pressed != buttonAt(e->pos())) {
		return;
	}
	switch (pressed) {
	case Button::Previous:
		if (!Library::Instance().queueStep(-1)) {
			instance->previous(Type::Song);
		}
		break;
	case Button::Play:
		if (instance->current(Type::Song)) {
			instance->playPause(Type::Song);
		} else if (const auto session = &_controller->session()) {
			const auto &recent = Library::Instance().recent();
			if (!recent.empty()) {
				Library::Instance().playQueue(session, recent, 0);
			}
		}
		break;
	case Button::Next:
		if (!Library::Instance().queueStep(1)) {
			instance->next(Type::Song);
		}
		break;
	case Button::Lyrics: {
		const auto controller = _controller;
		const auto close = _close;
		close();
		Extera::FullPlayer::Show(controller);
	} break;
	case Button::Skin: {
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		const auto current = MusicSkins::Current();
		for (auto i = 0; i != MusicSkins::kCount; ++i) {
			const auto skin = MusicSkins::Skin(i);
			_menu->addAction(
				MusicSkins::Name(skin),
				crl::guard(this, [=] {
					MusicSkins::Choose(skin);
					update();
				}),
				(skin == current) ? &st::menuIconSelect : nullptr);
		}
		_menu->popup(QCursor::pos());
	} break;
	default: break;
	}
}

void MusicWindow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto now = crl::now();

	// Sidebar backing and the header with the title.
	p.fillRect(QRect(0, 0, _navWidth, height()), Alpha(st::windowFg->c, 0.025));
	p.fillRect(QRect(_navWidth, 0, st::lineWidth, height()), Alpha(st::windowFg->c, 0.07));
	{
		const auto tile = QRectF(Px(18), Px(12), Px(32), Px(32));
		auto gradient = QLinearGradient(tile.topLeft(), tile.bottomRight());
		gradient.setColorAt(0., colors.from);
		gradient.setColorAt(1., colors.to);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(tile, Px(10), Px(10));
		p.setBrush(QColor(255, 255, 255));
		const auto c = tile.center();
		const auto s = tile.width() * 0.22;
		p.drawEllipse(QPointF(c.x() - s * 0.4, c.y() + s * 0.7), s * 0.55, s * 0.42);
		p.drawRect(QRectF(c.x() - s * 0.05, c.y() - s * 1.1, s * 0.3, s * 1.8));
		p.drawRect(QRectF(c.x() - s * 0.05, c.y() - s * 1.1, s * 1.1, s * 0.32));
		p.setFont(st::boxTitle.style.font);
		p.setPen(st::boxTitleFg);
		p.drawText(
			QRect(int(tile.right()) + Px(10), 0, _navWidth, Px(56)),
			Qt::AlignLeft | Qt::AlignVCenter,
			tr::lng_tgm_title(tr::now));
	}

	// The hero: a soft wash of the logo colors.
	const auto hero = heroRect();
	{
		auto wash = QLinearGradient(hero.topLeft(), hero.bottomRight());
		wash.setColorAt(0., Alpha(colors.from, 0.16));
		wash.setColorAt(1., Alpha(colors.to, 0.06));
		p.setPen(Qt::NoPen);
		p.setBrush(wash);
		p.drawRoundedRect(
			QRectF(hero).marginsRemoved(QMarginsF(Px(12), 0, Px(12), Px(8))),
			Px(20),
			Px(20));
	}

	const auto disc = discRect();
	const auto angle = SpinAngle(now, _lastSpin, _angle);
	// A colored glow under the record.
	{
		auto glow = QRadialGradient(disc.center(), disc.width() * 0.62);
		glow.setColorAt(0., Alpha(colors.from, 0.35));
		glow.setColorAt(1., Alpha(colors.to, 0.));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(disc.center(), disc.width() * 0.62, disc.width() * 0.62);
	}
	if (IsPlaying()) {
		if (_lastPlayed) {
			_played += (now - _lastPlayed) / 1000.;
		}
		_lastPlayed = now;
	} else {
		_lastPlayed = 0;
	}
	const auto skin = MusicSkins::Current();
	if (skin != MusicSkins::Skin::Vinyl) {
		const auto total = length();
		MusicSkins::Paint(p, disc, skin, {
			.cover = _cover.image(),
			.title = _track ? _track->title : QString(),
			.from = colors.from,
			.to = colors.to,
			.seconds = _played,
			.progress = (total > 0)
				? std::clamp(position() / double(total), 0., 1.)
				: 0.,
			.playing = IsPlaying(),
			.seed = _track ? _track->document : 0,
		});
	} else {
		PaintVinyl(
			p,
			disc,
			_cover.image(),
			angle,
			_track ? _track->document : 0);
	}

	// The tone arm moves onto the record while it plays.
	if (skin == MusicSkins::Skin::Vinyl) {
		const auto arm = _arm.value(_armTarget);
		const auto pivot = QPointF(
			disc.right() + Px(8),
			disc.top() + Px(14));
		const auto rest = -8.;
		const auto play = 22.;
		const auto turn = rest + (play - rest) * arm;
		p.save();
		p.translate(pivot);
		p.rotate(turn);
		const auto length = disc.height() * 0.78;
		p.setPen(QPen(
			QColor(0xC8, 0xC8, 0xD0),
			Px(4),
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(QPointF(0, 0), QPointF(-Px(4), length));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0xE6, 0xE6, 0xEC));
		p.drawRoundedRect(
			QRectF(-Px(4) - Px(7), length - Px(4), Px(14), Px(20)),
			Px(3),
			Px(3));
		p.setBrush(QColor(0x9A, 0x9A, 0xA4));
		p.drawEllipse(QPointF(0, 0), Px(9), Px(9));
		p.setBrush(QColor(0xE6, 0xE6, 0xEC));
		p.drawEllipse(QPointF(0, 0), Px(5), Px(5));
		p.restore();
	}

	// Title, artist, the wave and times.
	const auto wave = waveRect();
	const auto textLeft = int(wave.x());
	const auto textWidth = int(wave.width()) - Px(40);
	const auto big = Font(20, true);
	p.setFont(big->f);
	p.setPen(st::windowBoldFg);
	const auto title = _track
		? _track->title
		: tr::lng_tgm_nothing(tr::now);
	p.drawText(
		QRect(textLeft, hero.y() + Px(26), textWidth, big->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		big->elided(title, textWidth));
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	const auto artist = !_track
		? tr::lng_tgm_pick(tr::now)
		: _track->performer.isEmpty()
		? tr::lng_tgm_unknown_artist(tr::now)
		: _track->performer;
	p.drawText(
		QRect(textLeft, hero.y() + Px(30) + big->height, textWidth, st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(artist, textWidth));

	const auto total = length();
	const auto progress = (total > 0)
		? std::clamp(position() / double(total), 0., 1.)
		: 0.;
	PaintWave(
		p,
		wave,
		_track ? _track->document : 0,
		progress,
		_hover);
	const auto small = Font(11);
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRectF(wave.x(), wave.bottom() + Px(2), wave.width(), small->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		FormatTime(position()));
	p.drawText(
		QRectF(wave.x(), wave.bottom() + Px(2), wave.width(), small->height),
		Qt::AlignRight | Qt::AlignVCenter,
		FormatTime(total));

	// Controls.
	const auto playRect = buttonRect(Button::Play);
	{
		auto glow = colors.from;
		glow.setAlphaF(_over == Button::Play ? 0.35 : 0.22);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(playRect.marginsAdded({ 4, 4, 4, 4 }));
		auto gradient = QLinearGradient(playRect.topLeft(), playRect.bottomRight());
		gradient.setColorAt(0., colors.from);
		gradient.setColorAt(1., colors.to);
		p.setBrush(gradient);
		p.drawEllipse(playRect);
		PaintPlayIcon(
			p,
			playRect.marginsRemoved(QMarginsF(Px(13), Px(13), Px(13), Px(13))),
			IsPlaying(),
			QColor(255, 255, 255));
	}
	for (const auto button : { Button::Previous, Button::Next, Button::Lyrics, Button::Skin }) {
		const auto r = buttonRect(button);
		if (_over == button) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.08));
			p.drawEllipse(r);
		}
		const auto inner = r.marginsRemoved(QMarginsF(Px(9), Px(9), Px(9), Px(9)));
		if (button == Button::Skin) {
			// Four tiles: the look of the player.
			const auto half = inner.width() / 2.;
			const auto gap = Px(2);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowSubTextFg);
			for (auto k = 0; k != 4; ++k) {
				p.drawRoundedRect(
					QRectF(
						inner.x() + (k % 2) * half + gap / 2.,
						inner.y() + (k / 2) * half + gap / 2.,
						half - gap,
						half - gap),
					Px(2),
					Px(2));
			}
		} else if (button == Button::Lyrics) {
			// Lines of text with a note: the full player with lyrics.
			p.setPen(QPen(st::windowSubTextFg->c, Px(2), Qt::SolidLine, Qt::RoundCap));
			for (auto i = 0; i != 3; ++i) {
				const auto y = inner.y() + inner.height() * (0.2 + 0.3 * i);
				p.drawLine(
					QPointF(inner.x(), y),
					QPointF(inner.x() + inner.width() * (i == 1 ? 0.55 : 0.85), y));
			}
		} else {
			PaintSkipIcon(p, inner, button == Button::Next, st::windowFg->c);
		}
	}
}

// Volume from 0 to 1000%: up to 100% it is the usual song volume,
// above it the song volume stays full and the boost grows.
[[nodiscard]] int VolumePercent() {
	const auto boost = std::clamp(
		Extera::Int(u"tristegram_song_boost"_q),
		100,
		1000);
	if (boost > 100) {
		return boost;
	}
	return int(std::round(Core::App().settings().songVolume() * 100.));
}

void SetVolumePercent(int percent) {
	percent = std::clamp(percent, 0, 1000);
	auto &settings = Core::App().settings();
	const auto boost = std::max(percent, 100);
	const auto volume = std::min(percent, 100) / 100.;
	Extera::Config::Instance().setInt(u"tristegram_song_boost"_q, boost);
	SetSongBoost(boost);
	// The mixer takes new gains when the song volume changes.
	settings.setSongVolume(std::clamp(volume - 0.001, 0., 1.));
	settings.setSongVolume(volume);
	Core::App().saveSettingsDelayed();
}

void PaintSpeakerIcon(QPainter &p, QRectF rect, int percent, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	const auto c = rect.center();
	const auto s = rect.width() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	auto body = QPainterPath();
	body.moveTo(c.x() - s * 0.85, c.y() - s * 0.3);
	body.lineTo(c.x() - s * 0.45, c.y() - s * 0.3);
	body.lineTo(c.x() + s * 0.05, c.y() - s * 0.75);
	body.lineTo(c.x() + s * 0.05, c.y() + s * 0.75);
	body.lineTo(c.x() - s * 0.45, c.y() + s * 0.3);
	body.lineTo(c.x() - s * 0.85, c.y() + s * 0.3);
	body.closeSubpath();
	p.drawPath(body);
	p.setBrush(Qt::NoBrush);
	auto pen = QPen(color, std::max(1.5, s * 0.16), Qt::SolidLine, Qt::RoundCap);
	p.setPen(pen);
	if (!percent) {
		p.drawLine(
			QPointF(c.x() + s * 0.35, c.y() - s * 0.35),
			QPointF(c.x() + s * 0.95, c.y() + s * 0.35));
		p.drawLine(
			QPointF(c.x() + s * 0.95, c.y() - s * 0.35),
			QPointF(c.x() + s * 0.35, c.y() + s * 0.35));
		return;
	}
	const auto waves = (percent > 100) ? 3 : (percent > 50) ? 2 : 1;
	for (auto i = 0; i != waves; ++i) {
		const auto r = s * (0.45 + 0.3 * i);
		if (i == 2) {
			// Over 100%: the last wave glows in the logo colors.
			const auto colors = CurrentLogoStyle();
			pen.setColor(colors.to);
			p.setPen(pen);
		}
		p.drawArc(
			QRectF(c.x() - r + s * 0.15, c.y() - r, r * 2, r * 2),
			-50 * 16,
			100 * 16);
	}
}

// A floating capsule with the volume slider from 0 to 1000%.
class VolumePopup final : public Ui::RpWidget {
public:
	VolumePopup() : RpWidget(nullptr) {
		setWindowFlags(Qt::Popup
			| Qt::FramelessWindowHint
			| Qt::NoDropShadowWindowHint);
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_DeleteOnClose);
		setMouseTracking(true);
		resize(Px(300), Px(64));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto colors = CurrentLogoStyle();
		const auto r = QRectF(rect()).marginsRemoved({ 2., 2., 2., 2. });
		p.setPen(QPen(Alpha(st::windowFg->c, 0.1), 1.));
		p.setBrush(st::windowBg);
		p.drawRoundedRect(r, r.height() / 2., r.height() / 2.);

		const auto percent = VolumePercent();
		PaintSpeakerIcon(
			p,
			QRectF(Px(16), (height() - Px(22)) / 2., Px(22), Px(22)),
			percent,
			st::windowFg->c);
		const auto track = trackRect();
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.12));
		p.drawRoundedRect(track, track.height() / 2., track.height() / 2.);
		// The usual range and the boost part, marked at 100%.
		const auto at100 = track.x() + track.width() * 0.1;
		const auto filled = track.width() * percent / 1000.;
		auto fill = QLinearGradient(track.topLeft(), track.topRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setBrush(fill);
		p.drawRoundedRect(
			QRectF(track.x(), track.y(), filled, track.height()),
			track.height() / 2.,
			track.height() / 2.);
		p.setBrush(Alpha(st::windowFg->c, 0.5));
		p.drawRect(QRectF(at100 - 0.5, track.y() - Px(3), 1., track.height() + Px(6)));
		const auto knob = QPointF(track.x() + filled, track.center().y());
		p.setBrush(QColor(255, 255, 255));
		p.setPen(QPen(colors.to, 2.));
		p.drawEllipse(knob, Px(8), Px(8));

		p.setPen((percent > 100) ? QPen(colors.to) : QPen(st::windowFg->c));
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(width() - Px(68), 0, Px(56), height()),
			Qt::AlignRight | Qt::AlignVCenter,
			QString::number(percent) + '%');
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (trackRect().marginsAdded({ 6., 14., 6., 14. }).contains(e->pos())) {
			_dragging = true;
			applyAt(e->pos().x());
		}
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		if (_dragging) {
			applyAt(e->pos().x());
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		_dragging = false;
	}
	void wheelEvent(QWheelEvent *e) override {
		const auto delta = e->angleDelta().y();
		if (delta) {
			SetVolumePercent(VolumePercent() + ((delta > 0) ? 10 : -10));
			update();
		}
	}

private:
	[[nodiscard]] QRectF trackRect() const {
		return QRectF(
			Px(52),
			(height() - Px(6)) / 2.,
			width() - Px(52) - Px(80),
			Px(6));
	}
	void applyAt(int x) {
		const auto track = trackRect();
		const auto value = std::clamp(
			(x - track.x()) / track.width(),
			0.,
			1.);
		auto percent = int(std::round(value * 1000.));
		// A little stickiness at 100%, so the usual level is easy to hit.
		if (std::abs(percent - 100) <= 12) {
			percent = 100;
		}
		SetVolumePercent(percent);
		update();
	}

	bool _dragging = false;

};

// The capsule in the chat header.
class NowPlayingPill final : public Ui::RpWidget {
public:
	NowPlayingPill(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Part {
		None,
		Body,
		Previous,
		Play,
		Next,
		Volume,
	};
	void updateTrack();
	void showVolume();
	[[nodiscard]] QRectF discRect() const;
	[[nodiscard]] QRectF partRect(Part part) const;
	[[nodiscard]] Part partAt(QPoint position) const;

	const not_null<Window::SessionController*> _controller;
	std::optional<Track> _track;
	Cover _cover;
	base::Timer _spin;
	crl::time _lastSpin = 0;
	double _angle = 0.;
	Part _over = Part::None;
	Part _pressed = Part::None;
	Media::Player::TrackState _state;
	crl::time _volumeShownUntil = 0;
	QPointer<VolumePopup> _volume;

};

NowPlayingPill::NowPlayingPill(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _spin([=] {
	// 30 frames a second of the record only.
	if (isVisible()) {
		update(discRect().toAlignedRect().marginsAdded({ 2, 2, 2, 2 }));
	}
}) {
	setMouseTracking(true);
	Library::Instance().ensureTracking();
	const auto instance = Media::Player::instance();
	instance->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &state) {
		return state.id.type() == Type::Song;
	}) | rpl::on_next([=](const Media::Player::TrackState &state) {
		_state = state;
		updateTrack();
	}, lifetime());
	instance->trackChanged(
	) | rpl::filter(
		rpl::mappers::_1 == Type::Song
	) | rpl::on_next([=] {
		updateTrack();
	}, lifetime());
	controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (_cover.check()) {
			update();
		}
	}, lifetime());
	_state = instance->getState(Type::Song);
	updateTrack();
}

void NowPlayingPill::updateTrack() {
	_track = CurrentTrack();
	const auto current = Media::Player::instance()->current(Type::Song);
	_cover.set(current.audio(), current.contextId());
	const auto playing = IsPlaying()
		&& !Extera::Bool(u"tristegram_eco"_q);
	if (playing && !_spin.isActive()) {
		_spin.callEach(33);
	} else if (!playing && _spin.isActive()) {
		_spin.cancel();
	}
	update();
}

void NowPlayingPill::showVolume() {
	if (_volume) {
		_volume->close();
		return;
	}
	const auto popup = new VolumePopup();
	_volume = popup;
	const auto global = mapToGlobal(QPoint(
		width() - popup->width(),
		height() + Px(6)));
	popup->move(global);
	popup->show();
}

QRectF NowPlayingPill::discRect() const {
	return QRectF(
		Px(5),
		(height() - Px(30)) / 2.,
		Px(30),
		Px(30));
}

QRectF NowPlayingPill::partRect(Part part) const {
	const auto small = Px(24);
	const auto play = Px(28);
	const auto y = (height() - small) / 2.;
	const auto volume = QRectF(width() - Px(6) - small, y, small, small);
	const auto next = volume.translated(-small - Px(1), 0);
	const auto playRect = QRectF(
		next.x() - play - Px(1),
		(height() - play) / 2.,
		play,
		play);
	const auto previous = QRectF(playRect.x() - small - Px(1), y, small, small);
	switch (part) {
	case Part::Volume: return volume;
	case Part::Next: return next;
	case Part::Play: return playRect;
	case Part::Previous: return previous;
	case Part::Body: return QRectF(rect());
	case Part::None: break;
	}
	return QRectF();
}

NowPlayingPill::Part NowPlayingPill::partAt(QPoint position) const {
	for (const auto part : { Part::Volume }) {
		if (partRect(part).contains(position)) {
			return part;
		}
	}
	if (_track) {
		for (const auto part : { Part::Previous, Part::Play, Part::Next }) {
			if (partRect(part).contains(position)) {
				return part;
			}
		}
	}
	return rect().contains(position) ? Part::Body : Part::None;
}

void NowPlayingPill::mouseMoveEvent(QMouseEvent *e) {
	const auto over = partAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over != Part::None) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void NowPlayingPill::leaveEventHook(QEvent *e) {
	_over = Part::None;
	update();
}

void NowPlayingPill::wheelEvent(QWheelEvent *e) {
	const auto delta = e->angleDelta().y();
	if (!delta) {
		return;
	}
	SetVolumePercent(VolumePercent() + ((delta > 0) ? 10 : -10));
	_volumeShownUntil = crl::now() + 1200;
	update();
	base::call_delayed(1300, this, [=] { update(); });
	if (_volume) {
		_volume->update();
	}
}

void NowPlayingPill::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_pressed = partAt(e->pos());
	}
}

void NowPlayingPill::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, Part::None);
	if (pressed == Part::None || pressed != partAt(e->pos())) {
		return;
	}
	const auto instance = Media::Player::instance();
	switch (pressed) {
	case Part::Play:
		instance->playPause(Type::Song);
		break;
	case Part::Next:
		if (!Library::Instance().queueStep(1)) {
			instance->next(Type::Song);
		}
		break;
	case Part::Previous:
		if (!Library::Instance().queueStep(-1)) {
			instance->previous(Type::Song);
		}
		break;
	case Part::Volume:
		showVolume();
		break;
	case Part::Body:
		ShowMusicWindow(_controller);
		break;
	case Part::None:
		break;
	}
}

void NowPlayingPill::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto r = QRectF(rect()).marginsRemoved({ 0.5, 0.5, 0.5, 0.5 });
	const auto radius = r.height() / 2.;
	const auto playing = IsPlaying();

	// A glass capsule, the logo colors flow along it while music plays.
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(st::windowFg->c, _over == Part::Body ? 0.09 : 0.06));
	p.drawRoundedRect(r, radius, radius);
	if (_track) {
		auto wash = QLinearGradient(r.topLeft(), r.topRight());
		wash.setColorAt(0., Alpha(colors.from, 0.26));
		wash.setColorAt(0.6, Alpha(colors.to, 0.10));
		wash.setColorAt(1., Alpha(colors.to, 0.02));
		p.setBrush(wash);
		p.drawRoundedRect(r, radius, radius);
	}
	auto rim = QLinearGradient(r.topLeft(), r.topRight());
	rim.setColorAt(0., Alpha(colors.from, _track ? 0.55 : 0.15));
	rim.setColorAt(1., Alpha(colors.to, _track ? 0.25 : 0.08));
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QBrush(rim), 1.));
	p.drawRoundedRect(r, radius, radius);

	const auto disc = discRect();
	if (playing) {
		auto glow = QRadialGradient(disc.center(), disc.width() * 0.75);
		glow.setColorAt(0., Alpha(colors.from, 0.45));
		glow.setColorAt(1., Alpha(colors.from, 0.));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(disc.center(), disc.width() * 0.75, disc.width() * 0.75);
	}
	PaintVinyl(
		p,
		disc,
		_cover.image(),
		SpinAngle(crl::now(), _lastSpin, _angle),
		_track ? _track->document : 0);

	const auto left = int(disc.right()) + Px(8);
	const auto right = int(partRect(_track ? Part::Previous : Part::Volume).x())
		- Px(4);
	const auto available = std::max(right - left, Px(20));
	const auto small = Font(11);
	const auto showVolume = (crl::now() < _volumeShownUntil);
	const auto title = showVolume
		? (tr::lng_tgm_volume(tr::now) + u" "_q
			+ QString::number(VolumePercent()) + '%')
		: _track
		? _track->title
		: tr::lng_tgm_title(tr::now);
	const auto sub = !_track
		? tr::lng_tgm_pill_hint(tr::now)
		: _track->performer.isEmpty()
		? tr::lng_tgm_unknown_artist(tr::now)
		: _track->performer;
	const auto titleHeight = st::semiboldFont->height;
	const auto blockTop = (height() - titleHeight - small->height - Px(5)) / 2;
	p.setFont(st::semiboldFont);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(left, blockTop, available, titleHeight),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::semiboldFont->elided(title, available));
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(left, blockTop + titleHeight, available, small->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		small->elided(sub, available));

	// A thin line of how much of the song has played.
	if (_track) {
		const auto total = (_state.length > 0 && _state.frequency > 0)
			? (_state.length * 1000 / _state.frequency)
			: crl::time(_track->duration) * 1000;
		const auto position = (_state.frequency > 0)
			? (_state.position * 1000 / _state.frequency)
			: 0;
		const auto progress = (total > 0)
			? std::clamp(position / double(total), 0., 1.)
			: 0.;
		const auto line = QRectF(
			left,
			blockTop + titleHeight + small->height + Px(3),
			available,
			Px(2));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.12));
		p.drawRoundedRect(line, Px(1), Px(1));
		auto fill = QLinearGradient(line.topLeft(), line.topRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setBrush(fill);
		p.drawRoundedRect(
			QRectF(line.x(), line.y(), line.width() * progress, line.height()),
			Px(1),
			Px(1));
	}

	const auto paintHover = [&](Part part) {
		if (_over == part) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.1));
			p.drawEllipse(partRect(part));
		}
	};
	if (_track) {
		for (const auto part : { Part::Previous, Part::Next }) {
			paintHover(part);
			PaintSkipIcon(
				p,
				partRect(part).marginsRemoved(QMarginsF(Px(6), Px(6), Px(6), Px(6))),
				(part == Part::Next),
				st::windowFg->c);
		}
		const auto play = partRect(Part::Play);
		auto fill = QLinearGradient(play.topLeft(), play.bottomRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawEllipse(play);
		if (_over == Part::Play) {
			p.setBrush(QColor(255, 255, 255, 40));
			p.drawEllipse(play);
		}
		PaintPlayIcon(
			p,
			play.marginsRemoved(QMarginsF(Px(8), Px(8), Px(8), Px(8))),
			playing,
			QColor(255, 255, 255));
	}
	paintHover(Part::Volume);
	PaintSpeakerIcon(
		p,
		partRect(Part::Volume).marginsRemoved(QMarginsF(Px(5), Px(5), Px(5), Px(5))),
		VolumePercent(),
		st::windowFg->c);
}

} // namespace

void StartMusicTracking() {
	SetSongBoost(Extera::Int(u"tristegram_song_boost"_q));
	Library::Instance().ensureTracking();
}

void ShowMusicWindow(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto window = controller->widget();
		const auto width = std::min(Px(860), window->width() - Px(40));
		const auto height = std::min(Px(640), window->height() - Px(60));
		box->setNoContentMargin(true);
		box->setWidth(width);
		box->addRow(
			object_ptr<MusicWindow>(
				box,
				controller,
				[=] { box->closeBox(); },
				height),
			QMargins());
	}));
}

object_ptr<Ui::RpWidget> CreateNowPlayingPill(
		QWidget *parent,
		not_null<Window::SessionController*> controller) {
	return object_ptr<NowPlayingPill>(parent, controller);
}

void AddShareImportActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item,
		not_null<Window::SessionController*> controller) {
	const auto text = item->originalText().text;
	if (IsCustomThemeCode(text)) {
		menu->addAction(tr::lng_tgs_theme_install(tr::now), [=] {
			if (!ImportCustomTheme(controller, text)) {
				controller->showToast(tr::lng_tgs_theme_bad_code(tr::now));
			}
		}, &st::menuIconPalette);
	}
	if (const auto code = PlaylistCode(item)) {
		const auto itemId = item->fullId();
		const auto value = *code;
		menu->addAction(tr::lng_tgm_import(tr::now), [=] {
			if (const auto item = controller->session().data().message(itemId)) {
				ImportPlaylist(controller, item, value);
			}
		}, &st::menuIconAddToFolder);
	}
}

void AddPlaylistMenuActions(
		not_null<Ui::PopupMenu*> menu,
		not_null<HistoryItem*> item) {
	const auto track = TrackFromItem(item);
	if (!track || !item->isRegular()) {
		return;
	}
	const auto value = *track;
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(playlists.size()); ++i) {
		menu->addAction(
			tr::lng_tgm_add_to(tr::now) + u" «"_q + playlists[i].name + u"»"_q,
			[=] { Library::Instance().add(i, value); },
			&st::menuIconAddToFolder);
	}
}

} // namespace Tristegram
