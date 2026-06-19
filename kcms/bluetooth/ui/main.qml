// SPDX-FileCopyrightText: 2025 User8395 <therealuser8395@proton.me>
// SPDX-License-Identifier: GPL-2.0-or-later

// Some functions in this KCM were taken from the
// Plasma Desktop Bluetooth KCM (plasma/bluedevil).
// All credit goes to the respective authors.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.bluezqt as BluezQt
import org.kde.kcmutils as KCM
import org.kde.bigscreen as Bigscreen

import org.kde.plasma.bigscreen.bluetooth

import "script.js" as Script

Bigscreen.ScrollablePage {
    id: bluetoothView

    title: i18n("Bluetooth")
    background: null

    leftPadding: Kirigami.Units.smallSpacing
    topPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing

    property BluezQt.Manager manager: BluezQt.Manager
    readonly property var usableAdapter: manager.usableAdapter
    readonly property bool bluetoothReady: BluezQt.Manager.bluetoothOperational && usableAdapter
    readonly property bool discovering: bluetoothReady && usableAdapter.discovering
    property string discoveryError: ""

    function startDiscovery() {
        if (!bluetoothReady) {
            return;
        }

        discoveryError = "";
        Script.makeCall(usableAdapter.startDiscovery(), call => {
            if (call.error) {
                discoveryError = call.errorText;
            }
        });
    }

    function openDeviceSidebar(device, focusDelegate) {
        sidebarOverlay.delegate = focusDelegate;
        sidebarOverlay.device = device;
        sidebarOverlay.open();
    }

    Connections {
        target: manager

        onUsableAdapterChanged: {
            bluetoothView.startDiscovery();
        }
    }

    Component.onCompleted: startDiscovery()

    onActiveFocusChanged: {
        if (activeFocus) {
            bluetoothToggle.forceActiveFocus();
            startDiscovery();
        }
    }

    Timer {
        id: discoveryRestartTimer
        interval: 500
        repeat: false
        onTriggered: bluetoothView.startDiscovery()
    }

    DevicesProxyModel {
        id: pairedDevicesModel
        pairedOnly: true
        sourceModel: BluezQt.DevicesModel {}
    }

    DevicesProxyModel {
        id: unpairedDevicesModel
        pairedOnly: false
        sourceModel: BluezQt.DevicesModel {}
    }

    DevicesProxyModel {
        id: controllerCandidatesModel
        pairedOnly: false
        inputDevicesOnly: true
        sourceModel: BluezQt.DevicesModel {}
    }

    ColumnLayout {
        KeyNavigation.left: bluetoothView.KeyNavigation.left
        id: column
        spacing: 0

        Bigscreen.SwitchDelegate {
            id: bluetoothToggle
            raisedBackground: false
            text: i18n("Enable Bluetooth")
            icon.name: "network-bluetooth"
            checked: BluezQt.Manager.bluetoothOperational
            onClicked: {
                const bluetoothStatus = checked;

                BluezQt.Manager.bluetoothBlocked = !bluetoothStatus;
                BluezQt.Manager.adapters.forEach(adapter => {
                    adapter.powered = bluetoothStatus;
                });

                if (bluetoothStatus) {
                    discoveryRestartTimer.restart();
                }

                checked = Qt.binding(() => BluezQt.Manager.bluetoothOperational);
            }

            KeyNavigation.down: addControllerButton
        }

        Bigscreen.ButtonDelegate {
            id: addControllerButton
            raisedBackground: false
            visible: BluezQt.Manager.bluetoothOperational
            text: i18n("Add game controller")
            description: i18n("Pair Xbox, PlayStation, Steam, and other Bluetooth controllers")
            icon.name: "input-gamepad-symbolic"
            enabled: bluetoothView.bluetoothReady

            KeyNavigation.up: bluetoothToggle
            KeyNavigation.down: scanButton

            onClicked: {
                bluetoothView.startDiscovery();
                controllerSetupDialog.open();
            }
        }

        Bigscreen.ButtonDelegate {
            id: scanButton
            raisedBackground: false
            visible: BluezQt.Manager.bluetoothOperational
            text: bluetoothView.discovering ? i18n("Scanning for devices…") : i18n("Scan for devices")
            description: bluetoothView.discoveryError || i18n("Use this while a controller is in pairing mode")
            icon.name: bluetoothView.discoveryError ? "dialog-warning-symbolic" : "view-refresh-symbolic"
            enabled: bluetoothView.bluetoothReady

            KeyNavigation.up: addControllerButton
            KeyNavigation.down: pairedDelegateList

            onClicked: bluetoothView.startDiscovery()
        }

        QQC2.Label {
            id: pairedLabel
            text: i18n("Paired devices")
            visible: BluezQt.Manager.bluetoothOperational
            font.pixelSize: Bigscreen.Units.headingFontPixelSize
            Layout.topMargin: Kirigami.Units.gridUnit
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        ListView {
            id: pairedDelegateList
            Layout.fillWidth: true
            implicitHeight: contentHeight
            spacing: Kirigami.Units.smallSpacing
            visible: BluezQt.Manager.bluetoothOperational
            KeyNavigation.down: unpairedDelegateList

            clip: true
            model: pairedDevicesModel
            delegate: DeviceDelegate {
                id: pairedDelegate
                width: pairedDelegateList.width
                smallDescription: true
                raisedBackground: false

                onClicked: {
                    bluetoothView.openDeviceSidebar(model.Device, pairedDelegate);
                }
            }
        }

        QQC2.Label {
            id: unpairedLabel
            text: i18n("Available devices")
            visible: BluezQt.Manager.bluetoothOperational
            font.pixelSize: Bigscreen.Units.headingFontPixelSize
            Layout.topMargin: Kirigami.Units.gridUnit
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        ListView {
            id: unpairedDelegateList
            Layout.fillWidth: true
            implicitHeight: contentHeight
            spacing: Kirigami.Units.smallSpacing
            visible: BluezQt.Manager.bluetoothOperational
            KeyNavigation.up: pairedDelegateList

            clip: true
            model: unpairedDevicesModel
            delegate: DeviceDelegate {
                id: unpairedDelegate
                width: pairedDelegateList.width
                smallDescription: true
                raisedBackground: false

                onClicked: {
                    bluetoothView.openDeviceSidebar(model.Device, unpairedDelegate);
                }
            }
        }

        DeviceConnectionSidebar {
            id: sidebarOverlay

            property var delegate
            onClosed: {
                if (delegate) {
                    delegate.forceActiveFocus();
                } else {
                    pairedDevicesModel.forceActiveFocus();
                }
            }
        }
    }

    Bigscreen.Dialog {
        id: controllerSetupDialog
        title: i18n("Add game controller")
        openFocusItem: controllerScanButton

        onOpened: bluetoothView.startDiscovery()
        onClosed: addControllerButton.forceActiveFocus()

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Bigscreen.TextDelegate {
                Layout.fillWidth: true
                text: i18n("Put your controller in pairing mode")
                description: i18n("For Xbox, hold the pairing button until the Xbox light flashes. For PlayStation, hold Share and PS until the light bar flashes. For Steam controllers, use Bluetooth pairing mode.")
                icon.name: "input-gamepad-symbolic"
            }

            Bigscreen.ButtonDelegate {
                id: controllerScanButton
                Layout.fillWidth: true
                text: bluetoothView.discovering ? i18n("Scanning…") : i18n("Scan again")
                description: bluetoothView.discoveryError || i18n("Only likely controllers and unresolved input devices are shown here")
                icon.name: bluetoothView.discoveryError ? "dialog-warning-symbolic" : "view-refresh-symbolic"

                KeyNavigation.down: controllerCandidatesModel.count > 0 ? controllerCandidatesView : closeControllerSetupButton
                onClicked: bluetoothView.startDiscovery()
            }

            Bigscreen.TextDelegate {
                Layout.fillWidth: true
                visible: controllerCandidatesModel.count === 0
                text: bluetoothView.discovering ? i18n("Looking for controllers…") : i18n("No new controllers found")
                description: unpairedDevicesModel.count > 0
                    ? i18n("Other Bluetooth devices are available in the device list below.")
                    : i18n("Make sure the controller is nearby, awake, and still flashing.")
                icon.name: "view-refresh-symbolic"
            }

            ListView {
                id: controllerCandidatesView
                Layout.fillWidth: true
                implicitHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 14)
                visible: count > 0
                clip: true
                spacing: Kirigami.Units.smallSpacing
                model: controllerCandidatesModel
                keyNavigationEnabled: true

                KeyNavigation.up: controllerScanButton
                KeyNavigation.down: closeControllerSetupButton

                delegate: DeviceDelegate {
                    id: controllerCandidateDelegate
                    width: controllerCandidatesView.width
                    smallDescription: true
                    raisedBackground: false

                    onClicked: {
                        controllerSetupDialog.close();
                        bluetoothView.openDeviceSidebar(model.Device, addControllerButton);
                    }
                }
            }

            Bigscreen.ButtonDelegate {
                id: closeControllerSetupButton
                Layout.fillWidth: true
                text: i18n("Close")
                icon.name: "dialog-close-symbolic"

                KeyNavigation.up: controllerCandidatesModel.count > 0 ? controllerCandidatesView : controllerScanButton
                onClicked: controllerSetupDialog.close()
            }
        }
    }
}
