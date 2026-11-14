/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "mtproto/sender.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Header row above the chat list: current account (click to switch),
// the Tristegram logo with its menu, and a clock with the ping to Telegram.
class TopBar final : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] static int DefaultHeight();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Area {
		None,
		Account,
		Brand,
		Status,
	};

	[[nodiscard]] Area areaAt(QPoint point) const;
	[[nodiscard]] QRect accountRect() const;
	[[nodiscard]] QRect brandRect() const;
	[[nodiscard]] QRect statusRect() const;
	[[nodiscard]] QString clockText() const;
	[[nodiscard]] QString pingText() const;

	void tick();
	void requestPing();
	void showAccountsMenu();
	void showBrandMenu();

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;
	Ui::PeerUserpicView _userpic;
	QImage _logo;
	base::Timer _timer;
	base::unique_qptr<Ui::PopupMenu> _menu;
	Area _over = Area::None;
	crl::time _pingSent = 0;
	int _ping = -1;
	int _ticks = 0;

};

} // namespace Tristegram
