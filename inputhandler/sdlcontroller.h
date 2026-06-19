/*
 *   SPDX-FileCopyrightText: 2026 Devin Lin <devin@kde.org>
 *
 *   SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>

#include "device.h"
#include "devicewatcher.h"
#include "inputaction.h"

#include <SDL3/SDL.h>

class SdlController;

enum class SdlControllerFamily {
    Generic,
    Xbox,
    PlayStation,
    Steam,
};

class SdlDevice : public Device
{
    Q_OBJECT

public:
    explicit SdlDevice(QString name, QString uniqueIdentifier, QString devicePath, SdlControllerFamily family, SDL_JoystickID instanceId, SdlController *controller);
    ~SdlDevice() override;

    virtual bool isGamepad() const = 0;
    QString devicePath() const
    {
        return m_devicePath;
    }
    SDL_JoystickID instanceId() const
    {
        return m_instanceId;
    }

    virtual void processButtonEvent(const SDL_GamepadButtonEvent &event);
    virtual void processAxisEvent(const SDL_GamepadAxisEvent &event);
    virtual void processJoystickButtonEvent(const SDL_JoyButtonEvent &event);
    virtual void processJoystickAxisEvent(const SDL_JoyAxisEvent &event);
    virtual void processJoystickHatEvent(const SDL_JoyHatEvent &event);

Q_SIGNALS:
    void keyPress(int keyCode, bool pressed);

private Q_SLOTS:
    void updateMouseMovement();

protected:
    void initializeUsedKeys(const QList<InputAction> &buttonActions);
    void setAction(InputAction action, bool pressed);
    void setDirectionalAction(int newDirection, int &currentDirection, InputAction negativeAction, InputAction positiveAction);
    void updateMouseTimer();

    SdlController *const m_controller;
    // Axis state for direction tracking (left stick -> keyboard)
    int m_axisLeftXDirection = 0; // -1 left, 0 center, 1 right
    int m_axisLeftYDirection = 0; // -1 up, 0 center, 1 down
    int m_hatXDirection = 0;
    int m_hatYDirection = 0;
    // Right stick state for mouse movement
    double m_rightStickX = 0.0;
    double m_rightStickY = 0.0;

private:
    void setKey(InputAction action, int key, bool pressed);
    bool inputAllowedWhileSuppressed(InputAction action);

    SDL_JoystickID m_instanceId;
    const QString m_devicePath;
    const SdlControllerFamily m_controllerFamily;

    QSet<int> m_pressedKeys;

    QTimer *m_mouseTimer = nullptr;

    // Threshold for axis to be considered pressed (0-32767 range)
    static constexpr int AXIS_THRESHOLD = 16384;
    // Deadzone for mouse movement (smaller than keyboard threshold)
    static constexpr int MOUSE_DEADZONE = 4000;
    // Mouse sensitivity multiplier
    static constexpr double MOUSE_SENSITIVITY = 15.0;
};

class SdlGamepadDevice : public SdlDevice
{
public:
    explicit SdlGamepadDevice(SDL_Gamepad *gamepad, SDL_JoystickID instanceId, SdlController *controller);
    ~SdlGamepadDevice() override;

    bool isGamepad() const override
    {
        return true;
    }

    void processButtonEvent(const SDL_GamepadButtonEvent &event) override;
    void processAxisEvent(const SDL_GamepadAxisEvent &event) override;

private:
    SDL_Gamepad *const m_gamepad = nullptr;
    const QMap<SDL_GamepadButton, QList<InputAction>> m_buttons;
};

class SdlJoystickDevice : public SdlDevice
{
public:
    explicit SdlJoystickDevice(SDL_Joystick *joystick, SDL_JoystickID instanceId, SdlController *controller);
    ~SdlJoystickDevice() override;

    bool isGamepad() const override
    {
        return false;
    }

    void processJoystickButtonEvent(const SDL_JoyButtonEvent &event) override;
    void processJoystickAxisEvent(const SDL_JoyAxisEvent &event) override;
    void processJoystickHatEvent(const SDL_JoyHatEvent &event) override;

private:
    SDL_Joystick *const m_joystick = nullptr;
    const QMap<int, QList<InputAction>> m_buttons;
};

class SdlController : public QObject
{
    Q_OBJECT

public:
    explicit SdlController();
    ~SdlController() override;

    bool hasConnectedControllers() const;

    void setSuppressInput(bool suppress);
    bool isSuppressInput() const
    {
        return m_suppressInput;
    }
    void setAutoSuppressInput(bool enabled);
    bool autoSuppressInput() const
    {
        return m_autoSuppressInput;
    }
    bool isManualSuppressInput() const
    {
        return m_manualSuppressInput;
    }

Q_SIGNALS:
    void controllerAdded(const QString &name);
    void controllerRemoved(const QString &name);
    void isSuppressInputChanged(bool suppressed, bool automatic); // automatic - whether it was changed by the DeviceWatcher
    void autoSuppressInputChanged(bool enabled);

private Q_SLOTS:
    void poll();

private:
    void addGamepadDevice(SDL_JoystickID instanceId);
    void addJoystickDevice(SDL_JoystickID instanceId);
    void removeDevice(SDL_JoystickID instanceId);
    void releasePressedInput();
    void updateAutomaticSuppression();

    QMap<SDL_JoystickID, SdlDevice *> m_devices;
    QTimer *m_pollTimer = nullptr;
    bool m_suppressInput = false;
    bool m_autoSuppressInput = true;
    bool m_manualSuppressInput = false; // Manually set via D-Bus
    DeviceWatcher *m_deviceWatcher = nullptr;

    // Polling intervals
    static constexpr int SHORT_POLL_INTERVAL = 16; // ~60fps when devices connected
    static constexpr int LONG_POLL_INTERVAL = 2000; // 2 seconds when no devices
};
