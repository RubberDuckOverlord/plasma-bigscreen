// SPDX-FileCopyrightText: 2025 User8395 <therealuser8395@proton.me>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <BluezQt/Device>
#include <KQuickConfigModule>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariantList>

class OrgKdePlasmaBigscreenInputhandlerInterface;

class Bluetooth : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(bool fromDatabase READ isFromDatabase)
    Q_PROPERTY(bool inputHandlerAvailable READ inputHandlerAvailable NOTIFY inputHandlerAvailableChanged)
    Q_PROPERTY(QVariantList connectedInputControllers READ connectedInputControllers NOTIFY connectedInputControllersChanged)
public:
    Bluetooth(QObject *parent, const KPluginMetaData &data);
    ~Bluetooth() override;

    Q_INVOKABLE void setPin(const QString &pin);
    Q_INVOKABLE bool isFromDatabase();
    Q_INVOKABLE QString getPin(BluezQt::DevicePtr device);
    Q_INVOKABLE void refreshInputControllers();
    Q_INVOKABLE bool hasConnectedInputControllerForDevice(BluezQt::DevicePtr device) const;
    Q_INVOKABLE bool hasAnyConnectedGameController() const;

    bool inputHandlerAvailable() const;
    QVariantList connectedInputControllers() const;

Q_SIGNALS:
    void inputHandlerAvailableChanged();
    void connectedInputControllersChanged();

private Q_SLOTS:
    void connectToInputHandler();
    void disconnectFromInputHandler();
    void scheduleInputControllerUpdate();
    void updateInputControllers();

private:
    QString controllerFamilyForDevice(BluezQt::DevicePtr device) const;
    bool controllerMatchesDevice(const QVariantMap &controller, BluezQt::DevicePtr device) const;

    bool m_fromDatabase = false;
    QString m_pin;
    bool m_inputHandlerAvailable = false;
    bool m_inputControllerUpdateScheduled = false;
    QVariantList m_connectedInputControllers;
    QDBusServiceWatcher *m_inputHandlerWatcher = nullptr;
    OrgKdePlasmaBigscreenInputhandlerInterface *m_inputHandlerInterface = nullptr;
};
