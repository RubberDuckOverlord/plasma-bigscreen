/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "sdlcontroller.h"
#include "controllermanager.h"
#include "inputhandlersettings.h"

#include <QByteArray>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>
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

static QMap<SDL_GamepadButton, QList<int>> gamepadButtonMappings()
{
    return {
        // Same mappings as evdev backend
        {SDL_GAMEPAD_BUTTON_GUIDE, {KEY_LEFTMETA}}, // BTN_MODE -> Meta
        {SDL_GAMEPAD_BUTTON_START, {KEY_GAMES}}, // BTN_START -> Games
        {SDL_GAMEPAD_BUTTON_SOUTH, {KEY_ENTER}}, // BTN_SOUTH (A/Cross) -> Enter
        {SDL_GAMEPAD_BUTTON_EAST, {KEY_CANCEL, KEY_ESC}}, // BTN_EAST (B/Circle) -> Cancel/Escape
        {SDL_GAMEPAD_BUTTON_WEST, {KEY_MENU}}, // BTN_WEST (X/Square) -> Menu
        {SDL_GAMEPAD_BUTTON_NORTH, {KEY_UNKNOWN}}, // BTN_NORTH (Y/Triangle) - no evdev mapping
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, {KEY_LEFTSHIFT, KEY_TAB}}, // BTN_TL -> Shift+Tab (previous)
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, {KEY_TAB}}, // BTN_TR -> Tab (next)
        {SDL_GAMEPAD_BUTTON_BACK, {KEY_BACK}}, // Select/Back -> Back
        {SDL_GAMEPAD_BUTTON_DPAD_UP, {KEY_UP}}, // D-Pad Up
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, {KEY_DOWN}}, // D-Pad Down
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, {KEY_LEFT}}, // D-Pad Left
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, {KEY_RIGHT}}, // D-Pad Right
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, {KEY_UNKNOWN}}, // Left stick click
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, {KEY_UNKNOWN}}, // Right stick click
    };
}

static QMap<int, QList<int>> joystickButtonMappings()
{
    return {
        {0, {KEY_ENTER}},
        {1, {KEY_CANCEL, KEY_ESC}},
        {2, {KEY_MENU}},
        {3, {KEY_UNKNOWN}},
        {4, {KEY_LEFTSHIFT, KEY_TAB}},
        {5, {KEY_TAB}},
        {6, {KEY_BACK}},
        {7, {KEY_GAMES}},
        {8, {KEY_LEFTMETA}},
    };
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

    bool oldValue = m_suppressInput;
    m_suppressInput = m_autoSuppressInput && m_deviceWatcher->hasOtherProcesses();

    if (m_suppressInput == oldValue) {
        return;
    }

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

    auto device = new SdlDevice(gamepad, instanceId, this);
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

    auto device = new SdlDevice(joystick, instanceId, this);
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

SdlDevice::SdlDevice(SDL_Gamepad *gamepad, SDL_JoystickID instanceId, SdlController *controller)
    : Device(DeviceGamepad, gamepadName(gamepad), gamepadUniqueIdentifier(gamepad, instanceId))
    , m_controller(controller)
    , m_gamepad(gamepad)
    , m_instanceId(instanceId)
    , m_devicePath(gamepadPath(gamepad))
    , m_buttons(gamepadButtonMappings())
    , m_joystickButtons(joystickButtonMappings())
{
    initializeUsedKeys();
    qDebug() << "Created SdlDevice:" << m_name << "identifier:" << m_uniqueIdentifier;
}

SdlDevice::SdlDevice(SDL_Joystick *joystick, SDL_JoystickID instanceId, SdlController *controller)
    : Device(DeviceGamepad, joystickName(joystick), joystickUniqueIdentifier(joystick, instanceId))
    , m_controller(controller)
    , m_joystick(joystick)
    , m_instanceId(instanceId)
    , m_devicePath(joystickPath(joystick))
    , m_buttons(gamepadButtonMappings())
    , m_joystickButtons(joystickButtonMappings())
{
    initializeUsedKeys();
    qDebug() << "Created SdlDevice:" << m_name << "identifier:" << m_uniqueIdentifier << "backend: joystick";
}

void SdlDevice::initializeUsedKeys()
{
    QSet<int> keys;
    auto addKeyCombination = [&keys](const QList<int> &keyCombination) {
        for (int key : keyCombination) {
            if (key != KEY_UNKNOWN) {
                keys.insert(key);
            }
        }
    };

    for (const QList<int> &keyCombination : m_buttons) {
        addKeyCombination(keyCombination);
    }
    for (const QList<int> &keyCombination : m_joystickButtons) {
        addKeyCombination(keyCombination);
    }

    // Add keys produced by axes, hats and triggers.
    keys.insert(KEY_UP);
    keys.insert(KEY_DOWN);
    keys.insert(KEY_LEFT);
    keys.insert(KEY_RIGHT);
    keys.insert(KEY_BACK);
    keys.insert(KEY_FORWARD);

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
    if (m_gamepad) {
        SDL_CloseGamepad(m_gamepad);
    }
    if (m_joystick) {
        SDL_CloseJoystick(m_joystick);
    }
    qDebug() << "Destroyed SdlDevice:" << m_name;
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

bool SdlDevice::inputAllowedWhileSuppressed(int key)
{
    if (!m_controller->isSuppressInput()) {
        return true;
    }

    if (key != KEY_LEFTMETA && key != KEY_GAMES) {
        return false;
    }

    return ControllerManager::instance().startButtonEnabledWhenSuppressed(getUniqueIdentifier());
}

void SdlDevice::setKey(int key, bool pressed)
{
    if (key == KEY_UNKNOWN) {
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

    // When suppressed, only allow selected system keys through.
    if (!inputAllowedWhileSuppressed(key)) {
        return;
    }

    // Turn left meta into home action directly
    if (key == KEY_LEFTMETA) {
        if (pressed) {
            ControllerManager::instance().emitHomeAction(this);
        }
        return;
    }

    ControllerManager::instance().emitKey(this, key, pressed);
}

void SdlDevice::setDirectionalKeys(int newDirection, int &currentDirection, int negativeKey, int positiveKey)
{
    if (newDirection == currentDirection) {
        return;
    }

    if (currentDirection == -1) {
        setKey(negativeKey, false);
    } else if (currentDirection == 1) {
        setKey(positiveKey, false);
    }

    if (newDirection == -1) {
        setKey(negativeKey, true);
    } else if (newDirection == 1) {
        setKey(positiveKey, true);
    }

    currentDirection = newDirection;
}

void SdlDevice::updateMouseTimer()
{
    const bool stickActive = (qAbs(m_rightStickX) > MOUSE_DEADZONE || qAbs(m_rightStickY) > MOUSE_DEADZONE);
    if (stickActive && !m_mouseTimer->isActive()) {
        m_mouseTimer->start();
    } else if (!stickActive && m_mouseTimer->isActive()) {
        m_mouseTimer->stop();
    }
}

void SdlDevice::processButtonEvent(const SDL_GamepadButtonEvent &event)
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

    auto keyCodes = m_buttons.value(button);
    if (!keyCodes.isEmpty()) {
        for (int key : keyCodes) {
            setKey(key, pressed);
        }
    }
}

void SdlDevice::processAxisEvent(const SDL_GamepadAxisEvent &event)
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
        setDirectionalKeys(newDirection, m_axisLeftXDirection, KEY_LEFT, KEY_RIGHT);
    }
    // Handle left stick Y axis (up/down navigation)
    else if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1; // Down
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1; // Up
        }
        setDirectionalKeys(newDirection, m_axisLeftYDirection, KEY_UP, KEY_DOWN);
    }
    // Handle left trigger (L2)
    else if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
        bool pressed = (value > AXIS_THRESHOLD);
        setKey(KEY_BACK, pressed);
    }
    // Handle right trigger (R2)
    else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        bool pressed = (value > AXIS_THRESHOLD);
        setKey(KEY_FORWARD, pressed);
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

