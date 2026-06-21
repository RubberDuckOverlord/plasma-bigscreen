// SPDX-FileCopyrightText: 2025 User8395 <therealuser8395@proton.me>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bluetooth.h"
#include "devicesproxymodel.h"
#include "inputhandlerinterface.h"

#include <KPluginFactory>
#include <QAbstractItemModel>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QFile>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QXmlStreamReader>
#include <qqml.h>

#include <BluezQt/Adapter>
#include <BluezQt/PendingCall>

K_PLUGIN_CLASS_WITH_JSON(Bluetooth, "kcm_mediacenter_bluetooth.json")

using namespace Qt::StringLiterals;

static const QString s_inputHandlerServiceName = u"org.kde.plasma.bigscreen.inputhandler"_s;
static const QString s_inputHandlerObjectPath = u"/InputHandler"_s;

static QVariantMap controllerMapFromVariant(QVariant controller)
{
    if (controller.canConvert<QDBusVariant>()) {
        controller = controller.value<QDBusVariant>().variant();
    }

    if (controller.canConvert<QVariantMap>()) {
        return controller.toMap();
    }

    if (controller.canConvert<QDBusArgument>()) {
        QVariantMap controllerMap;
        controller.value<QDBusArgument>() >> controllerMap;
        return controllerMap;
    }

    return {};
}

static QVariantList controllerListFromDBusReply(const QVariantList &controllers)
{
    QVariantList controllerList;
    controllerList.reserve(controllers.size());

    for (QVariant controller : controllers) {
        QVariantMap controllerMap = controllerMapFromVariant(controller);
        if (!controllerMap.isEmpty()) {
            controllerList.append(controllerMap);
        }
    }

    return controllerList;
}

Bluetooth::Bluetooth(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_inputHandlerWatcher(new QDBusServiceWatcher(s_inputHandlerServiceName,
                                                    QDBusConnection::sessionBus(),
                                                    QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                                    this))
{
    setButtons(Apply);

    qmlRegisterType<DevicesProxyModel>("org.kde.plasma.bigscreen.bluetooth", 1, 0, "DevicesProxyModel");

    connect(m_inputHandlerWatcher, &QDBusServiceWatcher::serviceRegistered, this, &Bluetooth::connectToInputHandler);
    connect(m_inputHandlerWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &Bluetooth::disconnectFromInputHandler);

    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(s_inputHandlerServiceName)) {
        connectToInputHandler();
    }
}

Bluetooth::~Bluetooth()
{
    disconnectFromInputHandler();
}

void Bluetooth::setPin(const QString &pin)
{
    m_pin = pin;
    m_fromDatabase = false;
}

bool Bluetooth::isFromDatabase()
{
    return m_fromDatabase;
}

QString Bluetooth::getPin(BluezQt::DevicePtr device)
{
    m_fromDatabase = false;
    m_pin = QString::number(QRandomGenerator::global()->bounded(RAND_MAX));
    m_pin = m_pin.left(6);

    const QString &xmlPath = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("pin-code-database.xml"));

    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return m_pin;
    }

    QXmlStreamReader xml(&file);

    QString deviceType = BluezQt::Device::typeToString(device->type());
    if (deviceType == QLatin1String("audiovideo")) {
        deviceType = QStringLiteral("audio");
    }

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.name() != QLatin1String("device")) {
            continue;
        }
        QXmlStreamAttributes attr = xml.attributes();

        if (attr.count() == 0) {
            continue;
        }

        if (attr.hasAttribute(QLatin1String("type")) && attr.value(QLatin1String("type")) != QLatin1String("any")) {
            if (deviceType != attr.value(QLatin1String("type")).toString()) {
                continue;
            }
        }

        if (attr.hasAttribute(QLatin1String("oui"))) {
            if (!device->address().startsWith(attr.value(QLatin1String("oui")).toString())) {
                continue;
            }
        }

        if (attr.hasAttribute(QLatin1String("name"))) {
            if (device->name() != attr.value(QLatin1String("name")).toString()) {
                continue;
            }
        }

        m_pin = attr.value(QLatin1String("pin")).toString();
        m_fromDatabase = true;
        if (m_pin.startsWith(QLatin1String("max:"))) {
            m_fromDatabase = false;
            int num = QStringView(m_pin).right(m_pin.length() - 4).toInt();
            m_pin = QString::number(QRandomGenerator::global()->bounded(RAND_MAX)).left(num);
        }

        return m_pin;
    }

    return m_pin;
}

bool Bluetooth::inputHandlerAvailable() const
{
    return m_inputHandlerAvailable;
}

QVariantList Bluetooth::connectedInputControllers() const
{
    return m_connectedInputControllers;
}

int Bluetooth::connectedGameControllerCount() const
{
    int count = 0;
    for (const QVariant &controller : m_connectedInputControllers) {
        const QVariantMap controllerMap = controller.toMap();
        if (controllerMap.value(QStringLiteral("type")).toString() == QStringLiteral("gameController")) {
            count++;
        }
    }
    return count;
}

void Bluetooth::refreshInputControllers()
{
    updateInputControllers();
}

bool Bluetooth::hasAnyConnectedGameController() const
{
    return connectedGameControllerCount() > 0;
}

