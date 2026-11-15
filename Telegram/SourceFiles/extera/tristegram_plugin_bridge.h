/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace Tristegram::PluginBridge {

// Opens a page of a plugin's local server (http://127.0.0.1:port/...)
// in a Tristegram window with the built-in webview. Main thread only.
// Returns false when the webview is not available on this system.
bool OpenWebView(const QString &url, const QString &title);

// Finds a video by its document id: first in the chat with hostUserId,
// then in all chats, and downloads it to path. Any thread.
// When dialogId and messageId are given, that message is tried first.
[[nodiscard]] int StartVideoFetch(
	uint64 hostUserId,
	uint64 documentId,
	const QString &path,
	int64 dialogId = 0,
	int64 messageId = 0);

// {"state": "searching|loading|done|missing|error", "ready": n,
//  "total": n, "path": "..."} as JSON. Any thread.
[[nodiscard]] QString VideoFetchStatus(int job);

} // namespace Tristegram::PluginBridge
