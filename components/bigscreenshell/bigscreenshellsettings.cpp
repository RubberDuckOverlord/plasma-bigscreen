// SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bigscreenshellsettings.h"

#include <KIO/CommandLauncherJob>
#include <KNotificationJobUiDelegate>
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
        }
    });

    if (powerButtonTurnsOffScreen()) {
        KConfigGroup group{m_config, GENERAL_CONFIG_GROUP};
        applyPowerDevilPowerButtonSetting(true, group);
        m_config->sync();
    }
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

    // Update environment settings
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

            handleButtonEventsGroup.writeEntry("powerButtonAction", POWERDEVIL_TURN_OFF_SCREEN_ACTION);
            changed = true;
            continue;
        }

        if (!settingsGroup.hasKey(previousActionKey)) {
            continue;
        }

        const int previousAction = settingsGroup.readEntry(previousActionKey, POWERDEVIL_PROMPT_LOGOUT_DIALOG_ACTION);
        handleButtonEventsGroup.writeEntry("powerButtonAction", previousAction);
        settingsGroup.deleteEntry(previousActionKey);
        changed = true;
    }

    if (!changed) {
        return;
    }

    powerDevilConfig->sync();
    reloadPowerDevilConfiguration();
}
