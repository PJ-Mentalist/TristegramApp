/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_names.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"

#include <QtGui/QLinearGradient>

namespace Tristegram::Names {
namespace {

[[nodiscard]] int Mode() {
	const auto mode = Extera::Int(u"tristegram_name_mode"_q);
	if (!mode && Extera::Bool(u"name_accent"_q)) {
		return 1;
	}
	return (mode >= 0 && mode < kModeCount) ? mode : 0;
}

[[nodiscard]] QColor Value(const QString &key, QColor fallback) {
	const auto color = QColor(Extera::String(key));
	return color.isValid() ? color : fallback;
}

[[nodiscard]] std::pair<QColor, QColor> Colors(int mode) {
	if (mode == 3) {
		const auto logo = Tristegram::CurrentLogoStyle();
		return { logo.from, logo.to };
	}
	return {
		Value(u"tristegram_name_c1"_q, QColor(0xF0, 0x7A, 0x0C)),
		Value(u"tristegram_name_c2"_q, QColor(0xE8, 0x31, 0x8A)),
	};
}

} // namespace

std::optional<QColor> Override(const QColor &accent) {
	switch (Mode()) {
	case 1: return accent;
	case 2:
	case 3:
	case 4: return Colors(Mode()).first;
	}
	return std::nullopt;
}

std::optional<QPen> GradientPen(int left, int width) {
	const auto mode = Mode();
	if (mode != 3 && mode != 4) {
		return std::nullopt;
	}
	const auto [from, to] = Colors(mode);
	auto gradient = QLinearGradient(left, 0, left + std::max(width, 1), 0);
	gradient.setColorAt(0., from);
	gradient.setColorAt(1., to);
	return QPen(QBrush(gradient), 1.);
}

} // namespace Tristegram::Names
