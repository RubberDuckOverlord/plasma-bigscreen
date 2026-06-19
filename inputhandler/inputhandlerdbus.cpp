/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "inputhandlerdbus.h"
#include "controllermanager.h"
#include "inputhandleradaptor.h"
#include "sdlcontroller.h"

#ifdef HAS_LIBCEC
#include "libcec/ceccontroller.h"
#endif

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QDebug>

InputHandlerDBus::InputHandlerDBus(QObject *parent)
    : QObject(parent)
{
    new InputhandlerAdaptor(this);

    QDBusConnection sessionBus = QDBusConnection::sessionBus();

    if (!sessionBus.registerService(QStringLiteral("org.kde.plasma.bigscreen.inputhandler"))) {
        qWarning() << "Failed to register DBus service org.kde.plasma.bigscreen.inputhandler:" << sessionBus.lastError().message();
    }

    if (!sessionBus.registerObject(QStringLiteral("/InputHandler"), this)) {
        qWarning() << "Failed to register DBus object /InputHandler:" << sessionBus.lastError().message();
    }

    m_bigscreenInputFocusWatcher = new QDBusServiceWatcher(this);
    m_bigscreenInputFocusWatcher->setConnection(sessionBus);
    m_bigscreenInputFocusWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_bigscreenInputFocusWatcher,
            &QDBusServiceWatcher::serviceUnregistered,
            this,
            &InputHandlerDBus::releaseBigscreenInputFocusForCaller);

    connect(&ControllerManager::instance(), &ControllerManager::homeActionRequested, this, &InputHandlerDBus::homeActionRequested);
    connect(&ControllerManager::instance(), &ControllerManager::displayOffActionRequested, this, &InputHandlerDBus::displayOffActionRequested);
    connect(&ControllerManager::instance(), &ControllerManager::enabledChanged, this, &InputHandlerDBus::enabledChanged);
    connect(&ControllerManager::instance(), &ControllerManager::gameControllerEnabledChanged, this, &InputHandlerDBus::gameControllerEnabledChanged);
    connect(&ControllerManager::instance(), &ControllerManager::cecEnabledChanged, this, &InputHandlerDBus::cecEnabledChanged);
    connect(&ControllerManager::instance(), &ControllerManager::connectedControllersChanged, this, &InputHandlerDBus::connectedControllersChanged);

    qInfo() << "InputHandlerDBus registered on session bus";
}

InputHandlerDBus::~InputHandlerDBus()
{
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    sessionBus.unregisterObject(QStringLiteral("/InputHandler"));
    sessionBus.unregisterService(QStringLiteral("org.kde.plasma.bigscreen.inputhandler"));
}

void InputHandlerDBus::setSdlController(SdlController *controller)
{
    m_sdlController = controller;

    if (m_sdlController) {
        connect(m_sdlController, &SdlController::controllerAdded, this, &InputHandlerDBus::sdlControllerAdded);
        connect(m_sdlController, &SdlController::controllerRemoved, this, &InputHandlerDBus::sdlControllerRemoved);
        connect(m_sdlController, &SdlController::isSuppressInputChanged, this, &InputHandlerDBus::inputSuppressedChanged);
        connect(m_sdlController, &SdlController::autoSuppressInputChanged, this, &InputHandlerDBus::autoSuppressInputChanged);
    }
}

#ifdef HAS_LIBCEC
void InputHandlerDBus::setCecController(CECController *controller)
{
    m_cecController = controller;

    if (m_cecController) {
        connect(m_cecController, &CECController::controllerAdded, this, &InputHandlerDBus::cecControllerAdded);
        connect(m_cecController, &CECController::controllerRemoved, this, &InputHandlerDBus::cecControllerRemoved);
    }
}
#endif

bool InputHandlerDBus::isSdlControllerConnected() const
{
    if (!m_sdlController) {
        return false;
    }
    return m_sdlController->hasConnectedControllers();
}

bool InputHandlerDBus::isCecControllerConnected() const
{
#ifdef HAS_LIBCEC
    if (!m_cecController) {
        return false;
    }
    return m_cecController->hasConnectedAdapters();
#else
    return false;
#endif
}

bool InputHandlerDBus::isInputSuppressed() const
{
    if (!m_sdlController) {
        return false;
    }
    return m_sdlController->isSuppressInput();
}

bool InputHandlerDBus::isInputManuallySuppressed() const
{
    if (!m_sdlController) {
        return false;
    }
    return m_sdlController->isManualSuppressInput();
}

bool InputHandlerDBus::autoSuppressInput() const
{
    if (!m_sdlController) {
        return true;
    }
    return m_sdlController->autoSuppressInput();
}

