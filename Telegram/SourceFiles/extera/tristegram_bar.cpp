/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_bar.h"

#include "apiwrap.h"
#include "core/application.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_changelog.h"
#include "extera/extera_config.h"
#include "extera/extera_plugins.h"
#include "extera/extera_streamer.h"
#include "base/call_delayed.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QDateTime>
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtp_instance.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_extera.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QTime>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Tristegram {
namespace {

constexpr auto kPingEachTicks = 4;
constexpr auto kHistoryLimit = 48;
constexpr auto kGraphPoints = 16;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor WithAlpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] QColor QualityColor(int ping) {
	if (ping < 0) {
		return st::windowSubTextFg->c;
	} else if (ping <= 120) {
		return st::windowBgActive->c;
	} else if (ping <= 250) {
		return QColor(0xF5, 0xB7, 0x00);
	}
	return st::attentionButtonFg->c;
}

[[nodiscard]] QString QualityText(int ping) {
	if (ping < 0) {
		return tr::lng_tristegram_conn_none(tr::now);
	} else if (ping <= 120) {
		return tr::lng_tristegram_conn_good(tr::now);
	} else if (ping <= 250) {
		return tr::lng_tristegram_conn_ok(tr::now);
	}
	return tr::lng_tristegram_conn_bad(tr::now);
}

[[nodiscard]] QString Ms() {
	return tr::lng_tristegram_ms(tr::now);
}

[[nodiscard]] QString DcCity(int dcId) {
	switch (dcId) {
	case 1:
	case 3: return tr::lng_tristegram_dc_miami(tr::now);
	case 2:
	case 4: return tr::lng_tristegram_dc_amsterdam(tr::now);
	case 5: return tr::lng_tristegram_dc_singapore(tr::now);
	}
	return QString();
}

// Line with a soft area fill through `values`, scaled into `rect`.
void PaintGraph(
		QPainter &p,
		const QRectF &rect,
		const std::vector<int> &values,
		QColor color,
		double reveal,
		double lineWidth) {
	if (values.size() < 2) {
		return;
	}
	const auto minmax = std::minmax_element(values.begin(), values.end());
	const auto mn = *minmax.first - 4.;
	const auto mx = *minmax.second + 4.;
	const auto step = rect.width() / double(values.size() - 1);
	auto line = QPainterPath();
	for (auto i = 0; i != int(values.size()); ++i) {
		const auto x = rect.x() + i * step;
		const auto y = rect.y() + rect.height()
			- (values[i] - mn) / (mx - mn) * rect.height();
		if (!i) {
			line.moveTo(x, y);
		} else {
			line.lineTo(x, y);
		}
	}
	const auto end = line.currentPosition();
	p.save();
	if (reveal < 1.) {
		p.setClipRect(QRectF(
			rect.x() - lineWidth,
			rect.y() - lineWidth * 2,
			(rect.width() + lineWidth * 2) * reveal,
			rect.height() + lineWidth * 4));
	}
	auto area = line;
	area.lineTo(rect.x() + rect.width(), rect.y() + rect.height());
	area.lineTo(rect.x(), rect.y() + rect.height());
	area.closeSubpath();
	auto fill = QLinearGradient(rect.topLeft(), rect.bottomLeft());
	fill.setColorAt(0., WithAlpha(color, 0.38));
	fill.setColorAt(1., WithAlpha(color, 0.));
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawPath(area);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		color,
		lineWidth,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.drawPath(line);
	p.restore();

	if (reveal >= 1.) {
		p.setPen(Qt::NoPen);
		p.setBrush(WithAlpha(color, 0.3));
		p.drawEllipse(end, lineWidth * 2.6, lineWidth * 2.6);
		p.setBrush(color);
		p.drawEllipse(end, lineWidth * 1.3, lineWidth * 1.3);
	}
}

} // namespace

void PingHistory::add(int ms) {
	values.push_back(ms);
	while (int(values.size()) > kHistoryLimit) {
		values.pop_front();
	}
	last = ms;
	++sent;
}

void PingHistory::fail() {
	last = -1;
	++sent;
	++failed;
}

