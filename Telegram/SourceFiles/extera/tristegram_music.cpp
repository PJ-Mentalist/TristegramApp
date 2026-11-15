/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_music.h"

#include "apiwrap.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
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
};

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
	const auto weak = base::make_weak(session);
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
		_session = base::make_weak(session);
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
			if (const auto track = CurrentTrack(); track && track->peer) {
				pushRecent(*track);
			}
		}, _lifetime);
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

	std::vector<Playlist> _playlists;
	std::vector<Track> _recent;
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

enum class Source {
	Mine,
	Recent,
	Playlist,
};

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
	top += Px(8);
	_labelTop = top;
	top += Px(26);
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(playlists.size()); ++i) {
		add(Source::Playlist, i);
	}
	add(Source::Playlist, -2);
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
	_menu->addAction(tr::lng_tgm_delete(tr::now), [=] {
		Library::Instance().remove(index);
	}, &st::menuIconDelete);
	_menu->popup(QCursor::pos());
}

void Sidebar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto &playlists = Library::Instance().playlists();
	for (auto i = 0; i != int(_rows.size()); ++i) {
		const auto &row = _rows[i];
		const auto create = (row.source == Source::Playlist)
			&& (row.index == -2);
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
		}
		p.setPen(Qt::NoPen);
		p.setBrush(active ? QColor(255, 255, 255, 60) : color);
		p.drawRoundedRect(tile, Px(8), Px(8));
		p.setPen(QPen(
			(create && !active) ? st::windowFg->c : QColor(255, 255, 255),
			Px(2),
			Qt::SolidLine,
			Qt::RoundCap));
		const auto c = tile.center();
		const auto s = tile.width() * 0.22;
		if (create) {
			p.drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
			p.drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x(), c.y() + s));
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

	const not_null<Window::SessionController*> _controller;
	Source _source = Source::Mine;
	int _index = -1;
	std::vector<Track> _tracks;
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
, _animation([=] { update(); }) {
	setMouseTracking(true);
	Library::Instance().changes(
	) | rpl::on_next([=] {
		if (_source != Source::Mine) {
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
	}
	_over = -1;
	resizeToWidth(width());
	update();
}

int TrackList::resizeGetHeight(int newWidth) {
	return std::max(int(_tracks.size()) * Px(54) + Px(12), Px(150));
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
			QRectF(hero).marginsRemoved({ Px(12), 0, Px(12), Px(8) }),
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
	PaintVinyl(
		p,
		disc,
		_cover.image(),
		angle,
		_track ? _track->document : 0);

	// The tone arm moves onto the record while it plays.
	{
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
			playRect.marginsRemoved({ Px(13), Px(13), Px(13), Px(13) }),
			IsPlaying(),
			QColor(255, 255, 255));
	}
	for (const auto button : { Button::Previous, Button::Next, Button::Lyrics }) {
		const auto r = buttonRect(button);
		if (_over == button) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.08));
			p.drawEllipse(r);
		}
		const auto inner = r.marginsRemoved({ Px(9), Px(9), Px(9), Px(9) });
		if (button == Button::Lyrics) {
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
	void leaveEventHook(QEvent *e) override;

private:
	enum class Part {
		None,
		Body,
		Play,
		Next,
	};
	void updateTrack();
	[[nodiscard]] QRectF partRect(Part part) const;
	[[nodiscard]] Part partAt(QPoint position) const;

	const not_null<Window::SessionController*> _controller;
	std::optional<Track> _track;
	Cover _cover;
	Ui::Animations::Basic _spin;
	crl::time _lastSpin = 0;
	double _angle = 0.;
	Part _over = Part::None;
	Part _pressed = Part::None;
	Media::Player::TrackState _state;

};

NowPlayingPill::NowPlayingPill(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _spin([=] {
	if (isVisible()) {
		update();
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
	if (playing && !_spin.animating()) {
		_spin.start();
	} else if (!playing && _spin.animating()) {
		_spin.stop();
	}
	update();
}

QRectF NowPlayingPill::partRect(Part part) const {
	const auto size = Px(28);
	const auto y = (height() - size) / 2.;
	switch (part) {
	case Part::Next:
		return QRectF(width() - Px(6) - size, y, size, size);
	case Part::Play:
		return QRectF(width() - Px(6) - size * 2 - Px(2), y, size, size);
	case Part::Body:
		return QRectF(rect());
	case Part::None:
		break;
	}
	return QRectF();
}

NowPlayingPill::Part NowPlayingPill::partAt(QPoint position) const {
	if (_track) {
		for (const auto part : { Part::Play, Part::Next }) {
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

	// A glassy capsule with a hint of the logo colors.
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(st::windowFg->c, _over == Part::Body ? 0.09 : 0.06));
	p.drawRoundedRect(r, radius, radius);
	if (_track) {
		auto wash = QLinearGradient(r.topLeft(), r.topRight());
		wash.setColorAt(0., Alpha(colors.from, 0.22));
		wash.setColorAt(1., Alpha(colors.to, 0.04));
		p.setBrush(wash);
		p.drawRoundedRect(r, radius, radius);
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(Alpha(st::windowFg->c, 0.08), 1.));
	p.drawRoundedRect(r, radius, radius);

	const auto disc = QRectF(
		Px(4),
		(height() - Px(30)) / 2.,
		Px(30),
		Px(30));
	PaintVinyl(
		p,
		disc,
		_cover.image(),
		SpinAngle(crl::now(), _lastSpin, _angle),
		_track ? _track->document : 0);

	const auto left = int(disc.right()) + Px(8);
	const auto right = _track
		? int(partRect(Part::Play).x()) - Px(4)
		: width() - Px(12);
	const auto available = std::max(right - left, Px(20));
	const auto small = Font(11);
	const auto title = _track ? _track->title : tr::lng_tgm_title(tr::now);
	const auto sub = !_track
		? tr::lng_tgm_pill_hint(tr::now)
		: _track->performer.isEmpty()
		? tr::lng_tgm_unknown_artist(tr::now)
		: _track->performer;
	const auto titleHeight = st::semiboldFont->height;
	const auto blockTop = (height() - titleHeight - small->height) / 2;
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

	if (_track) {
		for (const auto part : { Part::Play, Part::Next }) {
			const auto rect = partRect(part);
			if (_over == part) {
				p.setPen(Qt::NoPen);
				p.setBrush(Alpha(st::windowFg->c, 0.1));
				p.drawEllipse(rect);
			}
			const auto inner = rect.marginsRemoved({ Px(7), Px(7), Px(7), Px(7) });
			if (part == Part::Play) {
				PaintPlayIcon(p, inner, IsPlaying(), st::windowFg->c);
			} else {
				PaintSkipIcon(p, inner, true, st::windowFg->c);
			}
		}
	}
}

} // namespace

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
