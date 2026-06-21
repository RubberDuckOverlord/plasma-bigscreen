/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QList>
#include <QSet>
#include <QString>

enum class InputAction {
    None,
    NavigateUp,
    NavigateDown,
    NavigateLeft,
    NavigateRight,
    Select,
    Back,
    Menu,
    Previous,
    Next,
    Home,
    SystemMenu,
    BrowserBack,
    BrowserForward,
    Play,
    Stop,
    Pause,
    Rewind,
    FastForward,
    Number0,
    Number1,
    Number2,
    Number3,
    Number4,
    Number5,
    Number6,
    Number7,
    Number8,
    Number9,
    ColorBlue,
    ColorRed,
    ColorGreen,
    ColorYellow,
    ChannelUp,
    ChannelDown,
    Subtitle,
    Info,
};

QList<int> keysForInputAction(InputAction action);
QSet<int> keysForInputActions(const QList<InputAction> &actions);
InputAction inputActionForKey(int key);
bool inputActionEmitsHome(InputAction action);
bool inputActionAllowedWhenSuppressed(InputAction action);
QString inputActionName(InputAction action);
