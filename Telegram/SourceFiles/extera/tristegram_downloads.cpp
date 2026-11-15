/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_downloads.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_download_manager.h"
#include "data/data_photo.h"
#include "extera/tristegram_logo.h"
#include "history/history_item.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QFileInfo>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Tristegram {
namespace {

constexpr auto kRefreshInterval = crl::time(250);
constexpr auto kAnimationInterval = crl::time(40);
constexpr auto kRecentLimit = 12;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] style::font SmallFont() {
	return style::font(Px(12), style::FontFlags(), 0);
}

[[nodiscard]] style::font BigFont() {
	return style::font(Px(26), style::FontFlags(), st::semiboldFont->family());
}

[[nodiscard]] QString Extension(const QString &name) {
	const auto dot = name.lastIndexOf('.');
	if (dot < 0 || dot + 1 >= name.size() || name.size() - dot > 6) {
		return QString();
	}
	return name.mid(dot + 1).toUpper();
}

// A color per kind of file, so that the list is easy to scan.
[[nodiscard]] QColor TileColor(const QString &ext, bool photo) {
	if (photo) {
		return QColor(0x34, 0xC7, 0x59);
	}
	static const auto kMap = std::vector<std::pair<QStringList, QColor>>{
		{ { u"JPG"_q, u"JPEG"_q, u"PNG"_q, u"GIF"_q, u"WEBP"_q, u"HEIC"_q, u"BMP"_q }, QColor(0x34, 0xC7, 0x59) },
		{ { u"MP4"_q, u"MOV"_q, u"MKV"_q, u"AVI"_q, u"WEBM"_q }, QColor(0xAF, 0x52, 0xDE) },
		{ { u"MP3"_q, u"OGG"_q, u"FLAC"_q, u"WAV"_q, u"M4A"_q, u"OPUS"_q }, QColor(0xFF, 0x2D, 0x55) },
		{ { u"ZIP"_q, u"RAR"_q, u"7Z"_q, u"TAR"_q, u"GZ"_q }, QColor(0xFF, 0x9F, 0x0A) },
		{ { u"PDF"_q }, QColor(0xFF, 0x45, 0x3A) },
		{ { u"DOC"_q, u"DOCX"_q, u"TXT"_q, u"RTF"_q }, QColor(0x0A, 0x84, 0xFF) },
		{ { u"XLS"_q, u"XLSX"_q, u"CSV"_q }, QColor(0x30, 0xB0, 0x50) },
		{ { u"EXE"_q, u"MSI"_q, u"APK"_q, u"DMG"_q }, QColor(0x5E, 0x5C, 0xE6) },
		{ { u"PY"_q, u"JS"_q, u"CPP"_q, u"H"_q, u"JSON"_q, u"PLUGIN"_q }, QColor(0x64, 0xD2, 0xFF) },
	};
	for (const auto &[list, color] : kMap) {
		if (list.contains(ext)) {
			return color;
		}
	}
	return QColor(0x8E, 0x8E, 0x93);
}

[[nodiscard]] QString FormatSpeed(double bytesPerSecond) {
	if (bytesPerSecond < 1.) {
		return QString();
	}
	return Ui::FormatSizeText(int64(bytesPerSecond))
		+ tr::lng_tgd_per_second(tr::now);
}

[[nodiscard]] QString FormatLeft(int64 left, double speed) {
	if (speed < 1. || left <= 0) {
		return QString();
	}
	const auto seconds = int64(std::ceil(left / speed));
	if (seconds < 60) {
		return u"~"_q
			+ QString::number(std::max(seconds, int64(1)))
			+ ' '
			+ tr::lng_tgd_seconds(tr::now);
	} else if (seconds < 3600) {
		return u"~"_q
			+ QString::number((seconds + 30) / 60)
			+ ' '
			+ tr::lng_tgd_minutes(tr::now);
	}
	return u"~"_q
		+ QString::number(seconds / 3600)
		+ ' '
		+ tr::lng_tgd_hours(tr::now);
}

[[nodiscard]] QString JoinParts(const QStringList &parts) {
	auto result = QStringList();
	for (const auto &part : parts) {
		if (!part.isEmpty()) {
			result.push_back(part);
		}
	}
	return result.join(u" · "_q);
}