int PingHistory::average() const {
	if (values.empty()) {
		return -1;
	}
	auto sum = 0;
	for (const auto v : values) {
		sum += v;
	}
	return sum / int(values.size());
}

int PingHistory::jitter() const {
	if (values.size() < 2) {
		return 0;
	}
	auto sum = 0;
	for (auto i = 1; i != int(values.size()); ++i) {
		sum += std::abs(values[i] - values[i - 1]);
	}
	return sum / int(values.size() - 1);
}

int PingHistory::minimum() const {
	return values.empty()
		? -1
		: *std::min_element(values.begin(), values.end());
}

int PingHistory::maximum() const {
	return values.empty()
		? -1
		: *std::max_element(values.begin(), values.end());
}

int PingHistory::lossPercent() const {
	return sent ? (failed * 100 / sent) : 0;
}

// Floating card with the connection details, shown on hover over the ping.
class ConnectionPanel final : public Ui::RpWidget {
public:
	ConnectionPanel(
		QWidget *parent,
		not_null<const PingHistory*> history,
		Fn<int()> dcId);

	void showAnimated();
	void hideAnimated(Fn<void()> done);

	[[nodiscard]] static QSize Size();
	[[nodiscard]] static int Shadow();

	Fn<void()> entered;
	Fn<void()> left;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void paintStat(
		QPainter &p,
		QRect rect,
		const QString &label,
		const QString &value) const;

	const not_null<const PingHistory*> _history;
	const Fn<int()> _dcId;
	Ui::Animations::Simple _shown;
	Ui::Animations::Simple _reveal;
	bool _hiding = false;

};

ConnectionPanel::ConnectionPanel(
	QWidget *parent,
	not_null<const PingHistory*> history,
	Fn<int()> dcId)
: RpWidget(parent)
, _history(history)
, _dcId(std::move(dcId)) {
	resize(Size());
	setMouseTracking(true);
}

QSize ConnectionPanel::Size() {
	return QSize(Px(300) + Shadow() * 2, Px(286) + Shadow() * 2);
}

int ConnectionPanel::Shadow() {
	return Px(14);
}

void ConnectionPanel::showAnimated() {
	_hiding = false;
	show();
	raise();
	_shown.start([=] { update(); }, 0., 1., 220, anim::easeOutCubic);
	_reveal.start([=] { update(); }, 0., 1., 700, anim::easeOutCubic);
}

void ConnectionPanel::hideAnimated(Fn<void()> done) {
	_hiding = true;
	_shown.start([=] {
		update();
		if (!_shown.animating() && _hiding && done) {
			done();
		}
	}, 1., 0., 160, anim::easeOutCubic);
}

void ConnectionPanel::enterEventHook(QEnterEvent *e) {
	if (entered) {
		entered();
	}
}

void ConnectionPanel::leaveEventHook(QEvent *e) {
	if (left) {
		left();
	}
}

void ConnectionPanel::paintStat(
		QPainter &p,
		QRect rect,
		const QString &label,
		const QString &value) const {
	const auto radius = Px(12);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);
	const auto inner = rect.marginsRemoved(
		QMargins(Px(10), Px(7), Px(8), Px(7)));
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(inner, Qt::AlignLeft | Qt::AlignTop, label);
	p.setFont(st::boxTitleFont);
	p.setPen(st::windowFg);
	p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, value);
}

