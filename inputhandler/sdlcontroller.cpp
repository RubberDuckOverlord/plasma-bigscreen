/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "sdlcontroller.h"
#include "controllermanager.h"
#include "inputhandlersettings.h"

#include <QByteArray>
#include <QStringList>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <utility>
#include <unistd.h>

static bool s_sdlInitialized = false;

static QString guidString(SDL_GUID guid)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(guid.data), sizeof(guid.data)).toHex());
}

static QString gamepadPath(SDL_Gamepad *gamepad)
{
    auto path = SDL_GetGamepadPath(gamepad);
    return path ? QString::fromUtf8(path) : QString();
}

static QString gamepadName(SDL_Gamepad *gamepad)
{
    auto name = SDL_GetGamepadName(gamepad);
    QString deviceName = name ? QString::fromUtf8(name) : QString();
    return deviceName.isEmpty() ? QStringLiteral("Game Controller") : deviceName;
}

static QString joystickPath(SDL_Joystick *joystick)
{
    auto path = SDL_GetJoystickPath(joystick);
    return path ? QString::fromUtf8(path) : QString();
}

static QString joystickName(SDL_Joystick *joystick)
{
    auto name = SDL_GetJoystickName(joystick);
    QString deviceName = name ? QString::fromUtf8(name) : QString();
    return deviceName.isEmpty() ? QStringLiteral("Joystick") : deviceName;
}