void PaintGradientBar(
		QPainter &p,
		QRectF rect,
		double progress,
		crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(st::windowFg->c, 0.08));
	p.drawRoundedRect(rect, radius, radius);
	progress = std::clamp(progress, 0., 1.);
	if (progress <= 0.) {
		return;
	}
	const auto style = CurrentLogoStyle();
	auto filled = rect;
	filled.setWidth(std::max(rect.height(), rect.width() * progress));
	auto gradient = QLinearGradient(filled.topLeft(), filled.topRight());
	gradient.setColorAt(0., style.from);
	gradient.setColorAt(1., style.to);
	p.setBrush(gradient);
	p.drawRoundedRect(filled, radius, radius);

	// A soft light running along the filled part while it downloads.
	if (progress < 1.) {
		const auto period = 1400.;
		const auto phase = std::fmod(double(now), period) / period;
		const auto width = std::max(filled.width() * 0.35, rect.height() * 4);
		const auto x = filled.x() - width + (filled.width() + width) * phase;
		auto shine = QLinearGradient(QPointF(x, 0), QPointF(x + width, 0));
		shine.setColorAt(0., QColor(255, 255, 255, 0));
		shine.setColorAt(0.5, QColor(255, 255, 255, 70));
		shine.setColorAt(1., QColor(255, 255, 255, 0));
		auto path = QPainterPath();
		path.addRoundedRect(filled, radius, radius);
		p.save();
		p.setClipPath(path);
		p.fillRect(filled, shine);
		p.restore();
	}
}

void PaintTile(
		QPainter &p,
		QRect rect,
		const QString &ext,
		bool photo) {
	auto hq = PainterHighQualityEnabler(p);
	const auto color = TileColor(ext, photo);
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setColorAt(0., color.lighter(118));
	gradient.setColorAt(1., color.darker(112));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	const auto radius = rect.height() * 0.28;
	p.drawRoundedRect(QRectF(rect), radius, radius);
	const auto text = photo
		? tr::lng_tgd_photo_short(tr::now)
		: ext.isEmpty()
		? tr::lng_tgd_file_short(tr::now)
		: ext;
	auto font = style::font(
		(text.size() > 3) ? Px(10) : Px(12),
		style::FontFlags(),
		st::semiboldFont->family());
	p.setFont(font->f);
	p.setPen(QColor(255, 255, 255));
	p.drawText(
		rect,
		Qt::AlignCenter,
		font->elided(text, rect.width() - Px(4)));
}

void PaintRoundIconButton(
		QPainter &p,
		QRect rect,
		const style::icon &icon,
		bool over) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(st::windowFg->c, over ? 0.12 : 0.06));
	p.drawEllipse(QRectF(rect));
	icon.paintInCenter(p, rect);
}

