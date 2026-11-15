/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// The Tristegram downloads window: overall progress, every file that is
// being downloaded with its speed and time left, and recent downloads
// that can be opened or shown in their folder.
void ShowDownloadsWindow(not_null<Window::SessionController*> controller);

// File extension in upper case, empty when the name has none.
[[nodiscard]] QString FileExtension(const QString &name);

// A color per kind of file, shared by the downloads window and the
// file tiles in chats.
[[nodiscard]] QColor FileTileColor(const QString &extension);

} // namespace Tristegram
