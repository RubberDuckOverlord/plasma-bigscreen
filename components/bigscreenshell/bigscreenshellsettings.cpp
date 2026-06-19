// SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bigscreenshellsettings.h"

#include <KIO/CommandLauncherJob>
#include <KNotificationJobUiDelegate>
#include <algorithm>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QStringList>

const QString CONFIG_FILE = QStringLiteral("plasmabigscreenrc");
const QString GENERAL_CONFIG_GROUP = QStringLiteral("General");

namespace
{
constexpr int POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION = 16;
constexpr int POWERDEVIL_TURN_OFF_SCREEN_ACTION = 64;
constexpr int DEFAULT_AUTOMATIC_SCREEN_OFF_MINUTES = 20;
constexpr int MINIMUM_AUTOMATIC_SCREEN_OFF_MINUTES = 1;

const QStringList POWERDEVIL_PROFILE_GROUPS = {
    QStringLiteral("AC"),
    QStringLiteral("Battery"),
    QStringLiteral("LowBattery"),
};

void reloadPowerDevilConfiguration()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.PowerManagement"),
                                                      QStringLiteral("/org/kde/Solid/PowerManagement"),
                                                      QStringLiteral("org.kde.Solid.PowerManagement"),
                                                      QStringLiteral("reparseConfiguration"));
    QDBusConnection::sessionBus().asyncCall(msg);
}
}

BigscreenShellSettings::BigscreenShellSettings(QObject *parent)
    : QObject{parent}
    , m_config{KSharedConfig::openConfig(CONFIG_FILE)}
{
    m_configWatcher = KConfigWatcher::create(m_config);
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group, const QByteArrayList &names) -> void {
        Q_UNUSED(names)
        if (group.name() == GENERAL_CONFIG_GROUP) {
            Q_EMIT pmInhibitionEnabledChanged();
            Q_EMIT navigationSoundEnabledChanged();
            Q_EMIT windowDecorationsEnabledChanged();
            Q_EMIT powerButtonTurnsOffScreenChanged();
            Q_EMIT automaticScreenOffChanged();
        }
    });

    KConfigGroup group{m_config, GENERAL_CONFIG_GROUP};
    if (powerButtonTurnsOffScreen()) {
        applyPowerDevilPowerButtonSetting(true, group);
    }
    if (automaticScreenOffEnabled()) {
        applyPowerDevilAutomaticScreenOffSetting(true, automaticScreenOffMinutes(), group);
    }
    m_config->sync();
}

bool BigscreenShellSettings::pmInhibitionEnabled() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("pmInhibitionEnabled", true);
}

void BigscreenShellSettings::setPmInhibitionEnabled(bool pmInhibitionEnabled)
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    group.writeEntry("pmInhibitionEnabled", pmInhibitionEnabled, KConfigGroup::Notify);
    m_config->sync();
}

bool BigscreenShellSettings::navigationSoundEnabled() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("navigationSoundEnabled", false);
}

void BigscreenShellSettings::setNavigationSoundEnabled(bool navigationSoundEnabled)
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    group.writeEntry("navigationSoundEnabled", navigationSoundEnabled, KConfigGroup::Notify);
    m_config->sync();
}

bool BigscreenShellSettings::windowDecorationsEnabled() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("windowDecorationsEnabled", false);
}

void BigscreenShellSettings::setWindowDecorationsEnabled(bool windowDecorationsEnabled)
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    group.writeEntry("windowDecorationsEnabled", windowDecorationsEnabled, KConfigGroup::Notify);
    m_config->sync();

    // Apply the decoration change to the running shell environment.
    auto *job = new KIO::CommandLauncherJob(QStringLiteral("plasma-bigscreen-envmanager --apply-settings"), {});
    job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
    job->setDesktopName(QStringLiteral("org.kde.plasma-bigscreen-envmanager"));
    job->start();
}

bool BigscreenShellSettings::powerButtonTurnsOffScreen() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("powerButtonTurnsOffScreen", false);
}