class DownloadsList final : public Ui::RpWidget {
public:
	DownloadsList(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Active {
		HistoryItem *item = nullptr;
		DocumentData *document = nullptr;
		PhotoData *photo = nullptr;
		Fn<void()> externalCancel;
		QString name;
		QString path;
		int64 ready = 0;
		int64 total = 0;
		double speed = 0.;
		bool done = false;
	};
	struct Recent {
		FullMsgId itemId;
		QString name;
		QString path;
		int64 size = 0;
		Data::DownloadDate started = 0;
	};
	enum class Hit {
		None,
		ActiveRow,
		Cancel,
		Open,
		Folder,
		RecentRow,
		RecentFolder,
		All,
		Clear,
	};
	struct Region {
		QRect rect;
		Hit hit = Hit::None;
		int index = -1;
	};
	struct Speed {
		int64 ready = 0;
		crl::time when = 0;
		double value = 0.;
	};

	void refresh();
	void animationTick();
	void recountRegions();
	void updateOver(QPoint position);
	void activate(const Region &region);
	[[nodiscard]] const Region *regionAt(QPoint position) const;
	[[nodiscard]] bool isOver(Hit hit, int index) const;

	void paintSummary(QPainter &p, QRect rect, crl::time now);
	void paintSectionLabel(QPainter &p, int top, const QString &text);
	void paintActive(QPainter &p, QRect rect, int index, crl::time now);
	void paintRecent(QPainter &p, QRect rect, int index);
	void paintEmpty(QPainter &p, QRect rect);
	void paintPill(
		QPainter &p,
		QRect rect,
		const QString &text,
		bool accent,
		bool over);

	[[nodiscard]] int summaryHeight() const;
	[[nodiscard]] int activeTop() const;
	[[nodiscard]] int recentTop() const;
	[[nodiscard]] int footerTop() const;

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	std::vector<Active> _active;
	std::vector<Recent> _recent;
	base::flat_map<const void*, Speed> _speeds;
	std::vector<Region> _regions;
	Region _over;
	Region _pressed;
	base::Timer _timer;
	base::Timer _animation;

};

DownloadsList::DownloadsList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _timer([=] { refresh(); })
, _animation([=] { animationTick(); }) {
	setMouseTracking(true);
	auto &manager = Core::App().downloadManager();
	rpl::merge(
		manager.loadingListChanges(),
		manager.loadedResolveDone(),
		manager.loadedAdded() | rpl::to_empty,
		manager.loadedRemoved() | rpl::to_empty
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
	refresh();
	_timer.callEach(kRefreshInterval);
	_animation.callEach(kAnimationInterval);
}

void DownloadsList::animationTick() {
	// Repaint often only while something downloads, for the bar shine.
	const auto loading = ranges::any_of(_active, [](const Active &a) {
		return !a.done;
	});
	if (loading && isVisible()) {
		update();
	}
}

void DownloadsList::refresh() {
	const auto now = crl::now();
	auto &manager = Core::App().downloadManager();
	auto active = std::vector<Active>();
	auto seen = base::flat_set<const void*>();
	for (const auto id : manager.loadingList()) {
		auto entry = Active();
		entry.item = id->object.item;
		entry.document = id->object.document;
		entry.photo = id->object.photo;
		entry.externalCancel = id->externalCancel;
		entry.path = id->path;
		entry.ready = id->ready;
		entry.total = id->total;
		entry.done = id->done;
		if (entry.document) {
			entry.name = entry.document->filename();
			if (!entry.done && entry.total <= 0) {
				entry.total = entry.document->size;
			}
		}
		if (entry.name.isEmpty() && !entry.path.isEmpty()) {
			entry.name = QFileInfo(entry.path).fileName();
		}
		if (entry.name.isEmpty()) {
			entry.name = entry.photo
				? tr::lng_tgd_photo(tr::now)
				: tr::lng_tgd_file(tr::now);
		}
		const auto key = static_cast<const void*>(entry.item);
		seen.emplace(key);
		auto &speed = _speeds[key];
		if (speed.when && now - speed.when < 200 && !entry.done) {
			entry.speed = speed.value;
			active.push_back(std::move(entry));
			continue;
		}
		if (speed.when && now > speed.when && entry.ready >= speed.ready) {
			const auto instant = (entry.ready - speed.ready) * 1000.
				/ double(now - speed.when);
			speed.value = speed.value
				? (speed.value * 0.7 + instant * 0.3)
				: instant;
		}
		if (entry.done) {
			speed.value = 0.;
		}
		speed.ready = entry.ready;
		speed.when = now;
		entry.speed = speed.value;
		active.push_back(std::move(entry));
	}
	auto stale = std::vector<const void*>();
	for (const auto &[key, speed] : _speeds) {
		if (!seen.contains(key)) {
			stale.push_back(key);
		}
	}
	for (const auto key : stale) {
		_speeds.remove(key);
	}
	// Downloading first, finished after them.
	std::stable_partition(active.begin(), active.end(), [](const Active &a) {
		return !a.done;
	});

	auto recent = std::vector<Recent>();
	for (const auto id : manager.loadedList()) {
		auto entry = Recent();
		entry.itemId = id->itemId;
		entry.path = id->path;
		entry.size = id->size;
		entry.started = id->started;
		entry.name = QFileInfo(id->path).fileName();
		if (entry.name.isEmpty()) {
			entry.name = tr::lng_tgd_file(tr::now);
		}
		recent.push_back(std::move(entry));
	}
	std::sort(recent.begin(), recent.end(), [](
			const Recent &a,
			const Recent &b) {
		return a.started > b.started;
	});
	if (recent.size() > kRecentLimit) {
		recent.resize(kRecentLimit);
	}

	const auto layoutChanged = (active.size() != _active.size())
		|| (recent.size() != _recent.size());
	_active = std::move(active);
	_recent = std::move(recent);
	if (layoutChanged && width() > 0) {
		resizeToWidth(width());
	}
	recountRegions();
	update();
}

int DownloadsList::summaryHeight() const {
	return Px(112);
}

int DownloadsList::activeTop() const {
	return Px(8) + summaryHeight() + Px(14);
}

int DownloadsList::recentTop() const {
	const auto rows = std::max(int(_active.size()), 1);
	return activeTop() + Px(30) + rows * Px(72) + Px(12);
}

int DownloadsList::footerTop() const {
	if (_recent.empty()) {
		return recentTop();
	}
	return recentTop() + Px(30) + int(_recent.size()) * Px(56) + Px(12);
}

int DownloadsList::resizeGetHeight(int newWidth) {
	return footerTop() + Px(40) + Px(16);
}

void DownloadsList::recountRegions() {
	_regions.clear();
	const auto side = Px(16);
	const auto w = width();
	auto top = activeTop() + Px(30);
	for (auto i = 0; i != int(_active.size()); ++i) {
		const auto row = QRect(side - Px(6), top, w - 2 * side + Px(12), Px(72));
		const auto &entry = _active[i];
		const auto button = Px(32);
		const auto by = top + (Px(72) - button) / 2;
		if (entry.done) {
			const auto folder = QRect(w - side - button, by, button, button);
			const auto openWidth = Px(84);
			const auto open = QRect(
				folder.x() - Px(8) - openWidth,
				by,
				openWidth,
				button);
			_regions.push_back({ folder, Hit::Folder, i });
			_regions.push_back({ open, Hit::Open, i });
		} else {
			_regions.push_back({
				QRect(w - side - button, by, button, button),
				Hit::Cancel,
				i,
			});
		}
		_regions.push_back({ row, Hit::ActiveRow, i });
		top += Px(72);
	}
	top = recentTop() + Px(30);
	for (auto i = 0; i != int(_recent.size()); ++i) {
		const auto row = QRect(side - Px(6), top, w - 2 * side + Px(12), Px(56));
		const auto button = Px(30);
		_regions.push_back({
			QRect(w - side - button, top + (Px(56) - button) / 2, button, button),
			Hit::RecentFolder,
			i,
		});
		_regions.push_back({ row, Hit::RecentRow, i });
		top += Px(56);
	}
	const auto footer = footerTop();
	const auto gap = Px(10);
	const auto half = (w - 2 * side - gap) / 2;
	_regions.push_back({ QRect(side, footer, half, Px(40)), Hit::All, -1 });
	_regions.push_back({
		QRect(side + half + gap, footer, half, Px(40)),
		Hit::Clear,
		-1,
	});
}

const DownloadsList::Region *DownloadsList::regionAt(QPoint position) const {
	for (const auto &region : _regions) {
		if (region.rect.contains(position)) {
			return &region;
		}
	}
	return nullptr;
}

bool DownloadsList::isOver(Hit hit, int index) const {
	return (_over.hit == hit) && (_over.index == index);
}

void DownloadsList::updateOver(QPoint position) {
	const auto found = regionAt(position);
	const auto now = found ? *found : Region();
	if (now.hit != _over.hit || now.index != _over.index) {
		_over = now;
		setCursor((_over.hit == Hit::None)
			? style::cur_default
			: style::cur_pointer);
		update();
	}
}

void DownloadsList::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
}

void DownloadsList::leaveEventHook(QEvent *e) {
	_over = Region();
	setCursor(style::cur_default);
	update();
}

void DownloadsList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	updateOver(e->pos());
	_pressed = _over;
}