static bool nameMatchesAny(const QString &name, const QStringList &values)
{
    for (const QString &value : values) {
        if (name.contains(value, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

static SdlControllerFamily familyFromName(const QString &deviceName)
{
    if (nameMatchesAny(deviceName, {QStringLiteral("steam")})) {
        return SdlControllerFamily::Steam;
    }
    if (nameMatchesAny(deviceName, {QStringLiteral("xbox"), QStringLiteral("xinput")})) {
        return SdlControllerFamily::Xbox;
    }
    if (nameMatchesAny(deviceName,
                       {
                           QStringLiteral("playstation"),
                           QStringLiteral("dualshock"),
                           QStringLiteral("dualsense"),
                           QStringLiteral("sony"),
                           QStringLiteral("wireless controller"),
                       })) {
        return SdlControllerFamily::PlayStation;
    }
    return SdlControllerFamily::Generic;
}

static SdlControllerFamily gamepadFamily(SDL_Gamepad *gamepad)
{
    const QString deviceName = gamepadName(gamepad);
    SdlControllerFamily family = familyFromName(deviceName);
    if (family != SdlControllerFamily::Generic) {
        return family;
    }

    switch (SDL_GetGamepadType(gamepad)) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return SdlControllerFamily::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return SdlControllerFamily::PlayStation;
    default:
        return SdlControllerFamily::Generic;
    }
}

static SdlControllerFamily joystickFamily(SDL_Joystick *joystick)
{
    return familyFromName(joystickName(joystick));
}

static QString controllerFamilyId(SdlControllerFamily family)
{
    switch (family) {
    case SdlControllerFamily::Xbox:
        return QStringLiteral("xbox");
    case SdlControllerFamily::PlayStation:
        return QStringLiteral("playstation");
    case SdlControllerFamily::Steam:
        return QStringLiteral("steam");
    case SdlControllerFamily::Generic:
        return QStringLiteral("generic");
    }
    return QStringLiteral("generic");
}

static QString controllerFamilyName(SdlControllerFamily family)
{
    switch (family) {
    case SdlControllerFamily::Xbox:
        return QStringLiteral("Xbox");
    case SdlControllerFamily::PlayStation:
        return QStringLiteral("PlayStation");
    case SdlControllerFamily::Steam:
        return QStringLiteral("Steam Controller");
    case SdlControllerFamily::Generic:
        return QStringLiteral("Generic Controller");
    }
    return QStringLiteral("Generic Controller");
}

static QString evdevUniqueIdentifier(const QString &devicePath)
{
    if (devicePath.isEmpty()) {
        return {};
    }

    int fd = open(qPrintable(devicePath), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }

    QByteArray uniqueIdentifier(256, '\0');
    int result = ioctl(fd, EVIOCGUNIQ(uniqueIdentifier.size()), uniqueIdentifier.data());
    close(fd);
    if (result < 0) {
        return {};
    }

    uniqueIdentifier.truncate(qstrnlen(uniqueIdentifier.data(), uniqueIdentifier.size()));
    return QString::fromUtf8(uniqueIdentifier);
}

static QString gamepadUniqueIdentifier(SDL_Gamepad *gamepad, SDL_JoystickID instanceId)
{
    auto serial = SDL_GetGamepadSerial(gamepad);
    QString serialNumber = serial ? QString::fromUtf8(serial) : QString();
    if (!serialNumber.isEmpty()) {
        return QStringLiteral("serial:%1").arg(serialNumber);
    }

    QString uniqueIdentifier = evdevUniqueIdentifier(gamepadPath(gamepad));
    if (!uniqueIdentifier.isEmpty()) {
        return QStringLiteral("evdev:%1").arg(uniqueIdentifier);
    }

    return QStringLiteral("guid:%1").arg(guidString(SDL_GetGamepadGUIDForID(instanceId)));
}

static QString joystickUniqueIdentifier(SDL_Joystick *joystick, SDL_JoystickID instanceId)
{
    auto serial = SDL_GetJoystickSerial(joystick);
    QString serialNumber = serial ? QString::fromUtf8(serial) : QString();
    if (!serialNumber.isEmpty()) {
        return QStringLiteral("serial:%1").arg(serialNumber);
    }

    QString uniqueIdentifier = evdevUniqueIdentifier(joystickPath(joystick));
    if (!uniqueIdentifier.isEmpty()) {
        return QStringLiteral("evdev:%1").arg(uniqueIdentifier);
    }

    return QStringLiteral("guid:%1").arg(guidString(SDL_GetJoystickGUIDForID(instanceId)));
}

static QMap<SDL_GamepadButton, QList<InputAction>> gamepadButtonMappings(SdlControllerFamily family)
{
    QMap<SDL_GamepadButton, QList<InputAction>> mappings = {
        {SDL_GAMEPAD_BUTTON_GUIDE, {InputAction::Home}},
        {SDL_GAMEPAD_BUTTON_START, {InputAction::SystemMenu}},
        {SDL_GAMEPAD_BUTTON_SOUTH, {InputAction::Select}},
        {SDL_GAMEPAD_BUTTON_EAST, {InputAction::Back}},
        {SDL_GAMEPAD_BUTTON_WEST, {InputAction::Menu}},
        {SDL_GAMEPAD_BUTTON_NORTH, {InputAction::None}},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, {InputAction::Previous}},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, {InputAction::Next}},
        {SDL_GAMEPAD_BUTTON_BACK, {InputAction::BrowserBack}},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, {InputAction::NavigateUp}},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, {InputAction::NavigateDown}},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, {InputAction::NavigateLeft}},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, {InputAction::NavigateRight}},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, {InputAction::None}},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, {InputAction::None}},
    };

    switch (family) {
    case SdlControllerFamily::Xbox:
        // Xbox Series share button: useful as an explicit contextual menu action in Bigscreen.
        mappings.insert(SDL_GAMEPAD_BUTTON_MISC1, {InputAction::Menu});
        break;
    case SdlControllerFamily::PlayStation:
        // DualShock/DualSense touchpad click is a natural menu/control surface affordance.
        mappings.insert(SDL_GAMEPAD_BUTTON_TOUCHPAD, {InputAction::Menu});
        break;
    case SdlControllerFamily::Steam:
        mappings.insert(SDL_GAMEPAD_BUTTON_MISC1, {InputAction::SystemMenu}); // QAM/Steam menu
        mappings.insert(SDL_GAMEPAD_BUTTON_TOUCHPAD, {InputAction::Select}); // Left/primary trackpad click
        mappings.insert(SDL_GAMEPAD_BUTTON_MISC2, {InputAction::Menu}); // Right/secondary trackpad click
        mappings.insert(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, {InputAction::Next});
        mappings.insert(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, {InputAction::Previous});
        mappings.insert(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, {InputAction::BrowserForward});
        mappings.insert(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, {InputAction::BrowserBack});
        break;
    case SdlControllerFamily::Generic:
        break;
    }

    return mappings;
}