void SdlDevice::processJoystickButtonEvent(const SDL_JoyButtonEvent &event)
{
    const bool pressed = (event.down != 0);
    qDebug() << "Joystick button event:" << event.button << "pressed:" << pressed;

    const auto keyCodes = m_joystickButtons.value(event.button);
    for (int key : keyCodes) {
        setKey(key, pressed);
    }
}

void SdlDevice::processJoystickAxisEvent(const SDL_JoyAxisEvent &event)
{
    const int value = event.value;

    if (event.axis == 0) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1;
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1;
        }
        setDirectionalKeys(newDirection, m_axisLeftXDirection, KEY_LEFT, KEY_RIGHT);
    } else if (event.axis == 1) {
        int newDirection = 0;
        if (value > AXIS_THRESHOLD) {
            newDirection = 1;
        } else if (value < -AXIS_THRESHOLD) {
            newDirection = -1;
        }
        setDirectionalKeys(newDirection, m_axisLeftYDirection, KEY_UP, KEY_DOWN);
    } else if (event.axis == 2) {
        m_rightStickX = value;
        updateMouseTimer();
    } else if (event.axis == 3) {
        m_rightStickY = value;
        updateMouseTimer();
    } else if (event.axis == 4) {
        setKey(KEY_BACK, value > AXIS_THRESHOLD);
    } else if (event.axis == 5) {
        setKey(KEY_FORWARD, value > AXIS_THRESHOLD);
    }
}

void SdlDevice::processJoystickHatEvent(const SDL_JoyHatEvent &event)
{
    const int newXDirection = (event.value & SDL_HAT_LEFT) ? -1 : ((event.value & SDL_HAT_RIGHT) ? 1 : 0);
    const int newYDirection = (event.value & SDL_HAT_UP) ? -1 : ((event.value & SDL_HAT_DOWN) ? 1 : 0);

    setDirectionalKeys(newXDirection, m_hatXDirection, KEY_LEFT, KEY_RIGHT);
    setDirectionalKeys(newYDirection, m_hatYDirection, KEY_UP, KEY_DOWN);
}