void ConnectionPanel::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	const auto shown = _shown.value(_hiding ? 0. : 1.);
	if (shown <= 0.) {
		return;
	}
	const auto shadow = Shadow();
	const auto card = rect().marginsRemoved(
		QMargins(shadow, shadow, shadow, shadow));
	p.setOpacity(shown);
	const auto scale = 0.94 + 0.06 * shown;
	p.translate(card.center().x(), card.y());
	p.scale(scale, scale);
	p.translate(-card.center().x(), -card.y() - Px(6) * (1. - shown));

	const auto radius = Px(18);
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != 6; ++i) {
		const auto spread = Px(2) * (i + 1);
		p.setBrush(WithAlpha(st::windowShadowFg->c, 0.035));
		p.drawRoundedRect(
			card.marginsAdded(
				QMargins(spread, spread - Px(2), spread, spread + Px(2))),
			radius + spread,
			radius + spread);
	}
	p.setBrush(st::boxBg);
	p.drawRoundedRect(card, radius, radius);

	const auto quality = QualityColor(_history->last);
	const auto pad = Px(14);
	auto top = card.y() + pad;
	const auto left = card.x() + pad;
	const auto width = card.width() - pad * 2;
	const auto titleH = st::boxTitleFont->height;

	// Title and quality pill.
	p.setFont(st::boxTitleFont);
	p.setPen(st::windowFg);
	p.drawText(
		QRect(left, top, width, titleH),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tristegram_conn_title(tr::now));
	const auto pillText = QualityText(_history->last);
	p.setFont(st::normalFont);
	const auto pillW = st::normalFont->width(pillText) + Px(26);
	const auto pillH = Px(24);
	const auto pill = QRect(
		left + width - pillW,
		top + (titleH - pillH) / 2,
		pillW,
		pillH);
	p.setPen(Qt::NoPen);
	p.setBrush(WithAlpha(quality, 0.16));
	p.drawRoundedRect(pill, pillH / 2., pillH / 2.);
	p.setBrush(quality);
	p.drawEllipse(
		QPointF(pill.x() + Px(11), pill.y() + pillH / 2.),
		Px(3),
		Px(3));
	p.setPen(quality);
	p.drawText(
		pill.marginsRemoved(QMargins(Px(19), 0, Px(8), 0)),
		Qt::AlignLeft | Qt::AlignVCenter,
		pillText);
	top += titleH + Px(12);

	// Graph.
	const auto graph = QRect(left, top, width, Px(96));
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(graph, Px(14), Px(14));
	p.setPen(QPen(WithAlpha(st::windowSubTextFg->c, 0.25), 1, Qt::DashLine));
	for (const auto f : { 0.25, 0.5, 0.75 }) {
		const auto y = graph.y() + int(graph.height() * f);
		p.drawLine(
			graph.x() + Px(8),
			y,
			graph.x() + graph.width() - Px(8),
			y);
	}
	const auto values = std::vector<int>(
		_history->values.begin(),
		_history->values.end());
	PaintGraph(
		p,
		QRectF(graph.marginsRemoved(QMargins(Px(8), Px(20), Px(10), Px(12)))),
		values,
		quality,
		_reveal.value(1.),
		Px(2));
	if (!values.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(graph.x() + Px(10), graph.y() + Px(3), width, Px(16)),
			Qt::AlignLeft | Qt::AlignVCenter,
			QString::number(_history->maximum()) + ' ' + Ms());
	}
	top += graph.height() + Px(10);

	// Stats: 3 x 2.
	const auto gap = Px(6);
	const auto cellW = (width - gap * 2) / 3;
	const auto cellH = Px(50);
	const auto withMs = [&](int v) {
		return (v < 0)
			? QString::fromUtf8("\xe2\x80\x94")
			: (QString::number(v) + ' ' + Ms());
	};
	const auto stats = std::array<std::pair<QString, QString>, 6>{ {
		{ tr::lng_tristegram_conn_ping(tr::now), withMs(_history->last) },
		{ tr::lng_tristegram_conn_avg(tr::now), withMs(_history->average()) },
		{ tr::lng_tristegram_conn_jitter(tr::now), withMs(_history->jitter()) },
		{ tr::lng_tristegram_conn_min(tr::now), withMs(_history->minimum()) },
		{ tr::lng_tristegram_conn_max(tr::now), withMs(_history->maximum()) },
		{
			tr::lng_tristegram_conn_loss(tr::now),
			QString::number(_history->lossPercent()) + '%',
		},
	} };
	for (auto i = 0; i != 6; ++i) {
		const auto row = i / 3;
		const auto col = i % 3;
		paintStat(
			p,
			QRect(
				left + col * (cellW + gap),
				top + row * (cellH + gap),
				cellW,
				cellH),
			stats[i].first,
			stats[i].second);
	}
	top += cellH * 2 + gap + Px(10);

	// Footer: data center.
	const auto dc = _dcId ? _dcId() : 0;
	auto footer = tr::lng_tristegram_conn_server(tr::now)
		+ u" DC"_q
		+ QString::number(dc);
	const auto city = DcCity(dc);
	if (!city.isEmpty()) {
		footer += QString::fromUtf8(" \xc2\xb7 ") + city;
	}
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(left, top, width, st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		footer);
}