static QMap<int, QList<InputAction>> joystickButtonMappings()
{
    return {
        {0, {InputAction::Select}},
        {1, {InputAction::Back}},
        {2, {InputAction::Menu}},
        {3, {InputAction::None}},
        {4, {InputAction::Previous}},
        {5, {InputAction::Next}},
        {6, {InputAction::BrowserBack}},
        {7, {InputAction::SystemMenu}},
        {8, {InputAction::Home}},
    };
}

template<typename Key>
static QList<InputAction> actionsFromMappings(const QMap<Key, QList<InputAction>> &mappings)
{
    QList<InputAction> actions;
    for (const QList<InputAction> &mappedActions : mappings) {
        actions.append(mappedActions);
    }
    return actions;
}

SdlController::SdlController()
    : QObject()
{
    m_autoSuppressInput = InputHandlerSettings::self()->autoSuppressInput();

    // Initialize SDL3 gamepad and joystick subsystems. The gamepad API gives
    // normalized layouts; the joystick API catches devices without mappings.
    if (!s_sdlInitialized) {
        // Prevent SDL from installing signal handlers that would block SIGINT (Ctrl+C)
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

        if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
            qWarning() << "Failed to initialize SDL controller subsystems:" << SDL_GetError();
            return;
        }
        s_sdlInitialized = true;
        qInfo() << "SDL3 controller subsystems initialized";
    }

    // Watch for other processes listening to the controller
    m_deviceWatcher = new DeviceWatcher(this);
    connect(m_deviceWatcher, &DeviceWatcher::otherProcessesChanged, this, [this](bool othersUsing) {
        qInfo() << "Other processes using device:" << othersUsing;

        updateAutomaticSuppression();
    });

    m_autoUnsuppressTimer = new QTimer(this);
    m_autoUnsuppressTimer->setSingleShot(true);
    m_autoUnsuppressTimer->setInterval(AUTO_UNSUPPRESS_DELAY);
    connect(m_autoUnsuppressTimer, &QTimer::timeout, this, [this]() {
        if (m_manualSuppressInput || !m_deviceWatcher) {
            return;
        }

        if (!m_autoSuppressInput || !m_deviceWatcher->hasOtherProcesses()) {
            setAutomaticSuppression(false);
        }
    });

    // Set up polling timer
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &SdlController::poll);
    m_pollTimer->start(LONG_POLL_INTERVAL);

    // Check for already connected gamepads
    int numGamepads = 0;
    SDL_JoystickID *gamepadIds = SDL_GetGamepads(&numGamepads);
    if (gamepadIds) {
        qInfo() << "Found" << numGamepads << "gamepad(s) at startup";
        for (int i = 0; i < numGamepads; ++i) {
            addGamepadDevice(gamepadIds[i]);
        }
        SDL_free(gamepadIds);
    }

    int numJoysticks = 0;
    SDL_JoystickID *joystickIds = SDL_GetJoysticks(&numJoysticks);
    if (joystickIds) {
        qInfo() << "Found" << numJoysticks << "joystick-capable device(s) at startup";
        for (int i = 0; i < numJoysticks; ++i) {
            if (!SDL_IsGamepad(joystickIds[i])) {
                addJoystickDevice(joystickIds[i]);
            }
        }
        SDL_free(joystickIds);
    }

    // Do an initial poll shortly after startup
    QTimer::singleShot(100, this, &SdlController::poll);
}

SdlController::~SdlController()
{
    // Clean up devices
    for (auto device : m_devices) {
        ControllerManager::instance().deviceRemoved(device);
        delete device;
    }
    m_devices.clear();

    if (s_sdlInitialized) {
        SDL_Quit();
        s_sdlInitialized = false;
    }
}

