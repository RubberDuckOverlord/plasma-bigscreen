/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "inputaction.h"

#include <linux/input-event-codes.h>

QList<int> keysForInputAction(InputAction action)
{
    switch (action) {
    case InputAction::NavigateUp:
        return {KEY_UP};
    case InputAction::NavigateDown:
        return {KEY_DOWN};
    case InputAction::NavigateLeft:
        return {KEY_LEFT};
    case InputAction::NavigateRight:
        return {KEY_RIGHT};
    case InputAction::Select:
        return {KEY_ENTER};
    case InputAction::Back:
        return {KEY_CANCEL, KEY_ESC};
    case InputAction::Menu:
        return {KEY_MENU};
    case InputAction::Previous:
        return {KEY_LEFTSHIFT, KEY_TAB};
    case InputAction::Next:
        return {KEY_TAB};
    case InputAction::Home:
        return {KEY_LEFTMETA};
    case InputAction::SystemMenu:
        return {KEY_GAMES};
    case InputAction::BrowserBack:
        return {KEY_BACK};
    case InputAction::BrowserForward:
        return {KEY_FORWARD};
    case InputAction::Play:
        return {KEY_PLAY};
    case InputAction::Stop:
        return {KEY_STOP};
    case InputAction::Pause:
        return {KEY_PAUSE};
    case InputAction::Rewind:
        return {KEY_REWIND};
    case InputAction::FastForward:
        return {KEY_FASTFORWARD};
    case InputAction::Number0:
        return {KEY_0};
    case InputAction::Number1:
        return {KEY_1};
    case InputAction::Number2:
        return {KEY_2};
    case InputAction::Number3:
        return {KEY_3};
    case InputAction::Number4:
        return {KEY_4};
    case InputAction::Number5:
        return {KEY_5};
    case InputAction::Number6:
        return {KEY_6};
    case InputAction::Number7:
        return {KEY_7};
    case InputAction::Number8:
        return {KEY_8};
    case InputAction::Number9:
        return {KEY_9};
    case InputAction::ColorBlue:
        return {KEY_BLUE};
    case InputAction::ColorRed:
        return {KEY_RED};
    case InputAction::ColorGreen:
        return {KEY_GREEN};
    case InputAction::ColorYellow:
        return {KEY_YELLOW};
    case InputAction::ChannelUp:
        return {KEY_CHANNELUP};
    case InputAction::ChannelDown:
        return {KEY_CHANNELDOWN};
    case InputAction::Subtitle:
        return {KEY_SUBTITLE};
    case InputAction::Info:
        return {KEY_INFO};
    case InputAction::None:
        return {};
    }
    return {};
}

QSet<int> keysForInputActions(const QList<InputAction> &actions)
{
    QSet<int> keys;
    for (InputAction action : actions) {
        const QList<int> actionKeys = keysForInputAction(action);
        for (int key : actionKeys) {
            keys.insert(key);
        }
    }
    return keys;
}

InputAction inputActionForKey(int key)
{
    switch (key) {
    case KEY_UP:
        return InputAction::NavigateUp;
    case KEY_DOWN:
        return InputAction::NavigateDown;
    case KEY_LEFT:
        return InputAction::NavigateLeft;
    case KEY_RIGHT:
        return InputAction::NavigateRight;
    case KEY_ENTER:
        return InputAction::Select;
    case KEY_CANCEL:
    case KEY_ESC:
        return InputAction::Back;
    case KEY_MENU:
        return InputAction::Menu;
    case KEY_TAB:
        return InputAction::Next;
    case KEY_LEFTMETA:
    case KEY_HOMEPAGE:
        return InputAction::Home;
    case KEY_GAMES:
        return InputAction::SystemMenu;
    case KEY_BACK:
        return InputAction::BrowserBack;
    case KEY_FORWARD:
        return InputAction::BrowserForward;
    case KEY_PLAY:
        return InputAction::Play;
    case KEY_STOP:
        return InputAction::Stop;
    case KEY_PAUSE:
        return InputAction::Pause;
    case KEY_REWIND:
        return InputAction::Rewind;
    case KEY_FASTFORWARD:
        return InputAction::FastForward;
    case KEY_0:
        return InputAction::Number0;
    case KEY_1:
        return InputAction::Number1;
    case KEY_2:
        return InputAction::Number2;
    case KEY_3:
        return InputAction::Number3;
    case KEY_4:
        return InputAction::Number4;
    case KEY_5:
        return InputAction::Number5;
    case KEY_6:
        return InputAction::Number6;
    case KEY_7:
        return InputAction::Number7;
    case KEY_8:
        return InputAction::Number8;
    case KEY_9:
        return InputAction::Number9;
    case KEY_BLUE:
        return InputAction::ColorBlue;
    case KEY_RED:
        return InputAction::ColorRed;
    case KEY_GREEN:
        return InputAction::ColorGreen;
    case KEY_YELLOW:
        return InputAction::ColorYellow;
    case KEY_CHANNELUP:
        return InputAction::ChannelUp;
    case KEY_CHANNELDOWN:
        return InputAction::ChannelDown;
    case KEY_SUBTITLE:
        return InputAction::Subtitle;
    case KEY_INFO:
        return InputAction::Info;
    default:
        return InputAction::None;
    }
}