TopBar::TopBar(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _api(&controller->session().mtp()) {
	setMouseTracking(true);
	resize(width(), DefaultHeight());
	_timer.setCallback([=] { tick(); });
	_timer.callEach(1000);
	_hideTimer.setCallback([=] { hidePanel(); });
	_ambient.setCallback([=] { update(); });
	requestPing();
	refreshAmbient();

	Extera::Config::Instance().changes(
	) | rpl::on_next([=](const QString &key) {
		if (key.startsWith(u"tristegram_"_q)
			|| key.startsWith(u"streamer_"_q)) {
			refreshAmbient();
			updateLayout();
			update();
		}
	}, lifetime());

	if (Extera::Bool(u"tristegram_logo_intro"_q)) {
		base::call_delayed(450, this, [=] { spinLogo(); });
	}
}

void TopBar::refreshAmbient() {
	if (LogoAmbientAnimated()) {
		if (!_ambient.isActive()) {
			_ambient.callEach(40);
		}
	} else {
		_ambient.cancel();
	}
}

void TopBar::paintHide(QPainter &p, QRect rect) const {
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	auto bg = st::windowBgActive->c;
	bg.setAlphaF(_over == Area::Hide ? 0.24 : 0.14);
	p.setBrush(bg);
	p.drawRoundedRect(rect, radius, radius);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowActiveTextFg);
	p.drawText(rect, Qt::AlignCenter, tr::lng_tristegram_hide_badge(tr::now));
}

void TopBar::showPluginsMenu() {
	using namespace Extera::Plugins;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto locale = QLocale::system();
	const auto now = QDateTime::currentDateTime();
	auto header = now.toString(u"HH:mm:ss"_q)
		+ QString::fromUtf8("  \xc2\xb7  ")
		+ locale.toString(now.date(), u"dddd, d MMMM"_q);
	_menu->addAction(header, [] {}, &st::menuIconSchedule)->setEnabled(false);
	_menu->addSeparator();
	auto any = false;
	if (Started()) {
		for (const auto &entry : Call(u"list"_q).toArray()) {
			const auto info = entry.toObject();
			const auto id = info.value(u"id"_q).toString();
			const auto name = info.value(u"name"_q).toString();
			if (id.isEmpty()) {
				continue;
			}
			any = true;
			const auto enabled = info.value(u"enabled"_q).toBool();
			_menu->addAction(name.isEmpty() ? id : name, [=] {
				if (!enabled && Extera::Bool(u"plugins_safe_mode"_q)) {
					_controller->showToast(
						tr::lng_exd_safe_mode_blocked(tr::now));
					return;
				}
				Call(u"set_enabled"_q, QJsonObject{
					{ u"id"_q, id },
					{ u"enabled"_q, !enabled },
				});
				notifyListChanged();
			}, enabled ? &st::menuIconSelect : &st::menuIconCustomize);
		}
	}
	if (!any) {
		_menu->addAction(
			tr::lng_tristegram_plugins_none(tr::now),
			[] {},
			&st::menuIconInfo)->setEnabled(false);
	}
	_menu->addSeparator();
	_menu->addAction(tr::lng_tristegram_plugins_all(tr::now), [=] {
		_controller->showSettings(Settings::ExteraPluginsId());
	}, &st::menuIconSettings);
	_menu->popup(mapToGlobal(QPoint(_layout.clock.x(), height())));
}

TopBar::~TopBar() = default;

int TopBar::DefaultHeight() {
	return Px(48);
}

void TopBar::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

