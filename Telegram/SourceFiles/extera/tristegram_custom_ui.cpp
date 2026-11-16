/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_custom_ui.h"

#include "base/timer.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "window/themes/window_theme.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"

#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>

namespace Tristegram {
namespace {

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] QColor ConfigColor(const QString &key, QColor fallback) {
	const auto color = QColor(Extera::String(key));
	return color.isValid() ? color : fallback;
}

class LivePreview final : public Ui::RpWidget {
public:
	explicit LivePreview(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;

private:
	void refreshAmbient();
	void paintBackground(QPainter &p, QRectF card) const;
	void paintBubble(
		QPainter &p,
		QRectF rect,
		const Ui::Text::String &text,
		bool out) const;

	Ui::Animations::Simple _spin;
	base::Timer _ambient;
	Ui::Text::String _in;
	Ui::Text::String _out;

};

LivePreview::LivePreview(QWidget *parent)
: RpWidget(parent)
, _in(st::defaultTextStyle, tr::lng_tristegram_preview_in(tr::now))
, _out(st::defaultTextStyle, tr::lng_tristegram_preview_out(tr::now)) {
	resize(width(), Px(220));
	// The real chat background, so a change can be seen right away.
	Window::Theme::Background()->updates(
	) | rpl::on_next([=](const Window::Theme::BackgroundUpdate &) {
		update();
	}, lifetime());
	setCursor(style::cur_pointer);
	_ambient.setCallback([=] { update(); });
	refreshAmbient();
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_"_q);
	}) | rpl::on_next([=] {
		refreshAmbient();
		update();
	}, lifetime());
}

void LivePreview::refreshAmbient() {
	if (LogoAmbientAnimated()) {
		if (!_ambient.isActive()) {
			_ambient.callEach(40);
		}
	} else {
		_ambient.cancel();
	}
}

void LivePreview::mousePressEvent(QMouseEvent *e) {
	if (!_spin.animating() && !Extera::Bool(u"tristegram_eco"_q)) {
		_spin.start([=] { update(); }, 0., 1., 900);
	}
}

void LivePreview::paintBackground(QPainter &p, QRectF card) const {
	const auto background = Window::Theme::Background();
	if (const auto color = background->colorForFill()) {
		p.fillRect(card, *color);
		return;
	}
	auto image = background->gradientForFill();
	if (image.isNull()) {
		image = background->prepared();
	}
	if (image.isNull()) {
		p.fillRect(card, st::windowBgOver);
		return;
	}
	// Fill the card keeping the proportions, like the chat does.
	const auto ratio = card.width() / std::max(card.height(), 1.);
	auto sw = double(image.width());
	auto sh = double(image.height());
	if (sw / std::max(sh, 1.) > ratio) {
		sw = sh * ratio;
	} else {
		sh = sw / ratio;
	}
	p.drawImage(
		card,
		image,
		QRectF((image.width() - sw) / 2., (image.height() - sh) / 2., sw, sh));
}

void LivePreview::paintBubble(
		QPainter &p,
		QRectF rect,
		const Ui::Text::String &text,
		bool out) const {
	const auto radius = std::min(
		rect.height() / 2.,
		double(Px(Extera::Int(u"tristegram_bubble_radius"_q))));
	p.setPen(Qt::NoPen);
	if (out) {
		auto from = st::msgOutBg->c;
		auto to = from;
		if (Extera::Bool(u"tristegram_bubble_gradient"_q)) {
			from = ConfigColor(u"tristegram_bubble_c1"_q, from);
			to = ConfigColor(u"tristegram_bubble_c2"_q, from);
		}
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
		fill.setColorAt(0., from);
		fill.setColorAt(1., to);
		p.setBrush(fill);
	} else {
		p.setBrush(st::msgInBg);
	}
	p.drawRoundedRect(rect, radius, radius);
	p.setPen(out ? st::historyTextOutFg : st::historyTextInFg);
	const auto available = int(rect.width()) - 2 * Px(12);
	text.draw(p, {
		.position = QPoint(
			int(rect.x()) + Px(12),
			int(rect.y() + (rect.height() - st::defaultTextStyle.font->height) / 2.)),
		.availableWidth = available,
		.elisionLines = 1,
	});
}

void LivePreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto now = crl::now();
	const auto style = CurrentLogoStyle(now);
	const auto card = QRectF(rect()).marginsRemoved(
		QMarginsF(Px(12), Px(4), Px(12), Px(4)));
	const auto radius = double(Px(22));

	// Background: soft glows in the logo colors over the window color.
	auto clip = QPainterPath();
	clip.addRoundedRect(card, radius, radius);
	p.save();
	p.setClipPath(clip);
	paintBackground(p, card);
	auto glow1 = QRadialGradient(
		QPointF(card.x() + card.width() * 0.2, card.y() + card.height() * 0.45),
		card.width() * 0.3);
	glow1.setColorAt(0., Alpha(style.from, 0.22));
	glow1.setColorAt(1., Alpha(style.from, 0.));
	p.fillRect(card, glow1);
	p.restore();
	p.setPen(QPen(Alpha(st::windowFg->c, 0.08), 1.));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(card, radius, radius);

	// Logo and gradient title on the left.
	const auto logoSize = double(Px(84));
	const auto logo = QRectF(
		card.x() + Px(28),
		card.y() + (card.height() - logoSize) / 2. - Px(12),
		logoSize,
		logoSize);
	PaintLogo(p, logo, style, _spin.value(1.));
	auto titleFill = QLinearGradient(
		QPointF(logo.x(), 0),
		QPointF(logo.x() + logoSize * 1.6, 0));
	titleFill.setColorAt(0., style.from);
	titleFill.setColorAt(1., style.to);
	p.setFont(st::boxTitleFont);
	p.setPen(QPen(QBrush(titleFill), 1));
	p.drawText(
		QRectF(
			logo.x() - Px(30),
			logo.y() + logoSize + Px(10),
			logoSize + Px(60),
			st::boxTitleFont->height),
		Qt::AlignHCenter | Qt::AlignTop,
		u"Tristegram"_q);

	// Two message bubbles on the right.
	const auto left = logo.x() + logoSize + Px(56);
	const auto right = card.x() + card.width() - Px(20);
	if (right - left > Px(120)) {
		const auto h = double(Px(36));
		const auto inWidth = std::min(
			right - left,
			double(_in.maxWidth() + 2 * Px(12)));
		const auto in = QRectF(
			left,
			card.y() + card.height() / 2. - h - Px(8),
			inWidth,
			h);
		paintBubble(p, in, _in, false);
		const auto outWidth = std::min(
			right - left,
			double(_out.maxWidth() + 2 * Px(12)));
		const auto out = QRectF(
			right - outWidth,
			card.y() + card.height() / 2. + Px(8),
			outWidth,
			h);
		paintBubble(p, out, _out, true);
	}
}

class TileGrid final : public Ui::RpWidget {
public:
	TileGrid(QWidget *parent, TileGridDescriptor &&descriptor);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] QRect tileRect(int index) const;
	[[nodiscard]] int indexAt(QPoint point) const;
	void setOver(int index);

	TileGridDescriptor _descriptor;
	int _columns = 1;
	int _tileWidth = 0;
	int _over = -1;
	Ui::Animations::Simple _overAnimation;
	int _overWas = -1;

};

TileGrid::TileGrid(QWidget *parent, TileGridDescriptor &&descriptor)
: RpWidget(parent)
, _descriptor(std::move(descriptor)) {
	setMouseTracking(true);
	if (!_descriptor.configKey.isEmpty()) {
		const auto key = _descriptor.configKey;
		Extera::Config::Instance().changes(
		) | rpl::filter([=](const QString &changed) {
			return (changed == key) || changed.startsWith(u"tristegram_"_q);
		}) | rpl::on_next([=] {
			update();
		}, lifetime());
	}
}

int TileGrid::resizeGetHeight(int newWidth) {
	const auto padding = Px(12);
	const auto gap = Px(8);
	const auto minTile = Px(84);
	const auto inner = std::max(newWidth - padding * 2, minTile);
	_columns = std::max(1, (inner + gap) / (minTile + gap));
	_tileWidth = (inner - gap * (_columns - 1)) / _columns;
	const auto rows = (_descriptor.count + _columns - 1) / _columns;
	const auto tileHeight = Px(92);
	return rows * tileHeight + std::max(rows - 1, 0) * gap + Px(8);
}

QRect TileGrid::tileRect(int index) const {
	const auto padding = Px(12);
	const auto gap = Px(8);
	const auto row = index / _columns;
	const auto col = index % _columns;
	return QRect(
		padding + col * (_tileWidth + gap),
		Px(4) + row * (Px(92) + gap),
		_tileWidth,
		Px(92));
}

