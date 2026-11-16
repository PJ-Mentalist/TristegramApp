/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_system_media.h"

#ifdef Q_OS_WIN
#include "base/platform/win/base_windows_winrt.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#endif // Q_OS_WIN

#include <chrono>
#include <vector>

namespace Tristegram::SystemMedia {
namespace {

#ifdef Q_OS_WIN
using namespace winrt::Windows::Media::Control;
using Session = GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = GlobalSystemMediaTransportControlsSessionPlaybackStatus;

[[nodiscard]] QString FromHString(const winrt::hstring &value) {
	return QString::fromWCharArray(value.c_str(), int(value.size()));
}

[[nodiscard]] QString AppName(QString id) {
	const auto bang = id.indexOf('!');
	if (bang >= 0) {
		id = id.mid(bang + 1);
	}
	if (id.endsWith(u".exe"_q, Qt::CaseInsensitive)) {
		id.chop(4);
	}
	const auto dot = id.lastIndexOf('.');
	if (dot > 0 && dot + 1 < id.size()) {
		id = id.mid(dot + 1);
	}
	const auto lower = id.toLower();
	if (lower == u"chrome"_q) {
		return u"Google Chrome"_q;
	} else if (lower == u"msedge"_q) {
		return u"Microsoft Edge"_q;
	} else if (lower == u"308046b0af4a39cb"_q || lower == u"firefox"_q) {
		return u"Firefox"_q;
	} else if (lower == u"zunemusic"_q) {
		return u"Media Player"_q;
	}
	if (!id.isEmpty()) {
		id[0] = id[0].toUpper();
	}
	return id;
}

[[nodiscard]] bool IsOwn(const QString &id) {
	const auto lower = id.toLower();
	return lower.contains(u"telegram"_q) || lower.contains(u"tristegram"_q);
}

// The playing session of another app, or the first one there is.
[[nodiscard]] Session ChooseSession(bool *playing) {
	const auto manager = GlobalSystemMediaTransportControlsSessionManager
		::RequestAsync().get();
	auto result = Session(nullptr);
	auto resultPlaying = false;
	const auto sessions = manager.GetSessions();
	for (const auto &session : sessions) {
		if (IsOwn(FromHString(session.SourceAppUserModelId()))) {
			continue;
		}
		const auto info = session.GetPlaybackInfo();
		const auto now = info
			&& (info.PlaybackStatus() == PlaybackStatus::Playing);
		if (!result || (now && !resultPlaying)) {
			result = session;
			resultPlaying = now;
		}
	}
	if (playing) {
		*playing = resultPlaying;
	}
	return result;
}

[[nodiscard]] crl::time Milliseconds(winrt::Windows::Foundation::TimeSpan value) {
	return crl::time(
		std::chrono::duration_cast<std::chrono::milliseconds>(value).count());
}

void InitThread() {
	base::WinRT::Try([] { winrt::init_apartment(); });
}
#endif // Q_OS_WIN

} // namespace

bool Supported() {
#ifdef Q_OS_WIN
	return base::WinRT::Supported();
#else
	return false;
#endif
}

void Request(Fn<void(std::optional<Track>)> done) {
#ifdef Q_OS_WIN
	if (!Supported()) {
		done(std::nullopt);
		return;
	}
	crl::async([=] {
		InitThread();
		auto result = std::optional<Track>();
		base::WinRT::Try([&] {
			auto playing = false;
			const auto session = ChooseSession(&playing);
			if (!session) {
				return;
			}
			auto track = Track();
			track.app = AppName(FromHString(session.SourceAppUserModelId()));
			track.playing = playing;
			const auto properties = session.TryGetMediaPropertiesAsync().get();
			if (properties) {
				track.title = FromHString(properties.Title());
				track.artist = FromHString(properties.Artist());
				if (track.artist.isEmpty()) {
					track.artist = FromHString(properties.AlbumArtist());
				}
				if (const auto thumbnail = properties.Thumbnail()) {
					using namespace winrt::Windows::Storage::Streams;
					const auto stream = thumbnail.OpenReadAsync().get();
					const auto size = uint32_t(stream.Size());
					if (size > 0 && size < 16 * 1024 * 1024) {
						auto reader = DataReader(stream.GetInputStreamAt(0));
						reader.LoadAsync(size).get();
						auto bytes = std::vector<uint8_t>(size);
						reader.ReadBytes(bytes);
						track.cover = QImage::fromData(
							reinterpret_cast<const uchar*>(bytes.data()),
							int(bytes.size()));
					}
				}
			}
			const auto timeline = session.GetTimelineProperties();
			if (timeline) {
				track.position = Milliseconds(timeline.Position());
				track.duration = Milliseconds(
					timeline.EndTime() - timeline.StartTime());
			}
			result = std::move(track);
		});
		crl::on_main([=] {
			done(result);
		});
	});
#else // Q_OS_WIN
	done(std::nullopt);
#endif // Q_OS_WIN
}

void Send(Command command) {
#ifdef Q_OS_WIN
	if (!Supported()) {
		return;
	}
	crl::async([=] {
		InitThread();
		base::WinRT::Try([&] {
			const auto session = ChooseSession(nullptr);
			if (!session) {
				return;
			}
			switch (command) {
			case Command::Toggle:
				session.TryTogglePlayPauseAsync().get();
				break;
			case Command::Next:
				session.TrySkipNextAsync().get();
				break;
			case Command::Previous:
				session.TrySkipPreviousAsync().get();
				break;
			}
		});
	});
#endif // Q_OS_WIN
}

} // namespace Tristegram::SystemMedia
