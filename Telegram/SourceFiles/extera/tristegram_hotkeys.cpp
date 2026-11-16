/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_hotkeys.h"

#include "base/unixtime.h"
#include "core/application.h"
#include "core/shortcuts.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "extera/tristegram_downloads.h"
#include "extera/tristegram_music.h"
#include "extera/tristegram_recorder.h"
#include "extera/tristegram_settings_window.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "lang/lang_keys.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Tristegram::Hotkeys {
namespace {

[[nodiscard]] Window::SessionController *ActiveController() {
	const auto window = Core::App().activePrimaryWindow();
	return window ? window->sessionController() : nullptr;
}

} // namespace

HistoryItem *LastMessage(not_null<History*> history, Whose whose) {
	for (auto i = history->blocks.size(); i != 0;) {
		const auto &block = history->blocks[--i];
		for (auto j = block->messages.size(); j != 0;) {
			const auto item = block->messages[--j]->data();
			if (!item->isRegular() || item->isService()) {
				continue;
			} else if (whose == Whose::Mine && !item->out()) {
				continue;
			} else if (whose == Whose::Others && item->out()) {
				continue;
			}
			return item;
		}
	}
	return nullptr;
}

bool DeleteLastOwn(
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	const auto item = LastMessage(history, Whose::Mine);
	if (!item || !item->canDelete()) {
		return false;
	}
	const auto revoke = item->canDeleteForEveryone(base::unixtime::now());
	history->owner().histories().deleteMessages(
		{ item->fullId() },
		revoke);
	history->owner().sendHistoryChangeNotifications();
	controller->showToast(tr::lng_tgk_deleted(tr::now));
	return true;
}

bool CopyLast(
		not_null<Window::SessionController*> controller,
		not_null<History*> history) {
	const auto item = LastMessage(history, Whose::Any);
	if (!item) {
		return false;
	}
	const auto text = item->originalText().text;
	if (text.isEmpty()) {
		return false;
	}
	QGuiApplication::clipboard()->setText(text);
	controller->showToast(tr::lng_tgk_copied(tr::now));
	return true;
}

void Init(rpl::lifetime &lifetime) {
	Shortcuts::Requests(
	) | rpl::on_next([=](not_null<Shortcuts::Request*> request) {
		using Command = Shortcuts::Command;
		const auto with = [&](
				Command command,
				Fn<void(not_null<Window::SessionController*>)> action) {
			request->check(command) && request->handle([=] {
				const auto controller = ActiveController();
				if (!controller) {
					return false;
				}
				action(controller);
				return true;
			});
		};
		with(Command::TgOpenMusic, [](auto controller) {
			ShowMusicWindow(controller);
		});
		with(Command::TgOpenSettings, [](auto controller) {
			ShowSettingsWindow(controller);
		});
		with(Command::TgOpenDownloads, [](auto controller) {
			ShowDownloadsWindow(controller);
		});
		with(Command::TgToggleHideMode, [](auto controller) {
			auto &config = Extera::Config::Instance();
			const auto enabled = !Extera::Bool(u"streamer_mode"_q);
			config.setBool(u"streamer_mode"_q, enabled);
			controller->showToast(enabled
				? tr::lng_tgk_hide_on(tr::now)
				: tr::lng_tgk_hide_off(tr::now));
		});
		with(Command::TgToggleNightMode, [](auto controller) {
			if (Window::Theme::Background()->editingTheme()) {
				return;
			}
			Window::Theme::ToggleNightModeWithConfirmation(
				&controller->window(),
				[] {
					Window::Theme::ToggleNightMode();
					Window::Theme::KeepApplied();
				});
		});
		with(Command::TgScreenRecord, [](auto controller) {
			ToggleScreenRecording(controller);
		});
	}, lifetime);
}

} // namespace Tristegram::Hotkeys