void DownloadsList::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto pressed = std::exchange(_pressed, Region());
	updateOver(e->pos());
	if (pressed.hit != Hit::None
		&& pressed.hit == _over.hit
		&& pressed.index == _over.index) {
		activate(pressed);
	}
}

void DownloadsList::activate(const Region &region) {
	const auto showItem = [this](HistoryItem *item) {
		if (item && item->isHistoryEntry()) {
			const auto close = _close;
			_controller->showMessage(item);
			close();
		}
	};
	switch (region.hit) {
	case Hit::Cancel: {
		if (region.index < 0 || region.index >= int(_active.size())) {
			return;
		}
		const auto &entry = _active[region.index];
		if (entry.externalCancel) {
			entry.externalCancel();
		} else if (entry.document) {
			entry.document->cancel();
		} else if (entry.photo) {
			entry.photo->cancel();
		}
		refresh();
	} break;
	case Hit::Open: {
		if (region.index >= 0 && region.index < int(_active.size())) {
			const auto path = _active[region.index].path;
			if (!path.isEmpty()) {
				File::Launch(path);
			}
		}
	} break;
	case Hit::Folder: {
		if (region.index >= 0 && region.index < int(_active.size())) {
			const auto path = _active[region.index].path;
			if (!path.isEmpty()) {
				File::ShowInFolder(path);
			}
		}
	} break;
	case Hit::ActiveRow: {
		if (region.index >= 0 && region.index < int(_active.size())) {
			const auto &entry = _active[region.index];
			if (entry.done && !entry.path.isEmpty()) {
				File::Launch(entry.path);
			} else {
				showItem(entry.item);
			}
		}
	} break;
	case Hit::RecentRow: {
		if (region.index >= 0 && region.index < int(_recent.size())) {
			const auto path = _recent[region.index].path;
			if (!path.isEmpty() && QFileInfo::exists(path)) {
				File::Launch(path);
			}
		}
	} break;
	case Hit::RecentFolder: {
		if (region.index >= 0 && region.index < int(_recent.size())) {
			const auto path = _recent[region.index].path;
			if (!path.isEmpty()) {
				File::ShowInFolder(path);
			}
		}
	} break;
	case Hit::All: {
		const auto controller = _controller;
		const auto close = _close;
		close();
		controller->showSection(
			Info::Downloads::Make(controller->session().user()));
	} break;
	case Hit::Clear: {
		Core::App().downloadManager().clearIfFinished();
		refresh();
	} break;
	case Hit::None: break;
	}
}

