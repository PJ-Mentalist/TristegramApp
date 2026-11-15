/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class PeerData;
class QPainter;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Tristegram {

// Arrows on both sides of my own profile layer: each switches to the
// previous or the next account and opens its profile.
void SetupAccountArrows(
	not_null<Ui::RpWidget*> layer,
	not_null<PeerData*> peer);

// A soft glow of the logo colors behind the profile photo.
void PaintProfileGlow(QPainter &p, QRect area, QRect userpic);

// A gradient ring around the profile photo.
void PaintProfileRing(QPainter &p, QRect userpic);

} // namespace Tristegram
