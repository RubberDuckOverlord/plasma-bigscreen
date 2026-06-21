// SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <KConfigGroup>
#include <KConfigWatcher>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QObject>
#include <qqmlregistration.h>

class BigscreenShellSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

    Q_PROPERTY(bool pmInhibitionEnabled READ pmInhibitionEnabled WRITE setPmInhibitionEnabled NOTIFY pmInhibitionEnabledChanged)
    Q_PROPERTY(bool navigationSoundEnabled READ navigationSoundEnabled WRITE setNavigationSoundEnabled NOTIFY navigationSoundEnabledChanged)
    Q_PROPERTY(bool windowDecorationsEnabled READ windowDecorationsEnabled WRITE setWindowDecorationsEnabled NOTIFY windowDecorationsEnabledChanged)
    Q_PROPERTY(bool powerButtonTurnsOffScreen READ powerButtonTurnsOffScreen WRITE setPowerButtonTurnsOffScreen NOTIFY powerButtonTurnsOffScreenChanged)
    Q_PROPERTY(bool automaticScreenOffEnabled READ automaticScreenOffEnabled WRITE setAutomaticScreenOffEnabled NOTIFY automaticScreenOffChanged)
    Q_PROPERTY(int automaticScreenOffMinutes READ automaticScreenOffMinutes WRITE setAutomaticScreenOffMinutes NOTIFY automaticScreenOffChanged)

public:
    BigscreenShellSettings(QObject *parent = nullptr);

    bool pmInhibitionEnabled() const;
    void setPmInhibitionEnabled(bool pmInhibitionEnabled);

    bool navigationSoundEnabled() const;
    void setNavigationSoundEnabled(bool navigationSoundEnabled);

    bool windowDecorationsEnabled() const;
    void setWindowDecorationsEnabled(bool windowDecorationsEnabled);

    bool powerButtonTurnsOffScreen() const;
    void setPowerButtonTurnsOffScreen(bool powerButtonTurnsOffScreen);

    bool automaticScreenOffEnabled() const;
    void setAutomaticScreenOffEnabled(bool automaticScreenOffEnabled);

    int automaticScreenOffMinutes() const;
    void setAutomaticScreenOffMinutes(int minutes);

Q_SIGNALS:
    void pmInhibitionEnabledChanged();
    void navigationSoundEnabledChanged();
    void windowDecorationsEnabledChanged();
    void powerButtonTurnsOffScreenChanged();
    void automaticScreenOffChanged();

private:
    void applyPowerDevilPowerButtonSetting(bool enabled, KConfigGroup &settingsGroup);
    void applyPowerDevilAutomaticScreenOffSetting(bool enabled, int minutes, KConfigGroup &settingsGroup);

    KConfigWatcher::Ptr m_configWatcher;
    KSharedConfig::Ptr m_config;
};