void DownloadsList::paintPill(
		QPainter &p,
		QRect rect,
		const QString &text,
		bool accent,
		bool over) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	if (accent) {
		const auto style = CurrentLogoStyle();
		auto gradient = QLinearGradient(rect.topLeft(), rect.topRight());
		gradient.setColorAt(0., over ? style.from.lighter(110) : style.from);
		gradient.setColorAt(1., over ? style.to.lighter(110) : style.to);
		p.setBrush(gradient);
	} else {
		p.setBrush(Alpha(st::windowFg->c, over ? 0.12 : 0.07));
	}
	p.drawRoundedRect(QRectF(rect), radius, radius);
	p.setFont(st::semiboldFont->f);
	p.setPen(accent ? QColor(255, 255, 255) : st::windowFg->c);
	p.drawText(
		rect,
		Qt::AlignCenter,
		st::semiboldFont->elided(text, rect.width() - Px(12)));
}

void DownloadsList::paintSummary(QPainter &p, QRect rect, crl::time now) {
	auto hq = PainterHighQualityEnabler(p);
	const auto style = CurrentLogoStyle();

	// A card with a faint wash of the logo colors.
	auto wash = QLinearGradient(rect.topLeft(), rect.bottomRight());
	wash.setColorAt(0., Alpha(style.from, 0.20));
	wash.setColorAt(1., Alpha(style.to, 0.12));
	p.setPen(QPen(Alpha(st::windowFg->c, 0.07), 1.));
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(QRectF(rect).marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }), Px(18), Px(18));
	p.setPen(Qt::NoPen);
	p.setBrush(wash);
	p.drawRoundedRect(QRectF(rect), Px(18), Px(18));

	auto ready = int64(0);
	auto total = int64(0);
	auto speed = 0.;
	auto loading = 0;
	for (const auto &entry : _active) {
		if (entry.done) {
			continue;
		}
		++loading;
		ready += entry.ready;
		total += std::max(entry.total, entry.ready);
		speed += entry.speed;
	}
	const auto padding = Px(18);
	const auto inner = rect.marginsRemoved(
		QMargins(padding, Px(14), padding, Px(16)));

	p.setFont(st::semiboldFont->f);
	p.setPen(st::windowSubTextFg);
	const auto title = loading
		? (tr::lng_tgd_active(tr::now) + u" · "_q + QString::number(loading))
		: tr::lng_tgd_idle(tr::now);
	p.drawText(
		QRect(inner.x(), inner.y(), inner.width(), st::semiboldFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		title);

	const auto big = BigFont();
	const auto percent = (total > 0)
		? int(std::floor(100. * ready / double(total)))
		: (loading ? 0 : 100);
	const auto bigTop = inner.y() + st::semiboldFont->height + Px(4);
	p.setFont(big->f);
	p.setPen(st::windowBoldFg);
	const auto bigText = loading
		? (QString::number(percent) + '%')
		: tr::lng_tgd_all_done(tr::now);
	p.drawText(
		QRect(inner.x(), bigTop, inner.width(), big->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		bigText);

	if (loading) {
		const auto small = SmallFont();
		p.setFont(small->f);
		p.setPen(st::windowSubTextFg);
		const auto info = JoinParts({
			Ui::FormatDownloadText(ready, total),
			FormatSpeed(speed),
			FormatLeft(total - ready, speed),
		});
		p.drawText(
			QRect(inner.x(), bigTop, inner.width(), big->height),
			Qt::AlignRight | Qt::AlignVCenter,
			small->elided(info, inner.width() - big->width(bigText) - Px(12)));
	}
	const auto barHeight = Px(8);
	PaintGradientBar(
		p,
		QRectF(
			inner.x(),
			inner.y() + inner.height() - barHeight,
			inner.width(),
			barHeight),
		loading ? (total > 0 ? (ready / double(total)) : 0.) : 1.,
		now);
}

void DownloadsList::paintSectionLabel(
		QPainter &p,
		int top,
		const QString &text) {
	const auto small = SmallFont();
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(Px(20), top, width() - Px(40), Px(30)),
		Qt::AlignLeft | Qt::AlignVCenter,
		text.toUpper());
}

