/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>

class QPainter;
class QRectF;

namespace Ui::TristegramAccent {

// Lets the UI library use the Tristegram logo colors and settings
// without depending on the app code: the app sets the providers once.
struct Gradient {
	QColor from;
	QColor to;
};

void SetGradientProvider(Fn<Gradient()> provider);
void SetFlagProvider(Fn<bool(const QString &key)> provider);

// The logo gradient, or the active window color when nothing is set.
[[nodiscard]] Gradient Current();

// A Tristegram setting, false when nothing is set.
[[nodiscard]] bool Flag(const QString &key);

// Fills a rounded rect (a pill when radius is negative) with the gradient.
void FillGradient(QPainter &p, const QRectF &rect, double radius = -1.);

} // namespace Ui::TristegramAccent