QString TopBar::clockText() const {
	return QTime::currentTime().toString(
		Extera::Bool(u"tristegram_bar_seconds"_q)
			? u"HH:mm:ss"_q
			: u"HH:mm"_q);
}

QString TopBar::pingText() const {
	return (_history.last < 0)
		? (QString::fromUtf8("\xe2\x80\x94 ") + Ms())
		: (QString::number(_history.last) + ' ' + Ms());
}

QColor TopBar::qualityColor() const {
	return QualityColor(_history.last);
}

void TopBar::updateLayout() {
	const auto h = height() - Px(8);
	const auto y = Px(4);
	auto layout = Layout();
	layout.account = QRect(Px(6), y, Px(38), h);

	const auto showPing = Extera::Bool(u"tristegram_bar_ping"_q);
	const auto showClock = Extera::Bool(u"tristegram_bar_clock"_q);
	const auto gap = Px(6);
	const auto graphW = Px(40) + Px(6);

	const auto brandW = Px(6) + Px(26) + Px(8)
		+ st::semiboldFont->width(u"Tristegram"_q) + Px(12);
	const auto clockW = st::semiboldFont->width(u"00:00:00"_q)
		+ Px(14) + Px(8) + Px(26) + Px(5);
	auto pingW = Px(12) + graphW
		+ st::semiboldFont->width(u"000 "_q + Ms()) + Px(12);

	auto from = layout.account.x() + layout.account.width() + gap;
	if (Extera::Streamer::Active()) {
		const auto hideW = st::semiboldFont->width(
			tr::lng_tristegram_hide_badge(tr::now)) + Px(20);
		layout.hide = QRect(from, y + Px(6), hideW, h - Px(12));
		from += hideW + gap;
	}
	const auto available = width() - from - Px(6);
	auto withPing = showPing;
	auto withClock = showClock;
	const auto total = [&] {
		return brandW
			+ (withPing ? (pingW + gap) : 0)
			+ (withClock ? (clockW + gap) : 0);
	};
	if (withPing && total() > available) {
		layout.showGraph = false;
		pingW -= graphW;
	}
	if (withPing && total() > available) {
		withPing = false;
	}
	if (withClock && total() > available) {
		withClock = false;
	}
	auto x = from + std::max(0, (available - total()) / 2);
	if (withPing) {
		layout.ping = QRect(x, y, pingW, h);
		x += pingW + gap;
	}
	layout.brand = QRect(x, y, brandW, h);
	x += brandW + gap;
	if (withClock) {
		layout.clock = QRect(x, y, clockW, h);
	}
	_layout = layout;
}

TopBar::Area TopBar::areaAt(QPoint point) const {
	if (_layout.account.contains(point)) {
		return Area::Account;
	} else if (_layout.hide.contains(point)) {
		return Area::Hide;
	} else if (_layout.ping.contains(point)) {
		return Area::Ping;
	} else if (_layout.brand.contains(point)) {
		return Area::Brand;
	} else if (_layout.clock.contains(point)) {
		return Area::Clock;
	}
	return Area::None;
}

void TopBar::paintPill(QPainter &p, QRect rect, bool over) const {
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(over ? st::windowBgRipple : st::windowBgOver);
	p.drawRoundedRect(rect, radius, radius);
}