void SdlController::poll()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            qInfo() << "Gamepad added event, instance ID:" << event.gdevice.which;
            addGamepadDevice(event.gdevice.which);
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            qInfo() << "Gamepad removed event, instance ID:" << event.gdevice.which;
            removeDevice(event.gdevice.which);
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (m_devices.contains(event.gbutton.which)) {
                m_devices.value(event.gbutton.which)->processButtonEvent(event.gbutton);
            }
            break;

        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (m_devices.contains(event.gaxis.which)) {
                m_devices.value(event.gaxis.which)->processAxisEvent(event.gaxis);
            }
            break;

        case SDL_EVENT_JOYSTICK_ADDED:
            if (!SDL_IsGamepad(event.jdevice.which)) {
                qInfo() << "Joystick added event, instance ID:" << event.jdevice.which;
                addJoystickDevice(event.jdevice.which);
            }
            break;

        case SDL_EVENT_JOYSTICK_REMOVED:
            if (m_devices.contains(event.jdevice.which) && !m_devices.value(event.jdevice.which)->isGamepad()) {
                qInfo() << "Joystick removed event, instance ID:" << event.jdevice.which;
                removeDevice(event.jdevice.which);
            }
            break;

        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
            if (m_devices.contains(event.jbutton.which) && !m_devices.value(event.jbutton.which)->isGamepad()) {
                m_devices.value(event.jbutton.which)->processJoystickButtonEvent(event.jbutton);
            }
            break;

        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
            if (m_devices.contains(event.jaxis.which) && !m_devices.value(event.jaxis.which)->isGamepad()) {
                m_devices.value(event.jaxis.which)->processJoystickAxisEvent(event.jaxis);
            }
            break;

        case SDL_EVENT_JOYSTICK_HAT_MOTION:
            if (m_devices.contains(event.jhat.which) && !m_devices.value(event.jhat.which)->isGamepad()) {
                m_devices.value(event.jhat.which)->processJoystickHatEvent(event.jhat);
            }
            break;
        }
    }
}

bool SdlController::hasConnectedControllers() const
{
    return !m_devices.isEmpty();
}

void SdlController::setSuppressInput(bool suppress)
{
    bool oldValue = m_suppressInput;

    if (m_autoUnsuppressTimer) {
        m_autoUnsuppressTimer->stop();
    }

    m_manualSuppressInput = suppress;
    m_suppressInput = suppress;

    qInfo() << "SDL input suppression (manual):" << (suppress ? "enabled" : "disabled")
            << "-> effective:" << (m_suppressInput ? "suppressed" : "not suppressed");

    if (m_suppressInput != oldValue) {
        if (m_suppressInput) {
            releasePressedInput();
        }
        Q_EMIT isSuppressInputChanged(m_suppressInput, false);
    }

    if (!m_manualSuppressInput) {
        updateAutomaticSuppression();
    }
}

void SdlController::setAutoSuppressInput(bool enabled)
{
    if (m_autoSuppressInput == enabled) {
        return;
    }

    m_autoSuppressInput = enabled;

    auto *settings = InputHandlerSettings::self();
    settings->setAutoSuppressInput(enabled);
    settings->save();

    updateAutomaticSuppression();
    Q_EMIT autoSuppressInputChanged(enabled);
}

void SdlController::updateAutomaticSuppression()
{
    if (m_manualSuppressInput || !m_deviceWatcher) {
        return;
    }

    const bool shouldSuppress = m_autoSuppressInput && m_deviceWatcher->hasOtherProcesses();
    if (shouldSuppress) {
        if (m_autoUnsuppressTimer) {
            m_autoUnsuppressTimer->stop();
        }
        setAutomaticSuppression(true);
        return;
    }

    if (!m_suppressInput) {
        return;
    }

    if (m_autoSuppressInput && m_autoUnsuppressTimer) {
        if (!m_autoUnsuppressTimer->isActive()) {
            m_autoUnsuppressTimer->start();
        }
        return;
    }

    setAutomaticSuppression(false);
}

void SdlController::setAutomaticSuppression(bool suppress)
{
    if (m_suppressInput == suppress) {
        return;
    }

    m_suppressInput = suppress;
    if (m_suppressInput) {
        releasePressedInput();
    }

    Q_EMIT isSuppressInputChanged(m_suppressInput, true);
    qInfo() << "SDL input suppression (auto):" << (m_suppressInput ? "enabled" : "disabled");
}

