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
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_extera.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QTime>

namespace Tristegram {
namespace {

constexpr auto kPingEachTicks = 10;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

} // namespace

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
	requestPing();

	Extera::Config::Instance().changes(
	) | rpl::on_next([=](const QString &key) {
		if (key.startsWith(u"tristegram_"_q)) {
			update();
		}
	}, lifetime());
}

int TopBar::DefaultHeight() {
	return Px(44);
}

QRect TopBar::accountRect() const {
	return QRect(Px(8), Px(4), Px(38), height() - Px(8));
}

QRect TopBar::brandRect() const {
	const auto account = accountRect();
	const auto textWidth = st::semiboldFont->width(u"Tristegram"_q);
	return QRect(
		account.x() + account.width() + Px(4),
		account.y(),
		Px(6) + Px(26) + Px(8) + textWidth + Px(10),
		account.height());
}

QString TopBar::clockText() const {
	return QTime::currentTime().toString(Extera::Bool(u"tristegram_bar_seconds"_q)
		? u"HH:mm:ss"_q
		: u"HH:mm"_q);
}

QString TopBar::pingText() const {
	const auto ms = QString::fromUtf16(u"мс");
	return (_ping < 0)
		? (u"— "_q + ms)
		: (QString::number(_ping) + ' ' + ms);
}

QRect TopBar::statusRect() const {
	const auto text = Extera::Bool(u"tristegram_bar_ping"_q)
		? (clockText() + u"   "_q + pingText())
		: clockText();
	const auto w = st::normalFont->width(text) + Px(16);
	return QRect(width() - w - Px(6), Px(4), w, height() - Px(8));
}

TopBar::Area TopBar::areaAt(QPoint point) const {
	if (accountRect().contains(point)) {
		return Area::Account;
	} else if (brandRect().contains(point)) {
		return Area::Brand;
	}
	return Area::None;
}

void TopBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);

	p.fillRect(rect(), st::dialogsBg);

	const auto radius = Px(10);
	const auto account = accountRect();
	const auto brand = brandRect();
	p.setPen(Qt::NoPen);
	if (_over == Area::Account) {
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(account, radius, radius);
	} else if (_over == Area::Brand) {
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(brand, radius, radius);
	}

	const auto userpicSize = Px(30);
	_controller->session().user()->paintUserpicLeft(
		p,
		_userpic,
		account.x() + (account.width() - userpicSize) / 2,
		account.y() + (account.height() - userpicSize) / 2,
		width(),
		userpicSize);

	const auto logoSize = Px(26);
	const auto logo = QRect(
		brand.x() + Px(6),
		brand.y() + (brand.height() - logoSize) / 2,
		logoSize,
		logoSize);
	const auto logoStyle = Tristegram::CurrentLogoStyle();
	Tristegram::PaintLogo(p, QRectF(logo), logoStyle, _spin.value(1.));

	auto textGradient = QLinearGradient(
		QPointF(logo.x() + logo.width() + Px(8), 0),
		QPointF(brand.x() + brand.width(), 0));
	textGradient.setColorAt(0., logoStyle.from);
	textGradient.setColorAt(1., logoStyle.to);
	p.setFont(st::semiboldFont);
	p.setPen(QPen(QBrush(textGradient), 1));
	p.drawText(
		QRect(
			logo.x() + logo.width() + Px(8),
			brand.y(),
			brand.width(),
			brand.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		u"Tristegram"_q);

	const auto status = statusRect();
	if (status.x() > brand.x() + brand.width() + Px(4)) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(status, radius, radius);
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		const auto inner = status.marginsRemoved(
			QMargins(Px(8), 0, Px(8), 0));
		p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, clockText());
		if (Extera::Bool(u"tristegram_bar_ping"_q)) {
			p.setPen(st::windowBgActive);
			p.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, pingText());
		}
	}
}

void TopBar::mouseMoveEvent(QMouseEvent *e) {
	const auto over = areaAt(e->pos());
	if (_over != over) {
		_over = over;
		if (over == Area::Brand) {
			spinLogo();
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
}

void TopBar::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	switch (areaAt(e->pos())) {
	case Area::Account: showAccountsMenu(); break;
	case Area::Brand: spinLogo(); showBrandMenu(); break;
	default: break;
	}
}

void TopBar::tick() {
	if (++_ticks % kPingEachTicks == 0) {
		requestPing();
	}
	update();
}

void TopBar::spinLogo() {
	if (!Tristegram::LogoAnimationEnabled() || _spin.animating()) {
		return;
	}
	_spin.start([=] { update(); }, 0., 1., 900);
}

void TopBar::requestPing() {
	if (_pingSent) {
		return;
	}
	_pingSent = crl::now();
	_api.request(MTPhelp_GetNearestDc()).done([=] {
		_ping = int(crl::now() - _pingSent);
		_pingSent = 0;
		update();
	}).fail([=] {
		_ping = -1;
		_pingSent = 0;
		update();
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
			text += u"  •  "_q + QString::number(unread);
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
	_menu->popup(mapToGlobal(QPoint(accountRect().x(), height())));
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
	_menu->addAction(tr::lng_tristegram_whats_new(tr::now), [=] {
		Extera::Changelog::Show(_controller);
	}, &st::menuIconInfo);
	_menu->popup(mapToGlobal(QPoint(brandRect().x(), height())));
}

} // namespace Tristegram