void DownloadsList::paintActive(
		QPainter &p,
		QRect rect,
		int index,
		crl::time now) {
	const auto &entry = _active[index];
	const auto rowOver = isOver(Hit::ActiveRow, index)
		|| isOver(Hit::Cancel, index)
		|| isOver(Hit::Open, index)
		|| isOver(Hit::Folder, index);
	if (rowOver) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.045));
		p.drawRoundedRect(
			QRectF(rect).marginsRemoved({ 0, 2, 0, 2 }),
			Px(14),
			Px(14));
	}
	const auto side = Px(16);
	const auto tileSize = Px(44);
	const auto tile = QRect(
		side,
		rect.y() + (rect.height() - tileSize) / 2,
		tileSize,
		tileSize);
	PaintTile(p, tile, Extension(entry.name), entry.photo != nullptr);

	const auto button = Px(32);
	const auto right = width() - side - (entry.done
		? (button + Px(8) + Px(84))
		: button) - Px(12);
	const auto left = tile.x() + tileSize + Px(12);
	const auto textWidth = std::max(right - left, Px(40));

	p.setFont(st::semiboldFont->f);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(left, rect.y() + Px(12), textWidth, st::semiboldFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::semiboldFont->elided(entry.name, textWidth, Qt::ElideMiddle));

	const auto small = SmallFont();
	p.setFont(small->f);
	const auto metaTop = rect.y() + Px(12) + st::semiboldFont->height + Px(3);
	if (entry.done) {
		p.setPen(QColor(0x34, 0xC7, 0x59));
		p.drawText(
			QRect(left, metaTop, textWidth, small->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			small->elided(
				JoinParts({
					u"✓ "_q + tr::lng_tgd_done(tr::now),
					Ui::FormatSizeText(std::max(entry.total, entry.ready)),
				}),
				textWidth));
	} else {
		p.setPen(st::windowSubTextFg);
		const auto meta = (entry.ready <= 0 && entry.speed < 1.)
			? tr::lng_tgd_waiting(tr::now)
			: JoinParts({
				Ui::FormatDownloadText(entry.ready, entry.total),
				FormatSpeed(entry.speed),
				FormatLeft(entry.total - entry.ready, entry.speed),
			});
		p.drawText(
			QRect(left, metaTop, textWidth, small->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			small->elided(meta, textWidth));
		const auto barTop = metaTop + small->height + Px(7);
		PaintGradientBar(
			p,
			QRectF(left, barTop, textWidth, Px(5)),
			(entry.total > 0) ? (entry.ready / double(entry.total)) : 0.,
			now);
	}

	const auto by = rect.y() + (rect.height() - button) / 2;
	if (entry.done) {
		const auto folder = QRect(width() - side - button, by, button, button);
		PaintRoundIconButton(
			p,
			folder,
			st::menuIconShowInFolder,
			isOver(Hit::Folder, index));
		const auto open = QRect(
			folder.x() - Px(8) - Px(84),
			by,
			Px(84),
			button);
		paintPill(
			p,
			open,
			tr::lng_tgd_open(tr::now),
			true,
			isOver(Hit::Open, index));
	} else {
		PaintRoundIconButton(
			p,
			QRect(width() - side - button, by, button, button),
			st::menuIconCancel,
			isOver(Hit::Cancel, index));
	}
}

void DownloadsList::paintRecent(QPainter &p, QRect rect, int index) {
	const auto &entry = _recent[index];
	if (isOver(Hit::RecentRow, index) || isOver(Hit::RecentFolder, index)) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.045));
		p.drawRoundedRect(
			QRectF(rect).marginsRemoved({ 0, 2, 0, 2 }),
			Px(12),
			Px(12));
	}
	const auto side = Px(16);
	const auto tileSize = Px(36);
	const auto tile = QRect(
		side,
		rect.y() + (rect.height() - tileSize) / 2,
		tileSize,
		tileSize);
	PaintTile(p, tile, Extension(entry.name), false);

	const auto button = Px(30);
	const auto left = tile.x() + tileSize + Px(12);
	const auto textWidth = std::max(
		width() - side - button - Px(12) - left,
		Px(40));
	const auto exists = !entry.path.isEmpty() && QFileInfo::exists(entry.path);

	p.setFont(st::normalFont->f);
	p.setPen(exists ? st::windowFg : st::windowSubTextFg);
	p.drawText(
		QRect(left, rect.y() + Px(9), textWidth, st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(entry.name, textWidth, Qt::ElideMiddle));

	const auto small = SmallFont();
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	const auto date = base::unixtime::parse(
		Data::DateFromDownloadDate(entry.started));
	const auto meta = JoinParts({
		entry.size ? Ui::FormatSizeText(entry.size) : QString(),
		QLocale().toString(date, u"d MMM, HH:mm"_q),
		exists ? QString() : tr::lng_tgd_missing(tr::now),
	});
	p.drawText(
		QRect(
			left,
			rect.y() + Px(9) + st::normalFont->height + Px(2),
			textWidth,
			small->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		small->elided(meta, textWidth));

	PaintRoundIconButton(
		p,
		QRect(
			width() - side - button,
			rect.y() + (rect.height() - button) / 2,
			button,
			button),
		st::menuIconShowInFolder,
		isOver(Hit::RecentFolder, index));
}

void DownloadsList::paintEmpty(QPainter &p, QRect rect) {
	auto hq = PainterHighQualityEnabler(p);
	const auto style = CurrentLogoStyle();
	const auto size = Px(40);
	const auto circle = QRect(
		Px(16),
		rect.y() + (rect.height() - size) / 2,
		size,
		size);
	auto gradient = QLinearGradient(circle.topLeft(), circle.bottomRight());
	gradient.setColorAt(0., Alpha(style.from, 0.25));
	gradient.setColorAt(1., Alpha(style.to, 0.25));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(QRectF(circle));
	st::menuIconDownload.paintInCenter(p, circle);

	const auto left = circle.x() + size + Px(12);
	const auto textWidth = width() - left - Px(16);
	p.setFont(st::semiboldFont->f);
	p.setPen(st::windowBoldFg);
	p.drawText(
		QRect(left, rect.y() + Px(14), textWidth, st::semiboldFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgd_empty(tr::now));
	const auto small = SmallFont();
	p.setFont(small->f);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(
			left,
			rect.y() + Px(14) + st::semiboldFont->height + Px(2),
			textWidth,
			small->height * 2),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
		tr::lng_tgd_empty_note(tr::now));
}

void DownloadsList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto now = crl::now();
	const auto side = Px(16);
	paintSummary(
		p,
		QRect(side, Px(8), width() - 2 * side, summaryHeight()),
		now);

	auto top = activeTop();
	paintSectionLabel(p, top, tr::lng_tgd_now(tr::now));
	top += Px(30);
	if (_active.empty()) {
		paintEmpty(p, QRect(0, top, width(), Px(72)));
	} else {
		for (auto i = 0; i != int(_active.size()); ++i) {
			paintActive(
				p,
				QRect(side - Px(6), top, width() - 2 * side + Px(12), Px(72)),
				i,
				now);
			top += Px(72);
		}
	}

	if (!_recent.empty()) {
		top = recentTop();
		paintSectionLabel(p, top, tr::lng_tgd_recent(tr::now));
		top += Px(30);
		for (auto i = 0; i != int(_recent.size()); ++i) {
			paintRecent(
				p,
				QRect(side - Px(6), top, width() - 2 * side + Px(12), Px(56)),
				i);
			top += Px(56);
		}
	}

	const auto footer = footerTop();
	const auto gap = Px(10);
	const auto half = (width() - 2 * side - gap) / 2;
	paintPill(
		p,
		QRect(side, footer, half, Px(40)),
		tr::lng_tgd_all(tr::now),
		false,
		isOver(Hit::All, -1));
	paintPill(
		p,
		QRect(side + half + gap, footer, half, Px(40)),
		tr::lng_tgd_clear(tr::now),
		false,
		isOver(Hit::Clear, -1));
}