void SdlController::releasePressedInput()
{
    for (SdlDevice *device : std::as_const(m_devices)) {
        ControllerManager::instance().releasePressedInput(device);
        device->resetInputState();
    }
}

void SdlController::addGamepadDevice(SDL_JoystickID instanceId)
{
    if (m_devices.contains(instanceId)) {
        qWarning() << "Device already exists, instance ID:" << instanceId;
        return;
    }

    SDL_Gamepad *gamepad = SDL_OpenGamepad(instanceId);
    if (!gamepad) {
        qWarning() << "Failed to open gamepad:" << SDL_GetError();
        return;
    }

    QString deviceName = gamepadName(gamepad);
    qInfo() << "Adding SDL gamepad:" << deviceName;

    auto device = new SdlGamepadDevice(gamepad, instanceId, this);
    m_devices.insert(instanceId, device);

    // Register the device path with the watcher
    QString devicePath = gamepadPath(gamepad);
    if (!devicePath.isEmpty()) {
        m_deviceWatcher->addDevicePath(devicePath);
    }

    ControllerManager::instance().newDevice(device);

    Q_EMIT controllerAdded(deviceName);

    // Switch to faster polling when we have devices
    m_pollTimer->setInterval(SHORT_POLL_INTERVAL);
}

void SdlController::addJoystickDevice(SDL_JoystickID instanceId)
{
    if (m_devices.contains(instanceId)) {
        qWarning() << "Device already exists, instance ID:" << instanceId;
        return;
    }

    SDL_Joystick *joystick = SDL_OpenJoystick(instanceId);
    if (!joystick) {
        qWarning() << "Failed to open joystick:" << SDL_GetError();
        return;
    }

    QString deviceName = joystickName(joystick);
    qInfo() << "Adding SDL joystick:" << deviceName;

    auto device = new SdlJoystickDevice(joystick, instanceId, this);
    m_devices.insert(instanceId, device);

    if (!device->devicePath().isEmpty()) {
        m_deviceWatcher->addDevicePath(device->devicePath());
    }

    ControllerManager::instance().newDevice(device);

    Q_EMIT controllerAdded(deviceName);

    // Switch to faster polling when we have devices
    m_pollTimer->setInterval(SHORT_POLL_INTERVAL);
}

void SdlController::removeDevice(SDL_JoystickID instanceId)
{
    if (!m_devices.contains(instanceId)) {
        qWarning() << "Device not found for removal, instance ID:" << instanceId;
        return;
    }

    auto device = m_devices.take(instanceId);
    QString deviceName = device->getName();
    qInfo() << "Removing SDL controller:" << deviceName;

    if (!device->devicePath().isEmpty()) {
        m_deviceWatcher->removeDevicePath(device->devicePath());
    }

    ControllerManager::instance().deviceRemoved(device);
    delete device;

    Q_EMIT controllerRemoved(deviceName);

    // Switch to slower polling if no devices
    if (m_devices.isEmpty()) {
        m_pollTimer->setInterval(LONG_POLL_INTERVAL);
    }
}

SdlDevice::SdlDevice(QString name, QString uniqueIdentifier, QString devicePath, SdlControllerFamily family, SDL_JoystickID instanceId, SdlController *controller)
    : Device(DeviceGamepad, std::move(name), std::move(uniqueIdentifier))
    , m_controller(controller)
    , m_instanceId(instanceId)
    , m_devicePath(std::move(devicePath))
    , m_controllerFamily(family)
{
    setControllerFamily(controllerFamilyId(m_controllerFamily), controllerFamilyName(m_controllerFamily));
}