void BigscreenShellSettings::setPowerButtonTurnsOffScreen(bool powerButtonTurnsOffScreen)
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    if (group.readEntry("powerButtonTurnsOffScreen", false) == powerButtonTurnsOffScreen) {
        return;
    }

    group.writeEntry("powerButtonTurnsOffScreen", powerButtonTurnsOffScreen, KConfigGroup::Notify);
    applyPowerDevilPowerButtonSetting(powerButtonTurnsOffScreen, group);
    m_config->sync();
}

bool BigscreenShellSettings::automaticScreenOffEnabled() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("automaticScreenOffEnabled", false);
}

void BigscreenShellSettings::setAutomaticScreenOffEnabled(bool automaticScreenOffEnabled)
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    if (group.readEntry("automaticScreenOffEnabled", false) == automaticScreenOffEnabled) {
        return;
    }

    group.writeEntry("automaticScreenOffEnabled", automaticScreenOffEnabled, KConfigGroup::Notify);
    applyPowerDevilAutomaticScreenOffSetting(automaticScreenOffEnabled, automaticScreenOffMinutes(), group);
    m_config->sync();
}

int BigscreenShellSettings::automaticScreenOffMinutes() const
{
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    return group.readEntry("automaticScreenOffMinutes", DEFAULT_AUTOMATIC_SCREEN_OFF_MINUTES);
}

void BigscreenShellSettings::setAutomaticScreenOffMinutes(int minutes)
{
    const int clampedMinutes = std::max(minutes, MINIMUM_AUTOMATIC_SCREEN_OFF_MINUTES);
    auto group = KConfigGroup{m_config, GENERAL_CONFIG_GROUP};
    if (group.readEntry("automaticScreenOffMinutes", DEFAULT_AUTOMATIC_SCREEN_OFF_MINUTES) == clampedMinutes) {
        return;
    }

    group.writeEntry("automaticScreenOffMinutes", clampedMinutes, KConfigGroup::Notify);
    if (automaticScreenOffEnabled()) {
        applyPowerDevilAutomaticScreenOffSetting(true, clampedMinutes, group);
    }
    m_config->sync();
}

void BigscreenShellSettings::applyPowerDevilPowerButtonSetting(bool enabled, KConfigGroup &settingsGroup)
{
    KSharedConfig::Ptr powerDevilConfig = KSharedConfig::openConfig(QStringLiteral("powermanagementprofilesrc"));
    bool changed = false;

    for (const QString &profile : POWERDEVIL_PROFILE_GROUPS) {
        KConfigGroup profileGroup{powerDevilConfig, profile};
        KConfigGroup handleButtonEventsGroup{&profileGroup, QStringLiteral("HandleButtonEvents")};
        const QString previousActionKey = QStringLiteral("previousPowerButtonAction_%1").arg(profile);

        if (enabled) {
            if (!settingsGroup.hasKey(previousActionKey)) {
                const int previousAction =
                    handleButtonEventsGroup.readEntry("powerButtonAction", POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION);
                settingsGroup.writeEntry(previousActionKey, previousAction);
            }

            if (handleButtonEventsGroup.readEntry("powerButtonAction", POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION) != POWERDEVIL_TURN_OFF_SCREEN_ACTION) {
                handleButtonEventsGroup.writeEntry("powerButtonAction", POWERDEVIL_TURN_OFF_SCREEN_ACTION);
                changed = true;
            }
            continue;
        }

        if (!settingsGroup.hasKey(previousActionKey)) {
            continue;
        }

        const int previousAction = settingsGroup.readEntry(previousActionKey, POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION);
        if (handleButtonEventsGroup.readEntry("powerButtonAction", POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION) != previousAction) {
            handleButtonEventsGroup.writeEntry("powerButtonAction", previousAction);
            changed = true;
        }
        settingsGroup.deleteEntry(previousActionKey);
    }

    if (!changed) {
        return;
    }

    powerDevilConfig->sync();
    reloadPowerDevilConfiguration();
}

