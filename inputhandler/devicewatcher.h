// SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>

/**
 * Watches input device nodes with inotify, then checks /proc to see whether
 * another user-facing process has the device open.
 */
class DeviceWatcher : public QObject
{
    Q_OBJECT

public:
    explicit DeviceWatcher(QObject *parent = nullptr);
    ~DeviceWatcher() override;

    void addDevicePath(const QString &devicePath);
    void removeDevicePath(const QString &devicePath);

    bool hasOtherProcesses() const
    {
        return m_othersUsingDevice;
    }

Q_SIGNALS:
    void otherProcessesChanged(bool othersUsingDevice);

private:
    void onInotifyEvent();
    void checkDeviceAccess();
    void updateRecheckTimer();
    bool isDeviceOpenByOthers() const;

    int m_inotifyFd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_recheckTimer = nullptr;
    QHash<int, QString> m_watchDescriptors;
    QHash<QString, int> m_devicePathRefs;
    QSet<QString> m_devicePaths;
    bool m_othersUsingDevice = false;
    qint64 m_myPid;

    static constexpr int IDLE_RECHECK_INTERVAL = 10000;
    static constexpr int ACTIVE_RECHECK_INTERVAL = 2000;
    static const QSet<QString> s_ignoredProcesses;
};
