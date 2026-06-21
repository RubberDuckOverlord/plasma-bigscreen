/*
    SPDX-FileCopyrightText: 2019 Aditya Mehra <aix.m@outlook.com>
    SPDX-FileCopyrightText: 2019 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.bigscreen as Bigscreen
import org.kde.bluezqt as BluezQt

import "script.js" as Script

Bigscreen.ButtonDelegate {
    id: delegate

    required property var model
    property bool connecting: false
    property bool disconnecting: false
    property bool controllerSetup: false

    function displayName() {
        if (model.DeviceFullName) {
            return model.DeviceFullName;
        }

        const addressName = model.Address ? model.Address.replace(/:/g, "-") : "";
        if (!model.Name || model.Name === addressName) {
            return i18n("Unknown device");
        }

        return model.Name;
    }

    function desc() {
        if (connecting) {
            return i18n("Connecting…");
        } else if (disconnecting) {
            return i18n("Disconnecting…");
        } else if (controllerSetup) {
            const labels = [];

            if (model.Connected) {
                labels.push(i18n("Ready to use"));
            } else if (model.Paired) {
                labels.push(i18n("Paired. Press to connect"));
            } else {
                labels.push(i18n("New controller. Press to pair"));
            }

            if (model.Battery) {
                labels.push(i18n("%1% Battery", model.Battery.percentage));
            }
            if (model.Name !== displayName() && model.Address) {
                labels.push(model.Address);
            }

            return labels.join(" · ");
        } else {
            const labels = [];

            if (model.Connected) {
                labels.push(i18n("Connected"));
            }

            labels.push(Script.deviceTypeToString(model.Device));

            if (model.Battery) {
                labels.push(i18n("%1% Battery", model.Battery.percentage));
            }
            if (model.Name !== displayName() && model.Address) {
                labels.push(model.Address);
            }

            return labels.join(" · ");
        }
    }

    icon.name: model.Icon
    text: displayName()
    textFont.bold: model.Connected

    description: desc()
    onDescriptionChanged: desc()
}