void BigscreenShellSettings::applyPowerDevilAutomaticScreenOffSetting(bool enabled, int minutes, KConfigGroup &settingsGroup)
{
    KSharedConfig::Ptr powerDevilConfig = KSharedConfig::openConfig(QStringLiteral("powermanagementprofilesrc"));
    bool changed = false;

    for (const QString &profile : POWERDEVIL_PROFILE_GROUPS) {
        KConfigGroup profileGroup{powerDevilConfig, profile};
        KConfigGroup dpmsGroup{&profileGroup, QStringLiteral("DPMSControl")};

        const QString previousIdleTimePresentKey = QStringLiteral("previousDpmsIdleTimePresent_%1").arg(profile);
        const QString previousIdleTimeKey = QStringLiteral("previousDpmsIdleTime_%1").arg(profile);
        const QString previousLockBeforeTurnOffPresentKey = QStringLiteral("previousDpmsLockBeforeTurnOffPresent_%1").arg(profile);
        const QString previousLockBeforeTurnOffKey = QStringLiteral("previousDpmsLockBeforeTurnOff_%1").arg(profile);

        if (enabled) {
            if (!settingsGroup.hasKey(previousIdleTimePresentKey)) {
                const bool hadIdleTime = dpmsGroup.hasKey(QStringLiteral("idleTime"));
                settingsGroup.writeEntry(previousIdleTimePresentKey, hadIdleTime);
                if (hadIdleTime) {
                    settingsGroup.writeEntry(previousIdleTimeKey, dpmsGroup.readEntry("idleTime", 0));
                }

                const bool hadLockBeforeTurnOff = dpmsGroup.hasKey(QStringLiteral("lockBeforeTurnOff"));
                settingsGroup.writeEntry(previousLockBeforeTurnOffPresentKey, hadLockBeforeTurnOff);
                if (hadLockBeforeTurnOff) {
                    settingsGroup.writeEntry(previousLockBeforeTurnOffKey, dpmsGroup.readEntry("lockBeforeTurnOff", false));
                }
            }

            const int desiredIdleTime = minutes * 60;
            if (dpmsGroup.readEntry("idleTime", 0) != desiredIdleTime) {
                dpmsGroup.writeEntry("idleTime", desiredIdleTime);
                changed = true;
            }
            if (dpmsGroup.readEntry("lockBeforeTurnOff", true)) {
                dpmsGroup.writeEntry("lockBeforeTurnOff", false);
                changed = true;
            }
            continue;
        }

        if (!settingsGroup.hasKey(previousIdleTimePresentKey)) {
            continue;
        }

        if (settingsGroup.readEntry(previousIdleTimePresentKey, false)) {
            const int previousIdleTime = settingsGroup.readEntry(previousIdleTimeKey, 0);
            if (dpmsGroup.readEntry("idleTime", 0) != previousIdleTime) {
                dpmsGroup.writeEntry("idleTime", previousIdleTime);
                changed = true;
            }
        } else {
            if (dpmsGroup.hasKey(QStringLiteral("idleTime"))) {
                dpmsGroup.deleteEntry("idleTime");
                changed = true;
            }
        }

        if (settingsGroup.readEntry(previousLockBeforeTurnOffPresentKey, false)) {
            const bool previousLockBeforeTurnOff = settingsGroup.readEntry(previousLockBeforeTurnOffKey, false);
            if (dpmsGroup.readEntry("lockBeforeTurnOff", false) != previousLockBeforeTurnOff) {
                dpmsGroup.writeEntry("lockBeforeTurnOff", previousLockBeforeTurnOff);
                changed = true;
            }
        } else {
            if (dpmsGroup.hasKey(QStringLiteral("lockBeforeTurnOff"))) {
                dpmsGroup.deleteEntry("lockBeforeTurnOff");
                changed = true;
            }
        }

        settingsGroup.deleteEntry(previousIdleTimePresentKey);
        settingsGroup.deleteEntry(previousIdleTimeKey);
        settingsGroup.deleteEntry(previousLockBeforeTurnOffPresentKey);
        settingsGroup.deleteEntry(previousLockBeforeTurnOffKey);
    }

    if (!changed) {
        return;
    }

    powerDevilConfig->sync();
    reloadPowerDevilConfiguration();
}
