/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_together.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "window/window_session_controller.h"

namespace Tristegram {
namespace {

using Type = AudioMsgId::Type;

[[nodiscard]] QString Simple(QString text) {
	text = text.toLower();
	auto result = QString();
	result.reserve(text.size());
	for (const auto ch : text) {
		if (ch.isLetterOrNumber()) {
			result.append(ch);
		}
	}
	return result;
}

// How well a found song matches the one of the friend.
[[nodiscard]] int Score(
		not_null<DocumentData*> document,
		const Activity::Status &status) {
	const auto song = document->song();
	const auto title = Simple(song ? song->title : document->filename());
	const auto performer = Simple(song ? song->performer : QString());
	const auto wantTitle = Simple(status.title);
	const auto wantPerformer = Simple(status.subtitle);
	auto result = 0;
	if (!wantTitle.isEmpty() && title == wantTitle) {
		result += 4;
	} else if (!wantTitle.isEmpty()
		&& (title.contains(wantTitle) || wantTitle.contains(title))
		&& !title.isEmpty()) {
		result += 2;
	}
	if (!wantPerformer.isEmpty() && !performer.isEmpty()) {
		if (performer == wantPerformer) {
			result += 3;
		} else if (performer.contains(wantPerformer)
			|| wantPerformer.contains(performer)) {
			result += 1;
		}
	}
	return result;
}

// Jumps to the friend's moment once the song is loaded enough to know
// its length.
void SeekWhenReady(
		not_null<DocumentData*> document,
		const Activity::Status &status) {
	if (status.duration <= 0) {
		return;
	}
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto started = crl::now();
	Media::Player::instance()->updatedNotifier(
	) | rpl::on_next([=](const Media::Player::TrackState &state) {
		if (!lifetime) {
			return;
		}
		if (crl::now() - started > 30'000) {
			lifetime->destroy();
			return;
		}
		if (state.id.type() != Type::Song
			|| state.id.audio() != document
			|| state.length <= 0
			|| state.frequency <= 0
			|| Media::Player::IsStoppedOrStopping(state.state)) {
			return;
		}
		const auto length = state.length * 1000 / state.frequency;
		// A little ahead for the time the request and loading took.
		const auto position = Activity::CurrentPosition(status) + 600;
		if (position > 2000 && position < length - 2000) {
			const auto instance = Media::Player::instance();
			instance->startSeeking(Type::Song);
			instance->finishSeeking(
				Type::Song,
				std::clamp(double(position) / length, 0., 1.));
		}
		lifetime->destroy();
	}, *lifetime);
}

} // namespace

void ListenTogether(
		not_null<Window::SessionController*> controller,
		const Activity::Status &status) {
	const auto query = (status.subtitle + ' ' + status.title).trimmed();
	if (query.isEmpty()) {
		return;
	}
	controller->showToast(tr::lng_tgt_searching(
		tr::now,
		lt_name,
		status.title));
	const auto session = &controller->session();
	const auto weak = base::make_weak(controller);
	session->api().request(MTPmessages_SearchGlobal(
		MTP_flags(0),
		MTP_int(0), // folder_id
		MTPInputChannel(),
		MTP_string(query),
		MTP_inputMessagesFilterMusic(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(0), // offset_rate
		MTP_inputPeerEmpty(),
		MTP_int(0), // offset_id
		MTP_int(30)
	)).done([=](const MTPmessages_Messages &result) {
		auto best = (HistoryItem*)nullptr;
		auto bestDocument = (DocumentData*)nullptr;
		auto bestScore = -1;
		const auto process = [&](const auto &data) {
			session->data().processUsers(data.vusers());
			session->data().processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				const auto peerId = PeerFromMessage(message);
				if (!session->data().peerLoaded(peerId)) {
					continue;
				}
				const auto item = session->data().addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
				const auto media = item ? item->media() : nullptr;
				const auto document = media ? media->document() : nullptr;
				if (!document || !document->isAudioFile()) {
					continue;
				}
				const auto score = Score(document, status);
				if (score > bestScore) {
					best = item;
					bestDocument = document;
					bestScore = score;
				}
			}
		};
		result.match([&](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			process(data);
		});
		const auto strong = weak.get();
		if (!best || !bestDocument || bestScore <= 0) {
			if (strong) {
				strong->showToast(tr::lng_tgt_not_found(tr::now));
			}
			return;
		}
		const auto document = not_null(bestDocument);
		Media::Player::instance()->play(AudioMsgId(document, best->fullId()));
		SeekWhenReady(document, status);
		if (strong) {
			strong->showToast(tr::lng_tgt_playing(
				tr::now,
				lt_name,
				status.title));
		}
	}).fail([=] {
		if (const auto strong = weak.get()) {
			strong->showToast(tr::lng_tgt_not_found(tr::now));
		}
	}).send();
}

} // namespace Tristegram