bool inputActionEmitsHome(InputAction action)
{
    return action == InputAction::Home;
}

bool inputActionAllowedWhenSuppressed(InputAction action)
{
    return action == InputAction::Home || action == InputAction::SystemMenu;
}

QString inputActionName(InputAction action)
{
    switch (action) {
    case InputAction::NavigateUp:
        return QStringLiteral("navigate-up");
    case InputAction::NavigateDown:
        return QStringLiteral("navigate-down");
    case InputAction::NavigateLeft:
        return QStringLiteral("navigate-left");
    case InputAction::NavigateRight:
        return QStringLiteral("navigate-right");
    case InputAction::Select:
        return QStringLiteral("select");
    case InputAction::Back:
        return QStringLiteral("back");
    case InputAction::Menu:
        return QStringLiteral("menu");
    case InputAction::Previous:
        return QStringLiteral("previous");
    case InputAction::Next:
        return QStringLiteral("next");
    case InputAction::Home:
        return QStringLiteral("home");
    case InputAction::SystemMenu:
        return QStringLiteral("system-menu");
    case InputAction::BrowserBack:
        return QStringLiteral("browser-back");
    case InputAction::BrowserForward:
        return QStringLiteral("browser-forward");
    case InputAction::Play:
        return QStringLiteral("play");
    case InputAction::Stop:
        return QStringLiteral("stop");
    case InputAction::Pause:
        return QStringLiteral("pause");
    case InputAction::Rewind:
        return QStringLiteral("rewind");
    case InputAction::FastForward:
        return QStringLiteral("fast-forward");
    case InputAction::Number0:
        return QStringLiteral("number-0");
    case InputAction::Number1:
        return QStringLiteral("number-1");
    case InputAction::Number2:
        return QStringLiteral("number-2");
    case InputAction::Number3:
        return QStringLiteral("number-3");
    case InputAction::Number4:
        return QStringLiteral("number-4");
    case InputAction::Number5:
        return QStringLiteral("number-5");
    case InputAction::Number6:
        return QStringLiteral("number-6");
    case InputAction::Number7:
        return QStringLiteral("number-7");
    case InputAction::Number8:
        return QStringLiteral("number-8");
    case InputAction::Number9:
        return QStringLiteral("number-9");
    case InputAction::ColorBlue:
        return QStringLiteral("color-blue");
    case InputAction::ColorRed:
        return QStringLiteral("color-red");
    case InputAction::ColorGreen:
        return QStringLiteral("color-green");
    case InputAction::ColorYellow:
        return QStringLiteral("color-yellow");
    case InputAction::ChannelUp:
        return QStringLiteral("channel-up");
    case InputAction::ChannelDown:
        return QStringLiteral("channel-down");
    case InputAction::Subtitle:
        return QStringLiteral("subtitle");
    case InputAction::Info:
        return QStringLiteral("info");
    case InputAction::None:
        return QStringLiteral("none");
    }
    return QStringLiteral("none");
}
