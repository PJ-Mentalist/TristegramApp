/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_live_wallpaper.h"

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "core/file_utilities.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "media/clip/media_clip_reader.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"
#include "styles/style_basic.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QStandardPaths>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>

#include <cmath>
#include <vector>

namespace Tristegram {
namespace {

constexpr auto kPi = 3.14159265358979;

[[nodiscard]] double Hash01(uint32 seed) {
	seed ^= seed >> 16;
	seed *= 0x7FEB352DU;
	seed ^= seed >> 15;
	seed *= 0x846CA68BU;
	seed ^= seed >> 16;
	return (seed & 0xFFFFFF) / double(0x1000000);
}

[[nodiscard]] QColor Mix(const QColor &a, const QColor &b, double t) {
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * t,
		a.greenF() + (b.greenF() - a.greenF()) * t,
		a.blueF() + (b.blueF() - a.blueF()) * t);
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] std::pair<QColor, QColor> SceneColors(
		const std::vector<QColor> &colors) {
	// Light versions of the background gradient, so the scene follows it.
	if (colors.size() >= 2) {
		return { colors[0].lighter(150), colors[1].lighter(150) };
	} else if (colors.size() == 1) {
		return { colors[0].lighter(160), colors[0].lighter(120) };
	}
	const auto logo = CurrentLogoStyle();
	return { logo.from, logo.to };
}

void PaintDove(
		QPainter &p,
		QPointF center,
		double size,
		double flap,
		QColor color) {
	// A simple bird: a body and two wings that go up and down.
	const auto wing = size * (0.15 + 0.85 * flap);
	auto path = QPainterPath();
	path.moveTo(center.x() - size, center.y() - wing);
	path.quadTo(
		center.x() - size * 0.45,
		center.y() - wing * 0.35,
		center.x(),
		center.y() + size * 0.08);
	path.quadTo(
		center.x() + size * 0.45,
		center.y() - wing * 0.35,
		center.x() + size,
		center.y() - wing);
	path.quadTo(
		center.x() + size * 0.4,
		center.y() + size * 0.12,
		center.x(),
		center.y() + size * 0.3);
	path.quadTo(
		center.x() - size * 0.4,
		center.y() + size * 0.12,
		center.x() - size,
		center.y() - wing);
	p.setBrush(color);
	p.drawPath(path);
	p.drawEllipse(
		QPointF(center.x() + size * 0.08, center.y() + size * 0.1),
		size * 0.14,
		size * 0.1);
}

void PaintDoves(
		QPainter &p,
		QSize fill,
		double t,
		std::pair<QColor, QColor> colors) {
	const auto count = std::clamp(fill.width() / 140, 5, 12);
	for (auto i = 0; i != count; ++i) {
		const auto seed = uint32(i * 7919 + 13);
		const auto speed = 40. + Hash01(seed) * 50.;
		const auto size = style::ConvertScale(1)
			* (10. + Hash01(seed + 1) * 16.);
		const auto span = fill.width() + size * 6;
		const auto x = std::fmod(Hash01(seed + 2) * span + t * speed, span)
			- size * 3;
		const auto baseY = fill.height() * (0.08 + 0.8 * Hash01(seed + 3));
		const auto y = baseY
			+ std::sin(t * 0.7 + i) * fill.height() * 0.03;
		const auto flap = 0.5 + 0.5 * std::sin(t * (7. + Hash01(seed + 4) * 4.) + i);
		const auto color = WithAlpha(
			Mix(colors.first, colors.second, Hash01(seed + 5)),
			0.35 + 0.35 * Hash01(seed + 6));
		PaintDove(p, QPointF(x, y), size, flap, color);
	}
}

void PaintStars(
		QPainter &p,
		QSize fill,
		double t,
		std::pair<QColor, QColor> colors) {
	const auto count = std::clamp(fill.width() * fill.height() / 9000, 30, 140);
	for (auto i = 0; i != count; ++i) {
		const auto seed = uint32(i * 104729 + 7);
		const auto x = Hash01(seed) * fill.width();
		const auto drift = t * (4. + Hash01(seed + 1) * 10.);
		const auto y = std::fmod(
			Hash01(seed + 2) * fill.height() - drift + fill.height() * 4,
			double(fill.height()));
		const auto twinkle = 0.5
			+ 0.5 * std::sin(t * (1.5 + Hash01(seed + 3) * 3.) + i);
		const auto r = style::ConvertScale(1)
			* (0.8 + Hash01(seed + 4) * 2.2) * (0.6 + 0.4 * twinkle);
		const auto color = Mix(colors.first, colors.second, Hash01(seed + 5));
		p.setBrush(WithAlpha(color, 0.12 * twinkle));
		p.drawEllipse(QPointF(x, y), r * 3.5, r * 3.5);
		p.setBrush(WithAlpha(QColor(255, 255, 255), 0.35 + 0.55 * twinkle));
		p.drawEllipse(QPointF(x, y), r, r);
	}
}

void PaintBubbles(
		QPainter &p,
		QSize fill,
		double t,
		std::pair<QColor, QColor> colors) {
	const auto count = std::clamp(fill.width() / 60, 10, 36);
	for (auto i = 0; i != count; ++i) {
		const auto seed = uint32(i * 15485863 + 3);
		const auto r = style::ConvertScale(1) * (8. + Hash01(seed) * 34.);
		const auto speed = 14. + Hash01(seed + 1) * 26.;
		const auto span = fill.height() + r * 4;
		const auto y = fill.height() + r * 2
			- std::fmod(Hash01(seed + 2) * span + t * speed, span);
		const auto x = Hash01(seed + 3) * fill.width()
			+ std::sin(t * 0.8 + i) * r * 0.8;
		const auto color = Mix(colors.first, colors.second, Hash01(seed + 4));
		auto gradient = QRadialGradient(
			QPointF(x - r * 0.35, y - r * 0.35),
			r * 1.3);
		gradient.setColorAt(0., WithAlpha(QColor(255, 255, 255), 0.30));
		gradient.setColorAt(0.5, WithAlpha(color, 0.16));
		gradient.setColorAt(1., WithAlpha(color, 0.04));
		p.setBrush(gradient);
		p.setPen(QPen(WithAlpha(QColor(255, 255, 255), 0.18), 1.));
		p.drawEllipse(QPointF(x, y), r, r);
		p.setPen(Qt::NoPen);
	}
}

void PaintCode(
		QPainter &p,
		QSize fill,
		double t,
		std::pair<QColor, QColor> colors) {
	// A terminal of an AI coding agent: prompts, tool calls and diffs that
	// scroll slowly upwards.
	static const auto lines = std::vector<QString>{
		u"> claude"_q,
		u"✻ Welcome to Claude Code!"_q,
		u"> make the chat list rounded"_q,
		u"● Read(dialogs_layout.cpp)"_q,
		u"● Update(dialogs_layout.cpp)"_q,
		u"  + p.drawRoundedRect(rounded, radius, radius);"_q,
		u"  - p.fillRect(geometry, bg);"_q,
		u"● Bash(cmake --build out)"_q,
		u"  ⎿  Build succeeded"_q,
		u"> add a vinyl to the player"_q,
		u"● Write(tristegram_music.cpp)"_q,
		u"  + PaintVinyl(p, disc, cover, angle);"_q,
		u"✻ Thinking…"_q,
		u"> ship it"_q,
		u"● git push origin tristegram-ui"_q,
		u"  ⎿  Done ✓"_q,
	};
	const auto font = style::font(
		style::ConvertScale(13),
		style::FontFlags(style::FontFlag::Monospace),
		0);
	p.setFont(font->f);
	const auto lineHeight = font->height * 1.6;
	const auto total = lineHeight * lines.size();
	const auto columns = std::max(1, fill.width() / style::ConvertScale(420));
	const auto columnWidth = fill.width() / double(columns);
	const auto accent = QColor(0xD9, 0x77, 0x57); // Terminal orange.
	for (auto c = 0; c != columns; ++c) {
		const auto shift = std::fmod(
			t * (14. + c * 3.) + c * total * 0.37,
			total);
		for (auto k = -1; k <= int(fill.height() / total) + 1; ++k) {
			for (auto i = 0; i != int(lines.size()); ++i) {
				const auto y = k * total + i * lineHeight - shift;
				if (y < -lineHeight || y > fill.height() + lineHeight) {
					continue;
				}
				const auto &text = lines[i];
				const auto edge = std::min(y, fill.height() - y)
					/ (fill.height() * 0.25);
				const auto fade = std::clamp(edge, 0., 1.);
				auto color = text.startsWith('>')
					? accent
					: text.startsWith(u"  +"_q)
					? QColor(0x34, 0xC7, 0x59)
					: text.startsWith(u"  -"_q)
					? QColor(0xFF, 0x45, 0x3A)
					: Mix(colors.first, colors.second, (i % 5) / 5.);
				p.setPen(WithAlpha(color, 0.42 * fade));
				p.drawText(
					QPointF(c * columnWidth + style::ConvertScale(24), y),
					text);
			}
		}
	}
	p.setPen(Qt::NoPen);
}

void PaintAurora(
		QPainter &p,
		QSize fill,
		double t,
		std::pair<QColor, QColor> colors) {
	for (auto band = 0; band != 3; ++band) {
		auto path = QPainterPath();
		const auto base = fill.height() * (0.25 + band * 0.22);
		const auto amplitude = fill.height() * (0.06 + band * 0.02);
		path.moveTo(0, fill.height());
		for (auto x = 0; x <= fill.width(); x += 12) {
			const auto y = base
				+ std::sin(x / 180. + t * (0.35 + band * 0.12) + band) * amplitude
				+ std::sin(x / 77. - t * 0.5) * amplitude * 0.35;
			path.lineTo(x, y);
		}
		path.lineTo(fill.width(), fill.height());
		path.closeSubpath();
		auto gradient = QLinearGradient(0, base - amplitude, 0, fill.height());
		const auto color = Mix(colors.first, colors.second, band / 2.);
		gradient.setColorAt(0., WithAlpha(color, 0.22));
		gradient.setColorAt(1., WithAlpha(color, 0.));
		p.setBrush(gradient);
		p.drawPath(path);
	}
}

// Your own GIF or MP4, played in a loop.
class VideoWallpaper final {
public:
	static VideoWallpaper &Instance() {
		static const auto result = new VideoWallpaper();
		return *result;
	}