void InputHandlerDBus::setAutoSuppressInput(bool enabled)
{
    if (!m_sdlController) {
        return;
    }
    m_sdlController->setAutoSuppressInput(enabled);
}

bool InputHandlerDBus::isEnabled() const
{
    return ControllerManager::instance().enabled();
}

void InputHandlerDBus::setEnabled(bool enabled)
{
    ControllerManager::instance().setEnabled(enabled);
}

bool InputHandlerDBus::isGameControllerEnabled() const
{
    return ControllerManager::instance().gameControllerEnabled();
}

void InputHandlerDBus::setGameControllerEnabled(bool enabled)
{
    ControllerManager::instance().setGameControllerEnabled(enabled);
}

bool InputHandlerDBus::isCecEnabled() const
{
    return ControllerManager::instance().cecEnabled();
}

void InputHandlerDBus::setCecEnabled(bool enabled)
{
    ControllerManager::instance().setCecEnabled(enabled);
}

QVariantList InputHandlerDBus::connectedControllers() const
{
    return ControllerManager::instance().connectedControllers();
}

void InputHandlerDBus::setControllerEnabled(const QString &uniqueIdentifier, bool enabled)
{
    ControllerManager::instance().setControllerEnabled(uniqueIdentifier, enabled);
}

void InputHandlerDBus::setStartButtonEnabledWhenSuppressed(const QString &uniqueIdentifier, bool enabled)
{
    ControllerManager::instance().setStartButtonEnabledWhenSuppressed(uniqueIdentifier, enabled);
}

void InputHandlerDBus::prepareForDisplayOffWake()
{
    ControllerManager::instance().prepareForDisplayOffWake();
}

void InputHandlerDBus::requestBigscreenInputFocus(const QString &source)
{
    if (!m_sdlController || source.isEmpty()) {
        return;
    }

    const QString caller = callerService();
    QSet<QString> &sources = m_bigscreenInputFocusSourcesByCaller[caller];
    if (sources.contains(source)) {
        return;
    }

    if (sources.isEmpty() && m_bigscreenInputFocusWatcher && caller.startsWith(QLatin1Char(':'))) {
        m_bigscreenInputFocusWatcher->addWatchedService(caller);
    }
    sources.insert(source);

    m_sdlController->requestBigscreenInputFocus(source);
}

void InputHandlerDBus::releaseBigscreenInputFocus(const QString &source)
{
    if (!m_sdlController || source.isEmpty()) {
        return;
    }

    const QString caller = callerService();
    auto it = m_bigscreenInputFocusSourcesByCaller.find(caller);
    if (it == m_bigscreenInputFocusSourcesByCaller.end() || !it.value().remove(source)) {
        return;
    }

    if (it.value().isEmpty()) {
        m_bigscreenInputFocusSourcesByCaller.erase(it);
        if (m_bigscreenInputFocusWatcher && caller.startsWith(QLatin1Char(':'))) {
            m_bigscreenInputFocusWatcher->removeWatchedService(caller);
        }
    }

    if (sourceOwnedByOtherCallers(source, caller)) {
        return;
    }

    m_sdlController->releaseBigscreenInputFocus(source);
}

QString InputHandlerDBus::callerService() const
{
    if (calledFromDBus()) {
        return message().service();
    }
    return QStringLiteral("local");
}

bool InputHandlerDBus::sourceOwnedByOtherCallers(const QString &source, const QString &caller) const
{
    for (auto it = m_bigscreenInputFocusSourcesByCaller.constBegin(); it != m_bigscreenInputFocusSourcesByCaller.constEnd(); ++it) {
        if (it.key() != caller && it.value().contains(source)) {
            return true;
        }
    }
    return false;
}

void InputHandlerDBus::releaseBigscreenInputFocusForCaller(const QString &caller)
{
    if (m_bigscreenInputFocusWatcher && caller.startsWith(QLatin1Char(':'))) {
        m_bigscreenInputFocusWatcher->removeWatchedService(caller);
    }

    if (!m_sdlController) {
        m_bigscreenInputFocusSourcesByCaller.remove(caller);
        return;
    }

    const QSet<QString> sources = m_bigscreenInputFocusSourcesByCaller.take(caller);
    for (const QString &source : sources) {
        if (!sourceOwnedByOtherCallers(source, caller)) {
            m_sdlController->releaseBigscreenInputFocus(source);
        }
    }
}

void InputHandlerDBus::setInputSuppressed(bool suppress)
{
    if (!m_sdlController) {
        return;
    }
    m_sdlController->setSuppressInput(suppress);
}

#include "moc_inputhandlerdbus.cpp"