int TileGrid::indexAt(QPoint point) const {
	for (auto i = 0; i != _descriptor.count; ++i) {
		if (tileRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void TileGrid::setOver(int index) {
	if (_over == index) {
		return;
	}
	_overWas = _over;
	_over = index;
	setCursor((index >= 0) ? style::cur_pointer : style::cur_default);
	_overAnimation.stop();
	_overAnimation.start([=] { update(); }, 0., 1., 180);
}

void TileGrid::mouseMoveEvent(QMouseEvent *e) {
	setOver(indexAt(e->pos()));
}

void TileGrid::leaveEventHook(QEvent *e) {
	setOver(-1);
}

void TileGrid::mousePressEvent(QMouseEvent *e) {
	const auto index = indexAt(e->pos());
	if (index >= 0 && _descriptor.chosen) {
		_descriptor.chosen(index);
		update();
	}
}

void TileGrid::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto selected = _descriptor.selected ? _descriptor.selected() : -1;
	const auto progress = _overAnimation.value(1.);
	const auto radius = double(Px(16));
	for (auto i = 0; i != _descriptor.count; ++i) {
		const auto lift = (i == _over)
			? progress
			: (i == _overWas)
			? (1. - progress)
			: 0.;
		auto tile = QRectF(tileRect(i)).translated(0, -Px(2) * lift);
		p.setPen(Qt::NoPen);
		p.setBrush((i == selected)
			? Alpha(st::windowBgActive->c, 0.14)
			: Alpha(st::windowBgOver->c, 1.));
		p.drawRoundedRect(tile, radius, radius);
		if (lift > 0.) {
			p.setBrush(Alpha(st::windowFg->c, 0.04 * lift));
			p.drawRoundedRect(tile, radius, radius);
		}
		if (i == selected) {
			p.setPen(QPen(st::windowBgActive->c, Px(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(
				tile.marginsRemoved(QMarginsF(1, 1, 1, 1)),
				radius - 1,
				radius - 1);
		}
		const auto swatchSize = Px(42) + Px(4) * lift;
		const auto swatch = QRect(
			int(tile.x() + (tile.width() - swatchSize) / 2.),
			int(tile.y() + Px(12) - Px(2) * lift),
			int(swatchSize),
			int(swatchSize));
		if (_descriptor.paintSwatch) {
			_descriptor.paintSwatch(p, swatch, i);
		}
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		const auto name = _descriptor.name ? _descriptor.name(i) : QString();
		const auto textRect = QRect(
			int(tile.x()) + Px(4),
			int(tile.y() + tile.height()) - Px(28),
			int(tile.width()) - Px(8),
			Px(20));
		p.drawText(
			textRect,
			Qt::AlignHCenter | Qt::AlignVCenter,
			st::normalFont->elided(name, textRect.width()));
	}
}


class SegmentedTabs final : public Ui::RpWidget {
public:
	SegmentedTabs(
		QWidget *parent,
		std::vector<QString> labels,
		int active,
		Fn<void(int)> changed);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	[[nodiscard]] QRectF segmentRect(double index) const;
	[[nodiscard]] int indexAt(QPoint point) const;

	std::vector<QString> _labels;
	Fn<void(int)> _changed;
	int _active = 0;
	int _over = -1;
	double _from = 0.;
	Ui::Animations::Simple _slide;

};

SegmentedTabs::SegmentedTabs(
	QWidget *parent,
	std::vector<QString> labels,
	int active,
	Fn<void(int)> changed)
: RpWidget(parent)
, _labels(std::move(labels))
, _changed(std::move(changed))
, _active(std::clamp(active, 0, std::max(int(_labels.size()) - 1, 0)))
, _from(_active) {
	setMouseTracking(true);
	Extera::Config::Instance().changes(
	) | rpl::filter([=](const QString &changed) {
		return changed.startsWith(u"tristegram_logo"_q);
	}) | rpl::on_next([=] {
		update();
	}, lifetime());
}

int SegmentedTabs::resizeGetHeight(int newWidth) {
	return Px(44);
}

QRectF SegmentedTabs::segmentRect(double index) const {
	const auto outer = QRectF(rect()).marginsRemoved(
		QMarginsF(Px(12), Px(4), Px(12), Px(4)));
	const auto count = std::max(int(_labels.size()), 1);
	const auto inner = outer.marginsRemoved(QMarginsF(3, 3, 3, 3));
	const auto width = inner.width() / count;
	return QRectF(inner.x() + width * index, inner.y(), width, inner.height());
}

int SegmentedTabs::indexAt(QPoint point) const {
	for (auto i = 0; i != int(_labels.size()); ++i) {
		if (segmentRect(i).contains(point)) {
			return i;
		}
	}
	return -1;
}

void SegmentedTabs::mouseMoveEvent(QMouseEvent *e) {
	const auto index = indexAt(e->pos());
	if (_over != index) {
		_over = index;
		setCursor((index >= 0 && index != _active)
			? style::cur_pointer
			: style::cur_default);
		update();
	}
}

void SegmentedTabs::leaveEventHook(QEvent *e) {
	_over = -1;
	update();
}

void SegmentedTabs::mousePressEvent(QMouseEvent *e) {
	const auto index = indexAt(e->pos());
	if (index < 0 || index == _active) {
		return;
	}
	_from = _slide.value(_active);
	_slide.stop();
	_slide.start([=] { update(); }, _from, double(index), 320, anim::easeOutBack);
	_active = index;
	setCursor(style::cur_default);
	update();
	if (_changed) {
		_changed(index);
	}
}

void SegmentedTabs::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto outer = QRectF(rect()).marginsRemoved(
		QMarginsF(Px(12), Px(4), Px(12), Px(4)));
	const auto radius = outer.height() / 2.;

	// Glass capsule.
	p.setPen(QPen(Alpha(st::windowFg->c, 0.08), 1.));
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(outer, radius, radius);

	// Sliding gradient thumb in the logo colors with a soft glow.
	const auto style = CurrentLogoStyle();
	const auto position = _slide.value(_active);
	const auto thumb = segmentRect(position);
	const auto thumbRadius = thumb.height() / 2.;
	for (auto i = 3; i != 0; --i) {
		auto glow = style.from;
		glow.setAlphaF(0.07 * style.glowPower);
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		const auto spread = i * 2.;
		p.drawRoundedRect(
			thumb.marginsAdded(QMarginsF(spread, spread, spread, spread)),
			thumbRadius + spread,
			thumbRadius + spread);
	}
	auto fill = QLinearGradient(thumb.topLeft(), thumb.bottomRight());
	fill.setColorAt(0., style.from);
	fill.setColorAt(1., style.to);
	p.setBrush(fill);
	p.drawRoundedRect(thumb, thumbRadius, thumbRadius);
	auto sheen = QLinearGradient(thumb.topLeft(), thumb.bottomLeft());
	sheen.setColorAt(0., QColor(255, 255, 255, 60));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
	p.setBrush(sheen);
	p.drawRoundedRect(thumb, thumbRadius, thumbRadius);

	// Labels: white over the thumb, muted elsewhere.
	p.setFont(st::semiboldFont);
	for (auto i = 0; i != int(_labels.size()); ++i) {
		const auto segment = segmentRect(i);
		const auto overlap = std::clamp(
			1. - std::abs(position - i),
			0.,
			1.);
		auto color = anim::color(
			(i == _over) ? st::windowFg : st::windowSubTextFg,
			QColor(255, 255, 255),
			overlap);
		p.setPen(color);
		const auto text = segment.toRect().marginsRemoved(
			QMargins(Px(4), 0, Px(4), 0));
		p.drawText(
			text,
			Qt::AlignCenter,
			st::semiboldFont->elided(_labels[i], text.width()));
	}
}

} // namespace

object_ptr<Ui::RpWidget> CreateLivePreview(QWidget *parent) {
	return object_ptr<LivePreview>(parent);
}

object_ptr<Ui::RpWidget> CreateTileGrid(
		QWidget *parent,
		TileGridDescriptor &&descriptor) {
	return object_ptr<TileGrid>(parent, std::move(descriptor));
}

object_ptr<Ui::RpWidget> CreateSegmentedTabs(
		QWidget *parent,
		std::vector<QString> labels,
		int active,
		Fn<void(int)> changed) {
	return object_ptr<SegmentedTabs>(
		parent,
		std::move(labels),
		active,
		std::move(changed));
}

} // namespace Tristegram