	void paint(QPainter &p, QSize fill) {
		const auto path = Extera::String(u"tristegram_live_file"_q);
		const auto version = Extera::Int(u"tristegram_live_version"_q);
		if (path != _path || version != _version) {
			_path = path;
			_version = version;
			_reader = nullptr;
			_failed = false;
			if (!path.isEmpty() && QFileInfo::exists(path)) {
				_reader = ::Media::Clip::MakeReader(
					path,
					[=](::Media::Clip::Notification) { notification(); });
			}
		}
		if (!_reader || _failed) {
			return;
		}
		if (_reader->state() == ::Media::Clip::State::Error) {
			_failed = true;
			_reader = nullptr;
			return;
		}
		if (!_reader->ready()) {
			return;
		}
		const auto source = QSize(_reader->width(), _reader->height());
		if (source.isEmpty()) {
			return;
		}
		const auto cover = source.scaled(fill, Qt::KeepAspectRatioByExpanding);
		auto request = ::Media::Clip::FrameRequest();
		request.frame = cover;
		request.outer = cover;
		request.factor = style::DevicePixelRatio();
		request.keepAlpha = false;
		if (!_reader->started() || _request.frame != cover) {
			_request = request;
			_reader->start(request);
		}
		const auto frame = _reader->current(request, crl::now());
		if (frame.isNull()) {
			return;
		}
		const auto left = (fill.width() - cover.width()) / 2;
		const auto top = (fill.height() - cover.height()) / 2;
		p.drawImage(QRect(QPoint(left, top), cover), frame);
	}

private:
	void notification() {
	}

