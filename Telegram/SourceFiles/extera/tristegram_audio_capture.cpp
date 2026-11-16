/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_audio_capture.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif // Q_OS_WIN

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace Tristegram::AudioCapture {
namespace {

// Keep at most this much not taken yet, in case nobody takes it.
constexpr auto kMaxBufferedFrames = kFrequency * 10;

} // namespace

class Capture::Source final {
public:
	explicit Source(bool loopback);
	~Source();

	[[nodiscard]] bool failed() const {
		return _failed.load();
	}

	// Adds up to `frames` stereo frames into `mix`, returns how many.
	int mixInto(float *mix, int frames, float gain);

private:
	void run(bool loopback);
	void push(const float *stereo, int frames, int rate);

	std::thread _thread;
	std::atomic<bool> _stop = false;
	std::atomic<bool> _failed = false;
	std::mutex _mutex;
	std::vector<float> _buffer;

	// Linear resampling state.
	double _position = 0.;
	float _last[2] = { 0.f, 0.f };

};

Capture::Source::Source(bool loopback) {
#ifdef Q_OS_WIN
	_thread = std::thread([=] { run(loopback); });
#else // Q_OS_WIN
	_failed = true;
#endif // Q_OS_WIN
}

Capture::Source::~Source() {
	_stop = true;
	if (_thread.joinable()) {
		_thread.join();
	}
}

int Capture::Source::mixInto(float *mix, int frames, float gain) {
	auto lock = std::unique_lock(_mutex);
	const auto available = int(_buffer.size() / kChannels);
	const auto count = std::min(available, frames);
	for (auto i = 0; i != count * kChannels; ++i) {
		mix[i] += _buffer[i] * gain;
	}
	_buffer.erase(_buffer.begin(), _buffer.begin() + count * kChannels);
	return count;
}

void Capture::Source::push(const float *stereo, int frames, int rate) {
	// From the device rate to 48 kHz, linear is enough for a recording.
	const auto step = double(rate) / kFrequency;
	auto converted = std::vector<float>();
	converted.reserve(size_t(frames / step + 4) * kChannels);
	while (_position < frames) {
		const auto index = int(_position);
		const auto fraction = float(_position - index);
		for (auto c = 0; c != kChannels; ++c) {
			const auto previous = (index == 0)
				? _last[c]
				: stereo[(index - 1) * kChannels + c];
			const auto current = stereo[index * kChannels + c];
			converted.push_back(previous + (current - previous) * fraction);
		}
		_position += step;
	}
	_position -= frames;
	if (frames > 0) {
		_last[0] = stereo[(frames - 1) * kChannels];
		_last[1] = stereo[(frames - 1) * kChannels + 1];
	}
	auto lock = std::unique_lock(_mutex);
	_buffer.insert(_buffer.end(), converted.begin(), converted.end());
	const auto limit = size_t(kMaxBufferedFrames) * kChannels;
	if (_buffer.size() > limit) {
		_buffer.erase(_buffer.begin(), _buffer.end() - limit);
	}
}

void Capture::Source::run(bool loopback) {
#ifdef Q_OS_WIN
	const auto coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	IMMDeviceEnumerator *enumerator = nullptr;
	IMMDevice *device = nullptr;
	IAudioClient *client = nullptr;
	IAudioCaptureClient *capture = nullptr;
	WAVEFORMATEX *format = nullptr;
	auto running = false;
	const auto cleanup = [&] {
		if (running && client) {
			client->Stop();
		}
		if (capture) {
			capture->Release();
		}
		if (client) {
			client->Release();
		}
		if (device) {
			device->Release();
		}
		if (enumerator) {
			enumerator->Release();
		}
		if (format) {
			CoTaskMemFree(format);
		}
		if (SUCCEEDED(coInit)) {
			CoUninitialize();
		}
	};
	const auto fail = [&] {
		_failed = true;
		cleanup();
	};
	if (FAILED(CoCreateInstance(
			__uuidof(MMDeviceEnumerator),
			nullptr,
			CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator),
			reinterpret_cast<void**>(&enumerator)))) {
		return fail();
	}
	if (FAILED(enumerator->GetDefaultAudioEndpoint(
			loopback ? eRender : eCapture,
			eConsole,
			&device))) {
		return fail();
	}
	if (FAILED(device->Activate(
			__uuidof(IAudioClient),
			CLSCTX_ALL,
			nullptr,
			reinterpret_cast<void**>(&client)))) {
		return fail();
	}
	if (FAILED(client->GetMixFormat(&format)) || !format) {
		return fail();
	}
	const auto channels = int(format->nChannels);
	const auto rate = int(format->nSamplesPerSec);
	const auto bits = int(format->wBitsPerSample);
	auto isFloat = (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
	auto isPcm = (format->wFormatTag == WAVE_FORMAT_PCM);
	if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const auto extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
			format);
		// KSDATAFORMAT_SUBTYPE_* keep the format tag in Data1.
		isFloat = (extensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT);
		isPcm = (extensible->SubFormat.Data1 == WAVE_FORMAT_PCM);
	}
	const auto supported = (channels > 0)
		&& (rate > 0)
		&& ((isFloat && bits == 32)
			|| (isPcm && (bits == 16 || bits == 24 || bits == 32)));
	if (!supported) {
		return fail();
	}
	constexpr auto kBufferDuration = REFERENCE_TIME(2'000'000); // 200 ms
	if (FAILED(client->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
			kBufferDuration,
			0,
			format,
			nullptr))) {
		return fail();
	}
	if (FAILED(client->GetService(
			__uuidof(IAudioCaptureClient),
			reinterpret_cast<void**>(&capture)))) {
		return fail();
	}
	if (FAILED(client->Start())) {
		return fail();
	}
	running = true;

	const auto bytesPerSample = bits / 8;
	const auto sample = [&](const BYTE *data, int index) -> float {
		const auto at = data + index * bytesPerSample;
		if (isFloat) {
			auto value = 0.f;
			std::memcpy(&value, at, sizeof(float));
			return value;
		} else if (bits == 16) {
			auto value = int16_t(0);
			std::memcpy(&value, at, sizeof(int16_t));
			return value / 32768.f;
		} else if (bits == 24) {
			const auto value = int32_t(
				(uint32_t(at[0]) << 8)
				| (uint32_t(at[1]) << 16)
				| (uint32_t(at[2]) << 24)) >> 8;
			return value / 8388608.f;
		}
		auto value = int32_t(0);
		std::memcpy(&value, at, sizeof(int32_t));
		return value / 2147483648.f;
	};
	auto stereo = std::vector<float>();
	while (!_stop) {
		Sleep(10);
		auto packet = UINT32(0);
		while (!_stop
			&& SUCCEEDED(capture->GetNextPacketSize(&packet))
			&& packet > 0) {
			BYTE *data = nullptr;
			auto frames = UINT32(0);
			auto flags = DWORD(0);
			if (FAILED(capture->GetBuffer(
					&data,
					&frames,
					&flags,
					nullptr,
					nullptr))) {
				break;
			}
			stereo.assign(size_t(frames) * kChannels, 0.f);
			if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
				for (auto i = 0; i != int(frames); ++i) {
					const auto base = i * channels;
					const auto left = sample(data, base);
					const auto right = (channels > 1)
						? sample(data, base + 1)
						: left;
					stereo[i * kChannels] = left;
					stereo[i * kChannels + 1] = right;
				}
			}
			capture->ReleaseBuffer(frames);
			push(stereo.data(), int(frames), rate);
		}
	}
	cleanup();