void TopBar::paintPing(QPainter &p, QRect rect) const {
	paintPill(p, rect, _over == Area::Ping);
	const auto color = qualityColor();
	auto x = rect.x() + Px(12);
	if (_layout.showGraph) {
		const auto from = std::max(
			0,
			int(_history.values.size()) - kGraphPoints);
		const auto values = std::vector<int>(
			_history.values.begin() + from,
			_history.values.end());
		const auto graph = QRectF(
			x,
			rect.y() + Px(11),
			Px(40),
			rect.height() - Px(22));
		PaintGraph(p, graph, values, color, 1., Px(1) * 1.6);
		x += Px(40) + Px(6);
	}
	p.setFont(st::semiboldFont);
	p.setPen(color);
	p.drawText(
		QRect(x, rect.y(), rect.x() + rect.width() - x, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		pingText());
}

void TopBar::paintBrand(QPainter &p, QRect rect) const {
	if (_over == Area::Brand) {
		paintPill(p, rect, false);
	}
	const auto logoSize = Px(26);
	const auto logo = QRect(
		rect.x() + Px(6),
		rect.y() + (rect.height() - logoSize) / 2,
		logoSize,
		logoSize);
	const auto logoStyle = CurrentLogoStyle(crl::now());
	PaintLogo(p, QRectF(logo), logoStyle, _spin.value(1.));

	auto textGradient = QLinearGradient(
		QPointF(logo.x() + logo.width() + Px(8), 0),
		QPointF(rect.x() + rect.width(), 0));
	textGradient.setColorAt(0., logoStyle.from);
	textGradient.setColorAt(1., logoStyle.to);
	p.setFont(st::semiboldFont);
	p.setPen(QPen(QBrush(textGradient), 1));
	p.drawText(
		QRect(
			logo.x() + logo.width() + Px(8),
			rect.y(),
			rect.width(),
			rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		u"Tristegram"_q);
}

void TopBar::paintClock(QPainter &p, QRect rect) const {
	paintPill(p, rect, _over == Area::Clock);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(
		QRect(rect.x() + Px(14), rect.y(), rect.width(), rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		clockText());

	// Round plugins badge in the logo colors.
	const auto size = Px(26);
	const auto badge = QRect(
		rect.x() + rect.width() - size - Px(5),
		rect.y() + (rect.height() - size) / 2,
		size,
		size);
	const auto logoStyle = CurrentLogoStyle(crl::now());
	auto fill = QLinearGradient(badge.topLeft(), badge.bottomRight());
	fill.setColorAt(0., logoStyle.from);
	fill.setColorAt(1., logoStyle.to);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawEllipse(badge);
	const auto &icon = st::exteraIconPlugins;
	if (icon.width() > 0) {
		p.save();
		const auto inner = size - Px(8);
		const auto scale = inner / double(icon.width());
		p.translate(badge.x() + Px(4), badge.y() + Px(4));
		p.scale(scale, scale);
		icon.paint(p, QPoint(0, 0), icon.width(), st::windowFgActive->c);
		p.restore();
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	p.fillRect(rect(), st::dialogsBg);

	const auto account = _layout.account;
	if (_over == Area::Account) {
		paintPill(p, account, false);
	}
	const auto userpicSize = Px(30);
	_controller->session().user()->paintUserpicLeft(
		p,
		_userpic,
		account.x() + (account.width() - userpicSize) / 2,
		account.y() + (account.height() - userpicSize) / 2,
		width(),
		userpicSize);

	if (!_layout.hide.isEmpty()) {
		paintHide(p, _layout.hide);
	}
	if (!_layout.ping.isEmpty()) {
		paintPing(p, _layout.ping);
	}
	paintBrand(p, _layout.brand);
	if (!_layout.clock.isEmpty()) {
		paintClock(p, _layout.clock);
	}
}

void TopBar::mouseMoveEvent(QMouseEvent *e) {
	const auto over = areaAt(e->pos());
	if (_over != over) {
		_over = over;
		if (over == Area::Brand) {
			spinLogo();
		}
		if (over == Area::Ping) {
			showPanel();
		} else {
			hidePanelDelayed();
		}
		setCursor((over == Area::None)
			? style::cur_default
			: style::cur_pointer);
		update();
	}
}

void TopBar::leaveEventHook(QEvent *e) {
	if (_over != Area::None) {
		_over = Area::None;
		setCursor(style::cur_default);
		update();
	}
	hidePanelDelayed();
}

void TopBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	switch (areaAt(e->pos())) {
	case Area::Account: showAccountsMenu(); break;
	case Area::Hide: Extera::Streamer::ShowMenu(_controller); break;
	case Area::Brand: spinLogo(); showBrandMenu(); break;
	case Area::Ping:
		if (_panel && _panel->isVisible()) {
			hidePanel();
		} else {
			showPanel();
		}
		break;
	case Area::Clock:
		hidePanel();
		showPluginsMenu();
		break;
	default: break;
	}
}

void TopBar::showPanel() {
	_hideTimer.cancel();
	const auto host = window();
	if (!host || _layout.ping.isEmpty()) {
		return;
	}
	if (!_panel) {
		_panel = base::make_unique_q<ConnectionPanel>(
			host,
			&_history,
			[=] { return int(_controller->session().mtp().mainDcId()); });
		_panel->entered = [=] { _hideTimer.cancel(); };
		_panel->left = [=] { hidePanelDelayed(); };
	}
	const auto size = ConnectionPanel::Size();
	const auto anchor = mapTo(
		host,
		QPoint(
			_layout.ping.x() + _layout.ping.width() / 2,
			_layout.ping.y() + _layout.ping.height()));
	const auto x = std::clamp(
		anchor.x() - size.width() / 2,
		0,
		std::max(0, host->width() - size.width()));
	_panel->setGeometry(
		x,
		anchor.y() - ConnectionPanel::Shadow() + Px(6),
		size.width(),
		size.height());
	if (_panel->isHidden()) {
		_panel->showAnimated();
	} else {
		_panel->raise();
		_panel->update();
	}
}

void TopBar::hidePanelDelayed() {
	if (_panel && !_panel->isHidden()) {
		_hideTimer.callOnce(250);
	}
}

void TopBar::hidePanel() {
	_hideTimer.cancel();
	if (!_panel || _panel->isHidden()) {
		return;
	}
	const auto raw = _panel.get();
	raw->hideAnimated([=] {
		if (_panel.get() == raw) {
			raw->hide();
		}
	});
}

void TopBar::tick() {
	if (++_ticks % kPingEachTicks == 0) {
		requestPing();
	}
	update();
}

void TopBar::spinLogo() {
	if (!LogoAnimationEnabled() || _spin.animating()) {
		return;
	}
	_spin.start([=] { update(); }, 0., 1., 900);
}

void TopBar::requestPing() {
	if (_pingSent) {
		return;
	}
	_pingSent = crl::now();
	const auto refresh = [=] {
		_pingSent = 0;
		update();
		if (_panel) {
			_panel->update();
		}
	};
	_api.request(MTPhelp_GetNearestDc()).done([=] {
		_history.add(int(crl::now() - _pingSent));
		refresh();
	}).fail([=] {
		_history.fail();
		refresh();
	}).send();
}

void TopBar::showAccountsMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto current = &_controller->session().account();
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (!account->sessionExists()) {
			continue;
		}
		auto text = account->session().user()->name();
		const auto unread = account->session().data().unreadBadge();
		if (unread > 0 && account != current) {
			text += QString::fromUtf8("  \xe2\x80\xa2  ")
				+ QString::number(unread);
		}
		_menu->addAction(text, [=] {
			if (account != &_controller->session().account()) {
				Core::App().domain().maybeActivate(account);
			}
		}, (account == current) ? &st::menuIconSelect : &st::menuIconProfile);
	}
	_menu->addSeparator();
	const auto environment = _controller->session().mtp().environment();
	_menu->addAction(tr::lng_menu_add_account(tr::now), [=] {
		Core::App().domain().addActivated(environment);
	}, &st::menuIconAddAccount);
	_menu->popup(mapToGlobal(QPoint(_layout.account.x(), height())));
}

void TopBar::showBrandMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(tr::lng_tristegram_settings(tr::now), [=] {
		_controller->showSettings(Settings::ExteraId());
	}, &st::menuIconSettings);
	_menu->addAction(tr::lng_tristegram_themes(tr::now), [=] {
		_controller->showSettings(Settings::ChatId());
	}, &st::menuIconPalette);
	_menu->addAction(tr::lng_tristegram_plugins(tr::now), [=] {
		_controller->showSettings(Settings::ExteraPluginsId());
	}, &st::menuIconCustomize);
	_menu->addAction(tr::lng_tristegram_whats_new(tr::now), [=] {
		Extera::Changelog::Show(_controller);
	}, &st::menuIconInfo);
	_menu->popup(mapToGlobal(QPoint(_layout.brand.x(), height())));
}

} // namespace Tristegram
