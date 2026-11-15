/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_plugin_bridge.h"

#include "apiwrap.h"
#include "core/application.h"
#include "core/cached_webview_availability.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "ui/basic_click_handlers.h"
#include "ui/rp_widget.h"
#include "ui/widgets/separate_panel.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>

#include <atomic>
#include <map>
#include <mutex>

namespace Tristegram::PluginBridge {
namespace {

[[nodiscard]] Main::Session *ActiveSession() {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	return controller ? &controller->session() : nullptr;
}

[[nodiscard]] bool IsLocalUrl(const QString &url) {
	return url.startsWith(u"http://127.0.0.1:"_q)
		|| url.startsWith(u"http://localhost:"_q);
}

struct WebPanel {
	std::unique_ptr<Ui::SeparatePanel> panel;
	std::unique_ptr<Webview::Window> webview;
	rpl::lifetime lifetime;
};

std::vector<std::unique_ptr<WebPanel>> &Panels() {
	static auto result = std::vector<std::unique_ptr<WebPanel>>();
	return result;
}

void ClosePanel(not_null<WebPanel*> state) {
	auto &list = Panels();
	const auto i = ranges::find_if(list, [&](const auto &p) {
		return p.get() == state.get();
	});
	if (i == list.end()) {
		return;
	}
	auto owned = std::move(*i);
	list.erase(i);
	// Close the webview before the panel, like the location picker does.
	base::take(owned->webview);
	owned->panel = nullptr;
}

struct FetchJob {
	QString state = u"searching"_q;
	int64 ready = 0;
	int64 total = 0;
	QString path;
};

std::mutex FetchMutex;
std::map<int, FetchJob> FetchJobs;
std::atomic<int> FetchNext = 0;

void SetJob(int id, Fn<void(FetchJob&)> change) {
	std::lock_guard<std::mutex> lock(FetchMutex);
	change(FetchJobs[id]);
}

[[nodiscard]] HistoryItem *FindIn(
		not_null<Main::Session*> session,
		const MTPmessages_Messages &result,
		uint64 documentId) {
	auto found = (HistoryItem*)nullptr;
	result.match([](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		session->data().processUsers(data.vusers());
		session->data().processChats(data.vchats());
		for (const auto &message : data.vmessages().v) {
			const auto item = session->data().addNewMessage(
				message,
				MessageFlags(),
				NewMessageType::Existing);
			if (!item || found) {
				continue;
			}
			const auto media = item->media();
			const auto document = media ? media->document() : nullptr;
			if (document && document->id == documentId) {
				found = item;
			}
		}
	});
	return found;
}

void Download(int id, not_null<HistoryItem*> item, const QString &path) {
	const auto document = item->media()->document();
	SetJob(id, [&](FetchJob &job) {
		job.state = u"loading"_q;
		job.total = document->size;
	});
	if (QFile::exists(path) && QFileInfo(path).size() == document->size) {
		SetJob(id, [&](FetchJob &job) {
			job.state = u"done"_q;
			job.ready = job.total;
			job.path = path;
		});
		return;
	}
	document->save(Data::FileOriginMessage(item->fullId()), path);
	const auto timer = new QTimer(qApp);
	const auto weak = base::make_weak(&item->history()->session());
	QObject::connect(timer, &QTimer::timeout, [=] {
		if (!weak) {
			SetJob(id, [](FetchJob &job) { job.state = u"error"_q; });
			timer->deleteLater();
			return;
		}
		if (document->loading()) {
			SetJob(id, [&](FetchJob &job) {
				job.ready = document->loadOffset();
				job.total = document->size;
			});
			return;
		}
		timer->deleteLater();
		const auto ok = QFile::exists(path) && QFileInfo(path).size() > 0;
		SetJob(id, [&](FetchJob &job) {
			job.state = ok ? u"done"_q : u"error"_q;
			job.ready = ok ? document->size : job.ready;
			job.path = ok ? path : QString();
		});
	});
	timer->start(300);
}

[[nodiscard]] PeerId PeerFromDialog(
		not_null<Main::Session*> session,
		int64 dialog) {
	if (dialog > 0) {
		return peerFromUser(UserId(uint64(dialog)));
	}
	const auto bare = uint64(-dialog);
	const auto channel = peerFromChannel(ChannelId(bare));
	return session->data().peerLoaded(channel)
		? channel
		: peerFromChat(ChatId(bare));
}

void Fetch(
		int id,
		uint64 hostUserId,
		uint64 documentId,
		QString path,
		int64 dialogId,
		int64 messageId) {
	const auto session = ActiveSession();
	if (!session) {
		SetJob(id, [](FetchJob &job) { job.state = u"error"_q; });
		return;
	}
	if (dialogId && messageId) {
		const auto item = session->data().message(
			PeerFromDialog(session, dialogId),
			MsgId(messageId));
		const auto media = item ? item->media() : nullptr;
		const auto document = media ? media->document() : nullptr;
		if (document && document->id == documentId) {
			Download(id, item, path);
			return;
		}
	}
	const auto weak = base::make_weak(session);
	const auto global = [=] {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		strong->api().request(MTPmessages_SearchGlobal(
			MTP_flags(0),
			MTP_int(0), // folder_id
			MTPInputChannel(),
			MTP_string(),
			MTP_inputMessagesFilterVideo(),
			MTP_int(0), // min_date
			MTP_int(0), // max_date
			MTP_int(0), // offset_rate
			MTP_inputPeerEmpty(),
			MTP_int(0), // offset_id
			MTP_int(100)
		)).done([=](const MTPmessages_Messages &result) {
			const auto strong = weak.get();
			const auto item = strong
				? FindIn(strong, result, documentId)
				: nullptr;
			if (item) {
				Download(id, item, path);
			} else {
				SetJob(id, [](FetchJob &job) { job.state = u"missing"_q; });
			}
		}).fail([=] {
			SetJob(id, [](FetchJob &job) { job.state = u"missing"_q; });
		}).send();
	};
	if (!hostUserId) {
		global();
		return;
	}
	const auto peer = session->data().peer(peerFromUser(UserId(hostUserId)));
	session->api().request(MTPmessages_Search(
		MTP_flags(0),
		peer->input(),
		MTP_string(), // q
		MTP_inputPeerEmpty(),
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterVideo(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(0), // offset_id
		MTP_int(0), // add_offset
		MTP_int(100), // limit
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		const auto strong = weak.get();
		const auto item = strong
			? FindIn(strong, result, documentId)
			: nullptr;
		if (item) {
			Download(id, item, path);
		} else {
			global();
		}
	}).fail([=] {
		global();
	}).send();
}

} // namespace

bool OpenWebView(const QString &url, const QString &title) {
	if (!IsLocalUrl(url)) {
		return false;
	}
	const auto &availability = Core::CachedWebviewAvailability();
	if (availability.error != Webview::Available::Error::None) {
		return false;
	}
	auto owned = std::make_unique<WebPanel>();
	const auto state = owned.get();
	state->panel = std::make_unique<Ui::SeparatePanel>();
	const auto panel = state->panel.get();
	panel->setTitle(rpl::single(title));
	panel->setInnerSize(
		QSize(style::ConvertScale(1180), style::ConvertScale(720)),
		true);
	panel->showInner(base::make_unique_q<Ui::RpWidget>(panel));
	const auto container = panel->inner();

	const auto session = ActiveSession();
	auto config = Webview::WindowConfig{
		.opaqueBg = QColor(13, 14, 19),
	};
	if (session) {
		config.storageId = session->local().resolveStorageIdBots();
	}
	state->webview = std::make_unique<Webview::Window>(
		container,
		std::move(config));
	const auto webview = state->webview.get();
	if (!webview->widget()) {
		state->webview = nullptr;
		state->panel = nullptr;
		return false;
	}
	container->sizeValue(
	) | rpl::on_next([=](QSize size) {
		webview->widget()->setGeometry(QRect(QPoint(), size));
	}, state->lifetime);
	webview->widget()->show();
	webview->setNavigationPolicyHandler([=](
			const QString &uri,
			bool newWindow) {
		if (IsLocalUrl(uri) && !newWindow) {
			return true;
		}
		// Links out of the player open in the usual browser.
		if (uri.startsWith(u"http"_q)) {
			crl::on_main([=] { File::OpenUrl(uri); });
		}
		return false;
	});
	webview->navigate(url);

	panel->closeRequests(
	) | rpl::on_next([=] {
		crl::on_main([=] { ClosePanel(state); });
	}, state->lifetime);

	if (const auto window = Core::App().activePrimaryWindow()) {
		const auto parent = window->widget()->geometry();
		panel->move(
			parent.x() + (parent.width() - panel->width()) / 2,
			parent.y() + (parent.height() - panel->height()) / 2);
	}
	panel->showAndActivate();
	Panels().push_back(std::move(owned));
	return true;
}

int StartVideoFetch(
		uint64 hostUserId,
		uint64 documentId,
		const QString &path,
		int64 dialogId,
		int64 messageId) {
	const auto id = ++FetchNext;
	SetJob(id, [](FetchJob &job) { job.state = u"searching"_q; });
	crl::on_main([=] {
		Fetch(id, hostUserId, documentId, path, dialogId, messageId);
	});
	return id;
}

QString VideoFetchStatus(int job) {
	auto copy = FetchJob();
	{
		std::lock_guard<std::mutex> lock(FetchMutex);
		const auto i = FetchJobs.find(job);
		if (i == FetchJobs.end()) {
			copy.state = u"error"_q;
		} else {
			copy = i->second;
		}
	}
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{ u"state"_q, copy.state },
		{ u"ready"_q, double(copy.ready) },
		{ u"total"_q, double(copy.total) },
		{ u"path"_q, copy.path },
	}).toJson(QJsonDocument::Compact));
}

} // namespace Tristegram::PluginBridge