#endif // Q_OS_WIN
}

bool Supported() {
#ifdef Q_OS_WIN
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

Capture::Capture(bool system, bool microphone)
: _start(crl::now()) {
	if (system) {
		_sources.push_back(std::make_unique<Source>(true));
	}
	if (microphone) {
		_sources.push_back(std::make_unique<Source>(false));
	}
}

Capture::~Capture() = default;

bool Capture::started() const {
	return ranges::any_of(_sources, [](const auto &source) {
		return !source->failed();
	});
}

std::vector<float> Capture::take() {
	// The clock decides how much there is: the loopback gives nothing
	// while the computer is silent, that becomes silence in the file.
	const auto elapsed = crl::now() - _start;
	const auto should = int64(elapsed) * kFrequency / 1000;
	const auto frames = int(std::clamp(
		should - _produced,
		int64(0),
		int64(kFrequency * 2)));
	if (frames <= 0) {
		return {};
	}
	// Leave a little in the buffers, so a late packet is not cut off.
	auto result = std::vector<float>(size_t(frames) * kChannels, 0.f);
	const auto gain = (_sources.size() > 1) ? 0.8f : 1.f;
	for (const auto &source : _sources) {
		source->mixInto(result.data(), frames, gain);
	}
	for (auto &value : result) {
		value = std::clamp(value, -1.f, 1.f);
	}
	_produced += frames;
	return result;
}

} // namespace Tristegram::AudioCapture
