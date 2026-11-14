/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "ui/effects/animations.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "mtproto/sender.h"

#include <deque>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// Ping measurements shared by the bar and its connection panel.
struct PingHistory {
	std::deque<int> values; // milliseconds, latest last
	int sent = 0;
	int failed = 0;
	int last = -1;

	void add(int ms);
	void fail();
	[[nodiscard]] int average() const;
	[[nodiscard]] int jitter() const;
	[[nodiscard]] int minimum() const;
	[[nodiscard]] int maximum() const;
	[[nodiscard]] int lossPercent() const;
};

class ConnectionPanel;

// Header row above the chat list: current account (click to switch),
// then the ping graph, the Tristegram logo with its menu and the clock
// (click opens plugins).
class TopBar final : public Ui::RpWidget {
public:
	TopBar(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~TopBar();

	[[nodiscard]] static int DefaultHeight();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	enum class Area {
		None,
		Account,
		Ping,
		Brand,
		Clock,
	};

	struct Layout {
		QRect account;
		QRect ping;
		QRect brand;
		QRect clock;
		bool showGraph = true;
	};

	[[nodiscard]] Area areaAt(QPoint point) const;
	[[nodiscard]] QString clockText() const;
	[[nodiscard]] QString pingText() const;
	[[nodiscard]] QColor qualityColor() const;
	void updateLayout();

	void paintPill(QPainter &p, QRect rect, bool over) const;
	void paintPing(QPainter &p, QRect rect) const;
	void paintBrand(QPainter &p, QRect rect) const;
	void paintClock(QPainter &p, QRect rect) const;

	void tick();
	void spinLogo();
	void requestPing();
	void showAccountsMenu();
	void showBrandMenu();
	void showPanel();
	void hidePanelDelayed();
	void hidePanel();

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;
	Ui::PeerUserpicView _userpic;
	Ui::Animations::Simple _spin;
	base::Timer _timer;
	base::Timer _hideTimer;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::unique_qptr<ConnectionPanel> _panel;
	PingHistory _history;
	Layout _layout;
	Area _over = Area::None;
	crl::time _pingSent = 0;
	int _ticks = 0;

};

} // namespace Tristegram