void SdlDevice::initializeUsedKeys(const QList<InputAction> &buttonActions)
{
    QSet<int> keys;
    keys.unite(keysForInputActions(buttonActions));

    // Add keys produced by axes, hats and triggers.
    keys.unite(keysForInputActions({
        InputAction::NavigateUp,
        InputAction::NavigateDown,
        InputAction::NavigateLeft,
        InputAction::NavigateRight,
        InputAction::BrowserBack,
        InputAction::BrowserForward,
    }));

    setUsedKeys(keys);

    // Set up mouse movement timer (runs at ~60fps when right stick is active)
    m_mouseTimer = new QTimer(this);
    m_mouseTimer->setInterval(16);
    connect(m_mouseTimer, &QTimer::timeout, this, &SdlDevice::updateMouseMovement);
}

SdlDevice::~SdlDevice()
{
    if (m_mouseTimer) {
        m_mouseTimer->stop();
    }
    qDebug() << "Destroyed SdlDevice:" << m_name;
}

void SdlDevice::processButtonEvent(const SDL_GamepadButtonEvent &event)
{
    Q_UNUSED(event)
}

void SdlDevice::processAxisEvent(const SDL_GamepadAxisEvent &event)
{
    Q_UNUSED(event)
}

void SdlDevice::processJoystickButtonEvent(const SDL_JoyButtonEvent &event)
{
    Q_UNUSED(event)
}

void SdlDevice::processJoystickAxisEvent(const SDL_JoyAxisEvent &event)
{
    Q_UNUSED(event)
}

void SdlDevice::processJoystickHatEvent(const SDL_JoyHatEvent &event)
{
    Q_UNUSED(event)
}

void SdlDevice::resetInputState()
{
    m_pressedKeys.clear();
    m_axisLeftXDirection = 0;
    m_axisLeftYDirection = 0;
    m_hatXDirection = 0;
    m_hatYDirection = 0;
    m_rightStickX = 0.0;
    m_rightStickY = 0.0;

    if (m_mouseTimer) {
        m_mouseTimer->stop();
    }
}

SdlGamepadDevice::SdlGamepadDevice(SDL_Gamepad *gamepad, SDL_JoystickID instanceId, SdlController *controller)
    : SdlDevice(gamepadName(gamepad), gamepadUniqueIdentifier(gamepad, instanceId), gamepadPath(gamepad), gamepadFamily(gamepad), instanceId, controller)
    , m_gamepad(gamepad)
    , m_buttons(gamepadButtonMappings(gamepadFamily(gamepad)))
{
    initializeUsedKeys(actionsFromMappings(m_buttons));
    qDebug() << "Created SdlGamepadDevice:" << m_name << "identifier:" << m_uniqueIdentifier << "family:" << controllerFamilyId();
}

SdlGamepadDevice::~SdlGamepadDevice()
{
    if (m_gamepad) {
        SDL_CloseGamepad(m_gamepad);
    }
}

SdlJoystickDevice::SdlJoystickDevice(SDL_Joystick *joystick, SDL_JoystickID instanceId, SdlController *controller)
    : SdlDevice(joystickName(joystick), joystickUniqueIdentifier(joystick, instanceId), joystickPath(joystick), joystickFamily(joystick), instanceId, controller)
    , m_joystick(joystick)
    , m_buttons(joystickButtonMappings())
{
    initializeUsedKeys(actionsFromMappings(m_buttons));
    qDebug() << "Created SdlJoystickDevice:" << m_name << "identifier:" << m_uniqueIdentifier << "family:" << controllerFamilyId();
}

SdlJoystickDevice::~SdlJoystickDevice()
{
    if (m_joystick) {
        SDL_CloseJoystick(m_joystick);
    }
}

void SdlDevice::updateMouseMovement()
{
    // Suppress mouse movement when input is suppressed
    if (m_controller->isSuppressInput()) {
        return;
    }

    // Check if right stick is outside deadzone
    if (qAbs(m_rightStickX) > MOUSE_DEADZONE || qAbs(m_rightStickY) > MOUSE_DEADZONE) {
        // Normalize to -1.0 to 1.0 range, applying deadzone
        double normalizedX = 0.0;
        double normalizedY = 0.0;

        if (qAbs(m_rightStickX) > MOUSE_DEADZONE) {
            normalizedX = (m_rightStickX - (m_rightStickX > 0 ? MOUSE_DEADZONE : -MOUSE_DEADZONE)) / (32767.0 - MOUSE_DEADZONE);
        }
        if (qAbs(m_rightStickY) > MOUSE_DEADZONE) {
            normalizedY = (m_rightStickY - (m_rightStickY > 0 ? MOUSE_DEADZONE : -MOUSE_DEADZONE)) / (32767.0 - MOUSE_DEADZONE);
        }

        // Apply sensitivity and send mouse movement
        double deltaX = normalizedX * MOUSE_SENSITIVITY;
        double deltaY = normalizedY * MOUSE_SENSITIVITY;

        ControllerManager::instance().emitPointerMotion(this, deltaX, deltaY);
    }
}