	QString _path;
	int _version = -1;
	::Media::Clip::ReaderPointer _reader;
	::Media::Clip::FrameRequest _request;
	bool _failed = false;

};

// Repaints the chat backgrounds while a live wallpaper is on.
class Driver final {
public:
	static Driver &Instance() {
		static const auto result = new Driver();
		return *result;
	}

	void add(not_null<QWidget*> widget) {
		for (const auto &existing : _widgets) {
			if (existing.data() == widget.get()) {
				return;
			}
		}
		_widgets.push_back(QPointer<QWidget>(widget.get()));
		ensureRunning();
	}

	void ensureRunning() {
		const auto fps = std::clamp(
			Extera::Int(u"tristegram_live_fps"_q),
			10,
			60);
		if (!_timer.isActive() || _fps != fps) {
			_fps = fps;
			_timer.callEach(1000 / fps);
		}
	}

private:
	Driver() : _timer([=] { tick(); }) {
	}

	void tick() {
		if (!LiveWallpaperEnabled()) {
			_timer.cancel();
			return;
		}
		ensureRunning();
		const auto active = QApplication::activeWindow();
		_widgets.erase(ranges::remove_if(_widgets, [](const QPointer<QWidget> &w) {
			return !w;
		}), _widgets.end());
		for (const auto &widget : _widgets) {
			// Only the window you look at, to save the battery.
			if (widget->isVisible() && widget->window() == active) {
				widget->update();
			}
		}
	}

