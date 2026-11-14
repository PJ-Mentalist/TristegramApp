/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_folders.h"

#include "apiwrap.h"
#include "data/data_chat_filters.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <memory>

namespace Tristegram {

void CreateTypeTabs(not_null<Window::SessionController*> controller) {
	using Flag = Data::ChatFilter::Flag;
	struct Tab {
		QString title;
		QString emoji;
		Data::ChatFilter::Flags flags;
	};
	const auto tabs = std::vector<Tab>{
		{
			tr::lng_tristegram_tab_private(tr::now),
			QString::fromUtf8("\xf0\x9f\x91\xa4"),
			Flag::Contacts | Flag::NonContacts,
		},
		{
			tr::lng_tristegram_tab_groups(tr::now),
			QString::fromUtf8("\xf0\x9f\x91\xa5"),
			Data::ChatFilter::Flags(Flag::Groups),
		},
		{
			tr::lng_tristegram_tab_channels(tr::now),
			QString::fromUtf8("\xf0\x9f\x93\xa2"),
			Data::ChatFilter::Flags(Flag::Channels),
		},
		{
			tr::lng_tristegram_tab_bots(tr::now),
			QString::fromUtf8("\xf0\x9f\xa4\x96"),
			Data::ChatFilter::Flags(Flag::Bots),
		},
	};

	const auto session = &controller->session();
	auto &filters = session->data().chatsFilters();
	const auto &list = filters.list();
	auto nextId = FilterId(2);
	for (const auto &filter : list) {
		nextId = std::max(nextId, FilterId(filter.id() + 1));
	}
	const auto exists = [&](const QString &title) {
		return std::any_of(list.begin(), list.end(), [&](const auto &f) {
			return f.titleText().text == title;
		});
	};

	const auto left = std::make_shared<int>(0);
	const auto failed = std::make_shared<bool>(false);
	const auto weak = base::make_weak(controller);
	const auto finish = [=] {
		if (--*left > 0) {
			return;
		}
		session->data().chatsFilters().reload();
		if (const auto strong = weak.get()) {
			strong->showToast(*failed
				? tr::lng_tristegram_tabs_failed(tr::now)
				: tr::lng_tristegram_tabs_done(tr::now));
		}
	};
	auto requests = 0;
	for (const auto &tab : tabs) {
		if (exists(tab.title)) {
			continue;
		}
		const auto filter = Data::ChatFilter(
			nextId,
			Data::ChatFilterTitle{ TextWithEntities{ tab.title } },
			tab.emoji,
			std::nullopt,
			tab.flags,
			{},
			{},
			{});
		++*left;
		++requests;
		session->api().request(MTPmessages_UpdateDialogFilter(
			MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
			MTP_int(nextId),
			filter.tl()
		)).done([=] {
			finish();
		}).fail([=] {
			*failed = true;
			finish();
		}).send();
		++nextId;
	}
	if (!requests) {
		controller->showToast(tr::lng_tristegram_tabs_exist(tr::now));
	}
}

} // namespace Tristegram