bool SdlDevice::inputAllowedWhileSuppressed(InputAction action)
{
    if (!m_controller->isSuppressInput()) {
        return true;
    }

    if (!inputActionAllowedWhenSuppressed(action)) {
        return false;
    }

    return ControllerManager::instance().startButtonEnabledWhenSuppressed(getUniqueIdentifier());
}

void SdlDevice::setAction(InputAction action, bool pressed)
{
    if (action == InputAction::None) {
        return;
    }

    const QList<int> keyCodes = keysForInputAction(action);
    if (pressed) {
        for (int key : keyCodes) {
            setKey(action, key, pressed);
        }
    } else {
        for (int i = keyCodes.size() - 1; i >= 0; --i) {
            setKey(action, keyCodes.at(i), pressed);
        }
    }
}

void SdlDevice::setKey(InputAction action, int key, bool pressed)
{
    if (key < 0) {
        return;
    }

    // While another app owns the controller, ignore non-system actions entirely.
    // This keeps Bigscreen from inheriting stale button/axis state when handoff ends.
    if (!inputAllowedWhileSuppressed(action)) {
        if (!pressed) {
            m_pressedKeys.remove(key);
        }
        return;
    }

    if (pressed == m_pressedKeys.contains(key)) {
        return;
    }

    if (pressed) {
        m_pressedKeys.insert(key);
    } else {
        m_pressedKeys.remove(key);
    }

    if (inputActionEmitsHome(action)) {
        if (pressed) {
            ControllerManager::instance().emitHomeAction(this);
        }
        return;
    }

    if (inputActionRequestsDisplayOff(action)) {
        if (pressed) {
            ControllerManager::instance().emitDisplayOffAction(this);
        }
        return;
    }

    ControllerManager::instance().emitKey(this, key, pressed);
}

void SdlDevice::setDirectionalAction(int newDirection, int &currentDirection, InputAction negativeAction, InputAction positiveAction)
{
    if (m_controller->isSuppressInput() && !inputAllowedWhileSuppressed(negativeAction) && !inputAllowedWhileSuppressed(positiveAction)) {
        currentDirection = 0;
        return;
    }

    if (newDirection == currentDirection) {
        return;
    }

    if (currentDirection == -1) {
        setAction(negativeAction, false);
    } else if (currentDirection == 1) {
        setAction(positiveAction, false);
    }

    if (newDirection == -1) {
        setAction(negativeAction, true);
    } else if (newDirection == 1) {
        setAction(positiveAction, true);
    }

    currentDirection = newDirection;
}

void SdlDevice::updateMouseTimer()
{
    if (m_controller->isSuppressInput()) {
        m_rightStickX = 0.0;
        m_rightStickY = 0.0;
        if (m_mouseTimer->isActive()) {
            m_mouseTimer->stop();
        }
        return;
    }

    const bool stickActive = (qAbs(m_rightStickX) > MOUSE_DEADZONE || qAbs(m_rightStickY) > MOUSE_DEADZONE);
    if (stickActive && !m_mouseTimer->isActive()) {
        m_mouseTimer->start();
    } else if (!stickActive && m_mouseTimer->isActive()) {
        m_mouseTimer->stop();
    }
}