bool Bluetooth::hasConnectedInputControllerForDevice(BluezQt::DevicePtr device) const
{
    if (!device || !device->isConnected()) {
        return false;
    }

    int connectedGameControllerCount = 0;
    for (const QVariant &controller : m_connectedInputControllers) {
        const QVariantMap controllerMap = controller.toMap();
        if (controllerMap.value(QStringLiteral("type")).toString() != QStringLiteral("gameController")) {
            continue;
        }

        connectedGameControllerCount++;
        if (controllerMatchesDevice(controllerMap, device)) {
            return true;
        }
    }

    return connectedGameControllerCount == 1;
}

void Bluetooth::connectToInputHandler()
{
    if (m_inputHandlerInterface) {
        return;
    }

    m_inputHandlerInterface = new OrgKdePlasmaBigscreenInputhandlerInterface(s_inputHandlerServiceName, s_inputHandlerObjectPath, QDBusConnection::sessionBus(), this);
    if (!m_inputHandlerInterface->isValid()) {
        delete m_inputHandlerInterface;
        m_inputHandlerInterface = nullptr;
        return;
    }

    connect(m_inputHandlerInterface, &OrgKdePlasmaBigscreenInputhandlerInterface::connectedControllersChanged, this, &Bluetooth::scheduleInputControllerUpdate);
    connect(m_inputHandlerInterface, &OrgKdePlasmaBigscreenInputhandlerInterface::gameControllerEnabledChanged, this, &Bluetooth::scheduleInputControllerUpdate);

    m_inputHandlerAvailable = true;
    Q_EMIT inputHandlerAvailableChanged();
    updateInputControllers();
}

void Bluetooth::disconnectFromInputHandler()
{
    m_inputControllerReplyPending = false;
    m_inputControllerRequestSerial++;

    if (m_inputHandlerInterface) {
        delete m_inputHandlerInterface;
        m_inputHandlerInterface = nullptr;
    }

    if (m_inputHandlerAvailable) {
        m_inputHandlerAvailable = false;
        Q_EMIT inputHandlerAvailableChanged();
    }

    if (!m_connectedInputControllers.isEmpty()) {
        m_connectedInputControllers.clear();
        Q_EMIT connectedInputControllersChanged();
    }
}

void Bluetooth::scheduleInputControllerUpdate()
{
    if (m_inputControllerUpdateScheduled) {
        return;
    }

    m_inputControllerUpdateScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_inputControllerUpdateScheduled = false;
        updateInputControllers();
    });
}

void Bluetooth::updateInputControllers()
{
    m_inputControllerUpdateScheduled = false;

    if (!m_inputHandlerInterface || m_inputControllerReplyPending) {
        return;
    }

    m_inputControllerReplyPending = true;
    const int requestSerial = ++m_inputControllerRequestSerial;
    auto *watcher = new QDBusPendingCallWatcher(m_inputHandlerInterface->connectedControllers(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, requestSerial](QDBusPendingCallWatcher *watcher) {
        m_inputControllerReplyPending = false;

        if (!m_inputHandlerInterface || requestSerial != m_inputControllerRequestSerial) {
            watcher->deleteLater();
            return;
        }

        QDBusPendingReply<QVariantList> reply = *watcher;
        watcher->deleteLater();

        QVariantList connectedControllers;
        if (!reply.isError()) {
            connectedControllers = controllerListFromDBusReply(reply.value());
        } else {
            qWarning() << "Failed to fetch connected input controllers:" << reply.error().message();
        }

        if (m_connectedInputControllers != connectedControllers) {
            m_connectedInputControllers = connectedControllers;
            Q_EMIT connectedInputControllersChanged();
        }
    });
}

QString Bluetooth::controllerFamilyForDevice(BluezQt::DevicePtr device) const
{
    if (!device) {
        return {};
    }

    const QString name = device->name().toLower();
    if (name.contains(QLatin1String("xbox"))) {
        return QStringLiteral("xbox");
    }
    if (name.contains(QLatin1String("dualsense")) || name.contains(QLatin1String("dualshock")) || name.contains(QLatin1String("playstation"))
        || name.contains(QLatin1String("wireless controller"))) {
        return QStringLiteral("playstation");
    }
    if (name.contains(QLatin1String("steam"))) {
        return QStringLiteral("steam");
    }

    return {};
}

bool Bluetooth::controllerMatchesDevice(const QVariantMap &controller, BluezQt::DevicePtr device) const
{
    if (!device || controller.value(QStringLiteral("type")).toString() != QStringLiteral("gameController")) {
        return false;
    }

    const QString deviceFamily = controllerFamilyForDevice(device);
    const QString controllerFamily = controller.value(QStringLiteral("controllerFamily")).toString();
    if (!deviceFamily.isEmpty() && controllerFamily == deviceFamily) {
        return true;
    }

    const QString deviceName = device->name().toLower();
    const QString controllerName = controller.value(QStringLiteral("name")).toString().toLower();
    return !deviceName.isEmpty() && !controllerName.isEmpty() && (controllerName.contains(deviceName) || deviceName.contains(controllerName));
}

#include "bluetooth.moc"