	std::vector<QPointer<QWidget>> _widgets;
	base::Timer _timer;
	int _fps = 0;

};

} // namespace

bool LiveWallpaperEnabled() {
	return (Extera::Int(u"tristegram_live_kind"_q) > 0)
		&& !Extera::Bool(u"tristegram_eco"_q);
}

void RegisterLiveWallpaperWidget(not_null<QWidget*> widget) {
	if (LiveWallpaperEnabled()) {
		Driver::Instance().add(widget);
	}
}

void PaintLiveWallpaper(
		QPainter &p,
		const std::vector<QColor> &colors,
		QSize fill,
		QRect clip) {
	if (!LiveWallpaperEnabled() || fill.isEmpty()) {
		return;
	}
	const auto kind = LiveWallpaperKind(Extera::Int(u"tristegram_live_kind"_q));
	const auto t = crl::now() / 1000.;
	const auto sceneColors = SceneColors(colors);
	p.save();
	p.setClipRect(clip, Qt::IntersectClip);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	switch (kind) {
	case LiveWallpaperKind::Doves: PaintDoves(p, fill, t, sceneColors); break;
	case LiveWallpaperKind::Stars: PaintStars(p, fill, t, sceneColors); break;
	case LiveWallpaperKind::Bubbles: PaintBubbles(p, fill, t, sceneColors); break;
	case LiveWallpaperKind::Code: PaintCode(p, fill, t, sceneColors); break;
	case LiveWallpaperKind::Aurora: PaintAurora(p, fill, t, sceneColors); break;
	case LiveWallpaperKind::Video:
		VideoWallpaper::Instance().paint(p, fill);
		break;
	case LiveWallpaperKind::None: break;
	}
	p.restore();
}

void ChooseLiveWallpaperFile(not_null<Window::SessionController*> controller) {
	FileDialog::GetOpenPath(
		controller->widget().get(),
		tr::lng_tgl_choose(tr::now),
		u"GIF / MP4 (*.gif *.mp4 *.webm *.mov)"_q,
		crl::guard(controller.get(), [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty()) {
				return;
			}
			const auto source = result.paths.front();
			// Keep a copy, so the wallpaper stays when the file moves.
			const auto folder = QStandardPaths::writableLocation(
				QStandardPaths::AppDataLocation) + u"/tristegram"_q;
			QDir().mkpath(folder);
			const auto target = folder
				+ u"/live_wallpaper."_q
				+ QFileInfo(source).suffix().toLower();
			QFile::remove(target);
			const auto path = QFile::copy(source, target) ? target : source;
			auto &config = Extera::Config::Instance();
			config.setString(u"tristegram_live_file"_q, path);
			config.setInt(
				u"tristegram_live_version"_q,
				Extera::Int(u"tristegram_live_version"_q) + 1);
			Extera::Config::Instance().setInt(
				u"tristegram_live_kind"_q,
				int(LiveWallpaperKind::Video));
			controller->widget()->update();
		}));
}

} // namespace Tristegram