class DownloadsWindow final : public Ui::RpWidget {
public:
	DownloadsWindow(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close,
		int height);

protected:
	int resizeGetHeight(int newWidth) override {
		return _height;
	}

private:
	void paintHeader(QPainter &p);

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	const int _height = 0;
	const not_null<Ui::RpWidget*> _header;
	const not_null<Ui::IconButton*> _closeButton;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<DownloadsList*> _list;

};

DownloadsWindow::DownloadsWindow(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close,
	int height)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _height(height)
, _header(Ui::CreateChild<Ui::RpWidget>(this))
, _closeButton(Ui::CreateChild<Ui::IconButton>(
	_header.get(),
	st::boxTitleClose))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _list(_scroll->setOwnedWidget(object_ptr<DownloadsList>(
	_scroll.get(),
	controller,
	_close))) {
	resize(width(), height);
	_closeButton->setClickedCallback([=] { _close(); });

	_header->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_header);
		paintHeader(p);
	}, _header->lifetime());

	// Logo colors may animate, the bars shine, so repaint now and then.
	sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto headerHeight = Px(64);
		_header->setGeometry(0, 0, size.width(), headerHeight);
		_closeButton->moveToRight(
			Px(10),
			(headerHeight - _closeButton->height()) / 2);
		_scroll->setGeometry(
			0,
			headerHeight,
			size.width(),
			size.height() - headerHeight);
		_list->resizeToWidth(size.width());
	}, lifetime());
}

