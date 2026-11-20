/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_sounds.h"

#include "extera/extera_config.h"
#include "lang/lang_keys.h"

#include <QtCore/QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif // Q_OS_WIN

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace Tristegram::Sounds {
namespace {

constexpr auto kRate = 44100;
constexpr auto kPi = 3.14159265358979323846;

struct Tone {
	double from = 440.; // Hz at the start
	double to = 440.; // Hz at the end
	double length = 0.05; // seconds
	double volume = 0.25;
	bool square = false;
	double decay = 6.; // how fast it fades
};

void AppendTone(std::vector<int16_t> &samples, const Tone &tone) {
	const auto count = int(tone.length * kRate);
	auto phase = 0.;
	for (auto i = 0; i != count; ++i) {
		const auto t = i / double(count);
		const auto frequency = tone.from + (tone.to - tone.from) * t;
		phase += 2 * kPi * frequency / kRate;
		auto value = tone.square
			? ((std::sin(phase) >= 0.) ? 1. : -1.) * 0.6
			: std::sin(phase);
		// Soft attack, then an exponential fade.
		const auto attack = std::min(1., i / (kRate * 0.004));
		value *= tone.volume * attack * std::exp(-tone.decay * t);
		samples.push_back(int16_t(std::clamp(value, -1., 1.) * 32767));
	}
}

[[nodiscard]] QByteArray Wave(const std::vector<int16_t> &samples) {
	auto result = QByteArray();
	const auto data = int(samples.size() * 2);
	const auto put32 = [&](uint32_t value) {
		result.append(reinterpret_cast<const char*>(&value), 4);
	};
	const auto put16 = [&](uint16_t value) {
		result.append(reinterpret_cast<const char*>(&value), 2);
	};
	result.append("RIFF", 4);
	put32(36 + data);
	result.append("WAVEfmt ", 8);
	put32(16);
	put16(1); // PCM
	put16(1); // mono
	put32(kRate);
	put32(kRate * 2);
	put16(2);
	put16(16);
	result.append("data", 4);
	put32(data);
	result.append(
		reinterpret_cast<const char*>(samples.data()),
		data);
	return result;
}

[[nodiscard]] QByteArray Make(int pack, Event event) {
	auto samples = std::vector<int16_t>();
	const auto send = (event == Event::Send);
	switch (pack) {
	case 1: // Soft: a gentle rising blip.
		if (send) {
			AppendTone(samples, { 880., 1320., 0.07, 0.22, false, 5. });
		} else {
			AppendTone(samples, { 660., 700., 0.045, 0.15, false, 7. });
		}
		break;
	case 2: // Bubbles: a drop going up.
		if (send) {
			AppendTone(samples, { 300., 900., 0.065, 0.28, false, 4. });
		} else {
			AppendTone(samples, { 500., 760., 0.04, 0.2, false, 5. });
		}
		break;
	case 3: // Retro: two 8-bit notes.
		if (send) {
			AppendTone(samples, { 988., 988., 0.04, 0.12, true, 1. });
			AppendTone(samples, { 1319., 1319., 0.06, 0.12, true, 3. });
		} else {
			AppendTone(samples, { 784., 784., 0.035, 0.1, true, 2. });
		}
		break;
	case 4: // Glass: a short ring.
		if (send) {
			AppendTone(samples, { 2093., 2093., 0.16, 0.16, false, 9. });
		} else {
			AppendTone(samples, { 1568., 1568., 0.09, 0.12, false, 10. });
		}
		break;
	}
	return samples.empty() ? QByteArray() : Wave(samples);
}

} // namespace

void Play(Event event) {
#ifdef Q_OS_WIN
	const auto pack = Extera::Int(u"tristegram_sounds"_q);
	if (pack <= 0 || pack > 4) {
		return;
	}
	// PlaySound reads the memory while it plays, so the sounds stay.
	static auto cache = std::map<std::pair<int, int>, QByteArray>();
	auto &data = cache[{ pack, int(event) }];
	if (data.isEmpty()) {
		data = Make(pack, event);
	}
	if (!data.isEmpty()) {
		PlaySoundW(
			reinterpret_cast<LPCWSTR>(data.constData()),
			nullptr,
			SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
	}
#endif // Q_OS_WIN
}

std::vector<QString> PackNames() {
	return {
		tr::lng_tgs_sounds_off(tr::now),
		tr::lng_tgs_sounds_soft(tr::now),
		tr::lng_tgs_sounds_bubbles(tr::now),
		tr::lng_tgs_sounds_retro(tr::now),
		tr::lng_tgs_sounds_glass(tr::now),
	};
}

} // namespace Tristegram::Sounds
