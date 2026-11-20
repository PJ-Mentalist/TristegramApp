/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QWidget;

namespace Tristegram {

// Takes a picture of the active window right before a theme change and
// fades it out over the new palette (tristegram_theme_fade).
void PrepareThemeFade();

// The old chat leaves over the new one: slide, fade, scale or flip,
// "tristegram_transition". Call right before the chat changes.
void PrepareChatTransition(not_null<QWidget*> widget);

// Animated logo over the window on the first show (tristegram_splash).
void ShowSplash(not_null<QWidget*> parent);

} // namespace Tristegram
