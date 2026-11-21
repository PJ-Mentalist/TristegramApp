/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_pip.h"

#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history_item.h"
#include "media/view/media_view_open_common.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Tristegram {
namespace {

crl::time PipRequestedAt = 0;

[[nodiscard]] DocumentData *VideoOf(not_null<HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document
		&& (document->isVideoFile() || document->isVideoMessage()))
		? document
		: nullptr;
}

} // namespace

bool CanOpenInPip(not_null<HistoryItem*> item) {
	return VideoOf(item) != nullptr;
}

void OpenInPip(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto document = VideoOf(item);
	if (!document) {
		return;
	}
	PipRequestedAt = crl::now();
	controller->window().openInMediaView(Media::View::OpenRequest(
		controller,
		document,
		item,
		item->topicRootId(),
		PeerId()));
}

bool TakePipRequest() {
	const auto requested = PipRequestedAt
		&& (crl::now() - PipRequestedAt < 3000);
	PipRequestedAt = 0;
	return requested;
}

} // namespace Tristegram
