/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_dnd.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>

namespace Tristegram {
namespace {

// Seconds since epoch, -1 for "until I turn it off", 0 for off.
const auto kUntil = u"tristegram_dnd_until"_q;
const auto kAllowed = u"tristegram_dnd_allowed"_q;

[[nodiscard]] int Until() {
	return Extera::Int(kUntil);
}

[[nodiscard]] std::vector<uint64> AllowedIds() {
	auto result = std::vector<uint64>();
	const auto array = QJsonDocument::fromJson(
		Extera::String(kAllowed).toUtf8()).array();
	for (const auto &value : array) {
		const auto id = value.toString().toULongLong();
		if (id) {
			result.push_back(id);
		}
	}
	return result;
}

void SaveAllowed(const std::vector<uint64> &ids) {
	auto array = QJsonArray();
	for (const auto id : ids) {
		array.push_back(QString::number(id));
	}
	Extera::Config::Instance().setString(
		kAllowed,
		QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void SetUntil(int value) {
	Extera::Config::Instance().setInt(kUntil, value);
}

[[nodiscard]] QString StatusText() {
	const auto until = Until();
	if (!DndActive()) {
		return tr::lng_tgdnd_off(tr::now);
	} else if (until < 0) {
		return tr::lng_tgdnd_on_forever(tr::now);
	}
	const auto when = QDateTime::fromSecsSinceEpoch(until);
	const auto today = QDate::currentDate();
	const auto time = QLocale().toString(when.time(), QLocale::ShortFormat);
	return tr::lng_tgdnd_on_until(
		tr::now,
		lt_time,
		(when.date() == today)
			? time
			: (QLocale().toString(when.date(), u"d MMM"_q) + u", "_q + time));
}

// The next 8:00 in the morning.
[[nodiscard]] int Morning() {
	auto when = QDateTime(QDate::currentDate(), QTime(8, 0));
	if (when <= QDateTime::currentDateTime()) {
		when = when.addDays(1);
	}
	return int(when.toSecsSinceEpoch());
}

} // namespace

bool DndActive() {
	const auto until = Until();
	return (until < 0)
		|| (until > 0 && until > base::unixtime::now());
}

bool DndAllowed(not_null<PeerData*> peer) {
	return ranges::contains(AllowedIds(), peer->id.value);
}

bool DndBlocks(not_null<PeerData*> peer) {
	return DndActive() && !DndAllowed(peer);
}

void DndToggleAllowed(not_null<PeerData*> peer) {
	auto ids = AllowedIds();
	const auto id = peer->id.value;
	if (ranges::contains(ids, id)) {
		ids.erase(ranges::remove(ids, id), ids.end());
	} else {
		ids.push_back(id);
	}
	SaveAllowed(ids);
}

rpl::producer<> DndChanges() {
	return Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_dnd"_q);
	}) | rpl::to_empty;
}

void ShowDndBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgdnd_title());
		box->setWidth(st::boxWideWidth);
		const auto session = &controller->session();

		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(rpl::empty) | rpl::then(
				DndChanges()
			) | rpl::map([](const auto &) { return StatusText(); }),
			st::boxLabel));
		status->setTextColorOverride(st::windowActiveTextFg->c);

		const auto addTime = [&](rpl::producer<QString> text, Fn<int()> until) {
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					std::move(text),
					st::defaultSettingsButton),
				QMargins());
			button->setClickedCallback([=] { SetUntil(until()); });
		};
		const auto in = [](int hours) {
			return [=] { return int(base::unixtime::now()) + hours * 3600; };
		};
		addTime(tr::lng_tgdnd_hour(), in(1));
		addTime(tr::lng_tgdnd_hours3(), in(3));
		addTime(tr::lng_tgdnd_hours8(), in(8));
		addTime(tr::lng_tgdnd_morning(), [] { return Morning(); });
		addTime(tr::lng_tgdnd_forever(), [] { return -1; });

		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_tgdnd_allowed(),
			st::boxDividerLabel));
		const auto list = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			QMargins());
		const auto refill = [=] {
			list->clear();
			const auto ids = AllowedIds();
			if (ids.empty()) {
				list->add(object_ptr<Ui::FlatLabel>(
					list,
					tr::lng_tgdnd_nobody(),
					st::boxDividerLabel),
					st::boxRowPadding);
			}
			for (const auto id : ids) {
				const auto peer = session->data().peerLoaded(PeerId(id));
				const auto name = peer
					? peer->name()
					: QString::number(id);
				const auto button = list->add(
					object_ptr<Ui::SettingsButton>(
						list,
						rpl::single(name + u"   ✕"_q),
						st::defaultSettingsButton));
				button->setClickedCallback([=] {
					auto ids = AllowedIds();
					ids.erase(ranges::remove(ids, id), ids.end());
					SaveAllowed(ids);
				});
			}
			list->resizeToWidth(st::boxWideWidth);
		};
		refill();
		Extera::Config::Instance().changes(
		) | rpl::filter([](const QString &key) {
			return key == kAllowed;
		}) | rpl::on_next([=](const QString &) {
			refill();
		}, list->lifetime());

		box->addButton(tr::lng_tgdnd_add(), [=] {
			Window::ShowChooseRecipientBox(
				controller,
				[=](not_null<Data::Thread*> thread) {
					if (!DndAllowed(thread->peer())) {
						DndToggleAllowed(thread->peer());
					}
					return true;
				},
				tr::lng_tgdnd_add());
		});
		box->addLeftButton(tr::lng_tgdnd_turn_off(), [=] { SetUntil(0); });
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Tristegram