void SdlGamepadDevice::processButtonEvent(const SDL_GamepadButtonEvent &event)
{
    bool pressed = (event.down != 0);
    auto button = static_cast<SDL_GamepadButton>(event.button);

    qDebug() << "Button event:" << event.button << "pressed:" << pressed;

    // Right stick click -> left mouse button (suppressed when input suppressed)
    if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
        if (!m_controller->isSuppressInput()) {
            ControllerManager::instance().emitPointerButton(this, BTN_LEFT, pressed);
        }
        return;
    }
    // Left stick click -> right mouse button (suppressed when input suppressed)
    if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
        if (!m_controller->isSuppressInput()) {
            ControllerManager::instance().emitPointerButton(this, BTN_RIGHT, pressed);
        }
        return;
    }

    const QList<InputAction> actions = m_buttons.value(button);
    for (InputAction action : actions) {
        setAction(action, pressed);
    }
}

void SdlGamepadDevice::processAxisEvent(const SDL_GamepadAxisEvent &event)
{
    int value = event.value;
    auto axis = static_cast<SDL_GamepadAxis>(event.axis);

    // Handle left stick X axis (left/right navigation)
    if (axis == SDL_GAMEPAD_AXIS_LEFTX) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1; // Right
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1; // Left
        }
        setDirectionalAction(newDirection, m_axisLeftXDirection, InputAction::NavigateLeft, InputAction::NavigateRight);
    }
    // Handle left stick Y axis (up/down navigation)
    else if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1; // Down
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1; // Up
        }
        setDirectionalAction(newDirection, m_axisLeftYDirection, InputAction::NavigateUp, InputAction::NavigateDown);
    }
    // Handle left trigger (L2)
    else if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
        bool pressed = (value > AXIS_THRESHOLD);
        setAction(InputAction::BrowserBack, pressed);
    }
    // Handle right trigger (R2)
    else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        bool pressed = (value > AXIS_THRESHOLD);
        setAction(InputAction::BrowserForward, pressed);
    }
    // Handle right stick X axis (mouse horizontal movement)
    else if (axis == SDL_GAMEPAD_AXIS_RIGHTX) {
        m_rightStickX = value;
        updateMouseTimer();
    }
    // Handle right stick Y axis (mouse vertical movement)
    else if (axis == SDL_GAMEPAD_AXIS_RIGHTY) {
        m_rightStickY = value;
        updateMouseTimer();
    }
}

void SdlJoystickDevice::processJoystickButtonEvent(const SDL_JoyButtonEvent &event)
{
    const bool pressed = (event.down != 0);
    qDebug() << "Joystick button event:" << event.button << "pressed:" << pressed;

    const QList<InputAction> actions = m_buttons.value(event.button);
    for (InputAction action : actions) {
        setAction(action, pressed);
    }
}

void SdlJoystickDevice::processJoystickAxisEvent(const SDL_JoyAxisEvent &event)
{
    const int value = event.value;

    if (event.axis == 0) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1;
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1;
        }
        setDirectionalAction(newDirection, m_axisLeftXDirection, InputAction::NavigateLeft, InputAction::NavigateRight);
    } else if (event.axis == 1) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1;
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1;
        }
        setDirectionalAction(newDirection, m_axisLeftYDirection, InputAction::NavigateUp, InputAction::NavigateDown);
    } else if (event.axis == 2) {
        m_rightStickX = value;
        updateMouseTimer();
    } else if (event.axis == 3) {
        m_rightStickY = value;
        updateMouseTimer();
    } else if (event.axis == 4) {
        setAction(InputAction::BrowserBack, value > AXIS_THRESHOLD);
    } else if (event.axis == 5) {
        setAction(InputAction::BrowserForward, value > AXIS_THRESHOLD);
    }
}

void SdlJoystickDevice::processJoystickHatEvent(const SDL_JoyHatEvent &event)
{
    const int newXDirection = (event.value & SDL_HAT_LEFT) ? -1 : ((event.value & SDL_HAT_RIGHT) ? 1 : 0);
    const int newYDirection = (event.value & SDL_HAT_UP) ? -1 : ((event.value & SDL_HAT_DOWN) ? 1 : 0);

    setDirectionalAction(newXDirection, m_hatXDirection, InputAction::NavigateLeft, InputAction::NavigateRight);
    setDirectionalAction(newYDirection, m_hatYDirection, InputAction::NavigateUp, InputAction::NavigateDown);
}
