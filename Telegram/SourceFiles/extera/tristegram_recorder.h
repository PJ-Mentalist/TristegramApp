/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// A record button for the chat header. It records only the Telegram
// window, scaled down and encoded to H.264 on a separate thread, skipping
// frames instead of slowing the app down on weak computers.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateRecordButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller);

// Starts recording the window or finishes and shows the file.
void ToggleScreenRecording(
	not_null<Window::SessionController*> controller);

// Finishes the file if a recording is running, for example on quit.
void StopScreenRecording();

// Records what the computer plays (and the microphone, if it is turned
// on in the settings) to an .m4a file, for voice chats where you can
// not start the Telegram recording.
[[nodiscard]] bool AudioRecording();
bool StartAudioRecording();
[[nodiscard]] QString StopAudioRecording();
[[nodiscard]] crl::time AudioRecordingStarted();

} // namespace Tristegram