void DownloadsWindow::paintHeader(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto style = CurrentLogoStyle();
	const auto size = Px(36);
	const auto tile = QRect(Px(18), (_header->height() - size) / 2, size, size);
	auto gradient = QLinearGradient(tile.topLeft(), tile.bottomRight());
	gradient.setColorAt(0., style.from);
	gradient.setColorAt(1., style.to);
	if (style.glow) {
		auto glow = style.from;
		glow.setAlphaF(0.25 * style.glowPower);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawRoundedRect(QRectF(tile).marginsAdded({ 3, 3, 3, 3 }), Px(13), Px(13));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(QRectF(tile), Px(11), Px(11));

	// A white arrow down into a tray, drawn so it stays crisp on any color.
	auto pen = QPen(QColor(255, 255, 255), Px(2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const auto cx = tile.x() + tile.width() / 2.;
	const auto top = tile.y() + tile.height() * 0.24;
	const auto bottom = tile.y() + tile.height() * 0.60;
	p.drawLine(QPointF(cx, top), QPointF(cx, bottom));
	p.drawLine(
		QPointF(cx - tile.width() * 0.16, bottom - tile.height() * 0.16),
		QPointF(cx, bottom));
	p.drawLine(
		QPointF(cx + tile.width() * 0.16, bottom - tile.height() * 0.16),
		QPointF(cx, bottom));
	const auto tray = tile.y() + tile.height() * 0.76;
	p.drawLine(
		QPointF(tile.x() + tile.width() * 0.26, tray),
		QPointF(tile.x() + tile.width() * 0.74, tray));

	const auto left = tile.x() + size + Px(12);
	const auto &font = st::boxTitle.style.font;
	p.setFont(font->f);
	p.setPen(st::boxTitleFg);
	p.drawText(
		QRect(left, 0, _header->width() - left - Px(60), _header->height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgd_title(tr::now));
}

} // namespace

void ShowDownloadsWindow(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto window = controller->widget();
		const auto width = std::min(Px(560), window->width() - Px(40));
		const auto height = std::min(Px(620), window->height() - Px(60));
		box->setNoContentMargin(true);
		box->setWidth(width);
		box->addRow(
			object_ptr<DownloadsWindow>(
				box,
				controller,
				[=] { box->closeBox(); },
				height),
			QMargins());
	}));
}

} // namespace Tristegram
