/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QPainter;

namespace Tristegram {

// Click and typing effects. Each burst is painted in its own small
// see-through window that stays on top of everything, so it is never
// covered by menus, popups or other windows, and never takes the mouse.
// "tristegram_fx_click", "tristegram_fx_type": the kinds below,
// "tristegram_fx_color", "tristegram_fx_size", "tristegram_fx_amount".
void InstallInputEffects();

[[nodiscard]] int ClickEffectsCount();
[[nodiscard]] QString ClickEffectName(int index);
[[nodiscard]] int TypeEffectsCount();
[[nodiscard]] QString TypeEffectName(int index);

// A still frame of the effect for the settings tiles.
void PaintClickEffectPreview(QPainter &p, QRectF rect, int index);
void PaintTypeEffectPreview(QPainter &p, QRectF rect, int index);

// Plays the effect at the point, for trying it in the settings.
void PlayClickEffect(QPoint global, int index);

} // namespace Tristegram
