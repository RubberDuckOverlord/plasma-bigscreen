/*
    SPDX-FileCopyrightText: 2014-2015 David Rosca <nowrep@gmail.com>
    SPDX-FileCopyrightText: 2025 User8935 <therealuser8395@proton.me>
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "devicesproxymodel.h"

#include <BluezQt/Adapter>
#include <BluezQt/Device>
#include <QStringList>

static const QString s_humanInterfaceDeviceServiceUuid = QStringLiteral("00001812-0000-1000-8000-00805f9b34fb");

DevicesProxyModel::DevicesProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0, Qt::DescendingOrder);

    connect(this, &QAbstractItemModel::modelReset, this, &DevicesProxyModel::rowCountChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &DevicesProxyModel::rowCountChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &DevicesProxyModel::rowCountChanged);
}

bool DevicesProxyModel::hideBlockedDevices() const
{
    return m_hideBlockedDevices;
}

void DevicesProxyModel::setHideBlockedDevices(bool shouldHide)
{
    if (m_hideBlockedDevices != shouldHide) {
        m_hideBlockedDevices = shouldHide;

        invalidateFilter();

        Q_EMIT hideBlockedDevicesChanged();
        Q_EMIT rowCountChanged();
    }
}

bool DevicesProxyModel::inputDevicesOnly() const
{
    return m_inputDevicesOnly;
}

void DevicesProxyModel::setInputDevicesOnly(bool inputDevicesOnly)
{
    if (m_inputDevicesOnly == inputDevicesOnly) {
        return;
    }

    m_inputDevicesOnly = inputDevicesOnly;
    invalidateFilter();
    Q_EMIT inputDevicesOnlyChanged();
    Q_EMIT rowCountChanged();
}

bool DevicesProxyModel::pairedOnly() const
{
    return m_pairedOnly;
}

void DevicesProxyModel::setPairedOnly(bool pairedOnly)
{
    if (m_pairedOnly != pairedOnly) {
        m_pairedOnly = pairedOnly;
        invalidateFilter();
        Q_EMIT rowCountChanged();
    }
}

QHash<int, QByteArray> DevicesProxyModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles[SectionRole] = QByteArrayLiteral("Section");
    roles[DeviceFullNameRole] = QByteArrayLiteral("DeviceFullName");
    return roles;
}

QVariant DevicesProxyModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case SectionRole:
        if (index.data(BluezQt::DevicesModel::PairedRole).toBool()) {
            return QStringLiteral("Paired");
        }
        return QStringLiteral("Available");

    case DeviceFullNameRole:
        if (duplicateIndexAddress(index)) {
            const QString &name = QSortFilterProxyModel::data(index, BluezQt::DevicesModel::NameRole).toString();
            const QString &ubi = QSortFilterProxyModel::data(index, BluezQt::DevicesModel::UbiRole).toString();
            const QString &hci = adapterHciString(ubi);

            if (!hci.isEmpty()) {
                return QStringLiteral("%1 - %2").arg(name, hci);
            }
        }
        return QSortFilterProxyModel::data(index, BluezQt::DevicesModel::NameRole);

    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

bool DevicesProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    bool leftBlocked = left.data(BluezQt::DevicesModel::BlockedRole).toBool();
    bool rightBlocked = right.data(BluezQt::DevicesModel::BlockedRole).toBool();

    // Blocked are checked first, but they go last.
    if (!leftBlocked && rightBlocked) {
        return false;
    } else if (leftBlocked && !rightBlocked) {
        return true;
    }

    bool leftConnected = left.data(BluezQt::DevicesModel::ConnectedRole).toBool();
    bool rightConnected = right.data(BluezQt::DevicesModel::ConnectedRole).toBool();

    // Connected go above disconnected but available (not blocked)
    if (!leftConnected && rightConnected) {
        return true;
    } else if (leftConnected && !rightConnected) {
        return false;
    }

    const QString &leftName = left.data(BluezQt::DevicesModel::NameRole).toString();
    const QString &rightName = right.data(BluezQt::DevicesModel::NameRole).toString();

    return QString::localeAwareCompare(leftName, rightName) > 0;
}

bool DevicesProxyModel::duplicateIndexAddress(const QModelIndex &idx) const
{
    const QModelIndexList &list = match(index(0, 0), //
                                        BluezQt::DevicesModel::AddressRole,
                                        idx.data(BluezQt::DevicesModel::AddressRole).toString(),
                                        2,
                                        Qt::MatchExactly);
    return list.size() > 1;
}

bool DevicesProxyModel::isInputDevice(const QModelIndex &idx) const
{
    switch (static_cast<BluezQt::Device::Type>(idx.data(BluezQt::DevicesModel::TypeRole).toInt())) {
    case BluezQt::Device::Keyboard:
    case BluezQt::Device::Mouse:
    case BluezQt::Device::Joypad:
    case BluezQt::Device::Tablet:
    case BluezQt::Device::Peripheral:
        return true;
    default:
        break;
    }

    const QStringList uuids = idx.data(BluezQt::DevicesModel::UuidsRole).toStringList();
    for (const QString &uuid : uuids) {
        if (uuid.compare(s_humanInterfaceDeviceServiceUuid, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return isLikelyControllerName(idx.data(BluezQt::DevicesModel::NameRole).toString());
}

bool DevicesProxyModel::isLikelyControllerName(const QString &name) const
{
    static const QStringList controllerNameFragments = {
        QStringLiteral("controller"),
        QStringLiteral("gamepad"),
        QStringLiteral("xbox"),
        QStringLiteral("dualsense"),
        QStringLiteral("dualshock"),
        QStringLiteral("playstation"),
        QStringLiteral("steam"),
        QStringLiteral("joy-con"),
        QStringLiteral("8bitdo"),
        QStringLiteral("stadia"),
        QStringLiteral("luna"),
    };

    for (const QString &fragment : controllerNameFragments) {
        if (name.contains(fragment, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

bool DevicesProxyModel::isUnresolvedDeviceName(const QModelIndex &idx) const
{
    const QString name = idx.data(BluezQt::DevicesModel::NameRole).toString();
    const QString address = idx.data(BluezQt::DevicesModel::AddressRole).toString();
    return name.isEmpty() || name == address || name == QString(address).replace(QLatin1Char(':'), QLatin1Char('-'));
}

bool DevicesProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    if (!sourceModel()) {
        return false;
    }

    const QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    if (m_hideBlockedDevices && sourceModel()->data(index, BluezQt::DevicesModel::BlockedRole).toBool()) {
        return false;
    }

    const bool paired = sourceModel()->data(index, BluezQt::DevicesModel::PairedRole).toBool();
    if (m_pairedOnly) {
        return paired && (!m_inputDevicesOnly || isInputDevice(index));
    }

    if (paired) {
        return false;
    }

    if (m_inputDevicesOnly) {
        return isInputDevice(index) || isUnresolvedDeviceName(index);
    }

    return true;
}

QString DevicesProxyModel::adapterHciString(const QString &ubi)
{
    int startIndex = ubi.indexOf(QLatin1String("/hci")) + 1;

    if (startIndex < 1) {
        return QString();
    }

    int endIndex = ubi.indexOf(QLatin1Char('/'), startIndex);

    if (endIndex == -1) {
        return ubi.mid(startIndex);
    }
    return ubi.mid(startIndex, endIndex - startIndex);
}

#include "moc_devicesproxymodel.cpp"
