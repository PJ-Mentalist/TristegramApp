/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <memory>
#include <vector>

namespace Tristegram::AudioCapture {

inline constexpr auto kFrequency = 48000;
inline constexpr auto kChannels = 2;

[[nodiscard]] bool Supported();

// Records what the computer plays (everything you hear, a voice chat
// included) and, if asked, the microphone, each on its own thread.
class Capture final {
public:
	Capture(bool system, bool microphone);
	~Capture();

	[[nodiscard]] bool started() const;

	// Interleaved stereo samples for the time passed since the previous
	// call, mixed, with silence where a source gave nothing.
	[[nodiscard]] std::vector<float> take();

private:
	class Source;

	std::vector<std::unique_ptr<Source>> _sources;
	crl::time _start = 0;
	int64 _produced = 0;

};

} // namespace Tristegram::AudioCapture
