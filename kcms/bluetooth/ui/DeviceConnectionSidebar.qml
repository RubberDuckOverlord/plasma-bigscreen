// SPDX-FileCopyrightText: 2025 User8395 <therealuser8395@proton.me>
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import Qt5Compat.GraphicalEffects

import org.kde.kirigami as Kirigami
import org.kde.bigscreen as Bigscreen
import org.kde.bluezqt as BluezQt
import org.kde.plasma.components as PlasmaComponents

import "script.js" as Script

Bigscreen.SidebarOverlay {
    id: root
    openFocusItem: deviceInfoButton

    property var device: null

    property bool connecting: false
    property bool disconnecting: false
    property string operationError: ""
    property bool autoConnectOnOpen: false
    property bool connectAfterPair: false
    property int connectAfterPairAttempts: 0
    property int inputConnectionRetryAttempts: 0
    property int inputReadinessRefreshAttempts: 0
    property int operationSerial: 0
    readonly property int maxInputConnectionRetryAttempts: 2

    function operationStatusText() {
        if (operationError) {
            return operationError;
        }
        if (connecting) {
            return i18n("Keep the device awake and in pairing mode until this finishes");
        }
        if (device && Script.isInputDevice(device) && device.connected) {
            if (!kcm.inputHandlerAvailable) {
                return i18n("Connected. Bigscreen input service is not available yet.");
            }
            if (kcm.hasConnectedInputControllerForDevice(device)) {
                return i18n("Ready for Bigscreen navigation.");
            }
            return i18n("Connected. Press a button on the controller to finish input setup.");
        }
        if (device && Script.isInputDevice(device) && device.paired) {
            return i18n("Trusted input devices can reconnect automatically.");
        }
        return "";
    }

    function primaryActionText() {
        if (!device) {
            return "";
        }
        if (connecting) {
            return i18n("Connecting…");
        }
        if (disconnecting) {
            return i18n("Disconnecting…");
        }
        if (device.connected) {
            return Script.isInputDevice(device) ? i18n("Disconnect controller") : i18n("Disconnect");
        }
        if (!device.paired) {
            return Script.isInputDevice(device) ? i18n("Pair controller") : i18n("Pair");
        }
        return Script.isInputDevice(device) ? i18n("Connect controller") : i18n("Connect");
    }

    function markInputDeviceTrusted() {
        if (device && Script.isInputDevice(device) && device.paired && !device.trusted) {
            device.trusted = true;
        }
    }

    function prepareInputDeviceConnection() {
        if (!device || !Script.isInputDevice(device)) {
            return;
        }

        if (device.blocked) {
            device.blocked = false;
        }

        markInputDeviceTrusted();
    }

    function beginOperation() {
        operationSerial++;
        operationTimeoutTimer.restart();
        return operationSerial;
    }

    function refreshInputReadiness() {
        if (device && Script.isInputDevice(device) && device.connected && kcm.inputHandlerAvailable) {
            kcm.refreshInputControllers();
        }
    }

    function resetInputReadinessPolling() {
        inputReadinessRefreshAttempts = 0;
        refreshInputReadiness();
    }

    function resetTransientOperationState(clearAutoConnect) {
        operationSerial++;
        operationTimeoutTimer.stop();
        connectAfterPairTimer.stop();
        inputConnectionRetryTimer.stop();

        connecting = false;
        disconnecting = false;
        operationError = "";
        connectAfterPair = false;
        connectAfterPairAttempts = 0;
        inputConnectionRetryAttempts = 0;
        inputReadinessRefreshAttempts = 0;

        if (clearAutoConnect) {
            autoConnectOnOpen = false;
        }
    }

    function startAutoConnectOnOpen() {
        if (!autoConnectOnOpen) {
            return;
        }

        autoConnectOnOpen = false;
        if (device && Script.isInputDevice(device) && device.paired && !device.connected && !connecting && !disconnecting) {
            operationError = "";
            startDeviceConnection(false);
        }
    }

    function finishOperation(call, fallbackError, connectAfterSuccess, serial) {
        if (serial !== operationSerial) {
            return;
        }

        operationTimeoutTimer.stop();
        root.connecting = false;
        root.disconnecting = false;

        if (call.error) {
            inputConnectionRetryTimer.stop();
            root.inputConnectionRetryAttempts = 0;
            root.operationError = call.errorText || fallbackError;
            return;
        }

        root.operationError = "";
        root.inputConnectionRetryAttempts = 0;
        markInputDeviceTrusted();
        refreshInputReadiness();

        if (connectAfterSuccess && device && Script.isInputDevice(device) && !device.connected) {
            root.connectAfterPair = true;
            root.connectAfterPairAttempts = 0;
            root.connecting = true;
            connectAfterPairTimer.restart();
        }
    }

    function connectInputDeviceAfterPair() {
        if (!connectAfterPair || !device || !Script.isInputDevice(device)) {
            return;
        }

        if (device.connected) {
            root.connectAfterPair = false;
            root.connecting = false;
            root.operationError = "";
            root.resetInputReadinessPolling();
            return;
        }

        if (!device.paired) {
            if (connectAfterPairAttempts < 8) {
                connectAfterPairAttempts++;
                connectAfterPairTimer.restart();
            } else {
                root.connectAfterPair = false;
                root.connecting = false;
                root.operationError = i18n("Pairing finished, but the controller is not ready yet. Press a button on the controller, then try Connect.");
            }
            return;
        }

        root.connectAfterPair = false;
        startDeviceConnection(false);
    }

    function inputConnectionFailureRetryable(call) {
        if (!call.error || !device || !Script.isInputDevice(device) || !device.paired || device.connected) {
            return false;
        }

        const errorText = call.errorText ? call.errorText.toLowerCase() : "";
        return !errorText.includes("authentication")
            && !errorText.includes("not paired")
            && !errorText.includes("rejected")
            && !errorText.includes("canceled")
            && !errorText.includes("cancelled");
    }

    function startDeviceConnection(retry) {
        if (!device) {
            return;
        }

        if (!retry) {
            root.inputConnectionRetryAttempts = 0;
        }

        prepareInputDeviceConnection();
        const serial = beginOperation();
        root.connecting = true;
        Script.makeCall(device.connectToDevice(), connectCall => {
            root.finishConnectionOperation(connectCall, serial);
        });
    }

    function finishConnectionOperation(call, serial) {
        if (serial !== operationSerial) {
            return;
        }

        if (inputConnectionFailureRetryable(call) && root.inputConnectionRetryAttempts < root.maxInputConnectionRetryAttempts) {
            root.inputConnectionRetryAttempts++;
            root.operationError = i18n("Connection did not complete. Press a controller button; retrying…");
            inputConnectionRetryTimer.restart();
            return;
        }

        root.finishOperation(call, i18n("Connecting failed"), false, serial);
    }

    Timer {
        id: operationTimeoutTimer
        interval: 30000
        repeat: false
        onTriggered: {
            root.operationSerial++;
            root.connecting = false;
            root.disconnecting = false;
            root.connectAfterPair = false;
            root.inputConnectionRetryAttempts = 0;
            inputConnectionRetryTimer.stop();
            root.operationError = i18n("The Bluetooth operation timed out. Keep the controller awake and try again.");
        }
    }

    Timer {
        id: connectAfterPairTimer
        interval: 500
        repeat: false
        onTriggered: root.connectInputDeviceAfterPair()
    }

    Timer {
        id: inputConnectionRetryTimer
        interval: 900
        repeat: false
        onTriggered: {
            if (!device || !root.connecting || device.connected) {
                root.inputConnectionRetryAttempts = 0;
                return;
            }

            root.startDeviceConnection(true);
        }
    }

    Timer {
        id: inputReadinessRefreshTimer
        interval: 1000
        repeat: true
        running: root.opened
            && device
            && Script.isInputDevice(device)
            && device.connected
            && kcm.inputHandlerAvailable
            && !kcm.hasConnectedInputControllerForDevice(device)
            && root.inputReadinessRefreshAttempts < 20
        onTriggered: {
            root.inputReadinessRefreshAttempts++;
            root.refreshInputReadiness();
        }
    }

    Connections {
        target: root.device
        ignoreUnknownSignals: true

        function onPairedChanged() {
            root.connectInputDeviceAfterPair();
        }

        function onConnectedChanged() {
            if (device && device.connected) {
                inputConnectionRetryTimer.stop();
                root.inputConnectionRetryAttempts = 0;
            }
            root.connectInputDeviceAfterPair();
            root.resetInputReadinessPolling();
        }
    }

    onOpened: {
        resetInputReadinessPolling();
        Qt.callLater(startAutoConnectOnOpen);
    }
    onClosed: resetTransientOperationState(true)
    onDeviceChanged: {
        resetTransientOperationState(false);
    }

    header: Bigscreen.SidebarOverlayHeader {
        iconSource: device ? device.icon : ""
        title: device ? device.name : ""
    }

    content: ColumnLayout {
        id: colLayoutSettingsItem
        spacing: Kirigami.Units.smallSpacing

        Bigscreen.ButtonDelegate {
            id: deviceInfoButton
            icon.name: "info"
            text: i18n("Device information")
            description: desc()

            onClicked: infoDialog.open()
            KeyNavigation.down: connectToggleButton
            Keys.onLeftPressed: root.close()
        }

        Bigscreen.ButtonDelegate {
            id: connectToggleButton

            text: root.primaryActionText()
            description: root.operationStatusText()
            icon.name: device ? (device.connected ? "network-disconnect" : "network-connect") : ""
            enabled: device && !root.connecting && !root.disconnecting

            KeyNavigation.down: forgetButton
            Keys.onLeftPressed: root.close()

            onClicked: {
                root.operationError = "";
                if (!device.paired) {
                    const serial = root.beginOperation();
                    root.connecting = true;
                    root.prepareInputDeviceConnection();
                    Script.makeCall(device.pair(), call => {
                        root.finishOperation(call, i18n("Pairing failed"), true, serial);
                    });
                } else if (device.connected) {
                    const serial = root.beginOperation();
                    root.disconnecting = true;
                    Script.makeCall(device.disconnectFromDevice(), call => {
                        root.finishOperation(call, i18n("Disconnecting failed"), false, serial);
                    });
                } else {
                    root.startDeviceConnection(false);
                }
            }
        }

        Bigscreen.TextDelegate {
            visible: device && Script.isInputDevice(device)
            text: i18n("Controller setup")
            description: device && device.connected
                ? (kcm.hasConnectedInputControllerForDevice(device)
                    ? i18n("Input is ready. Use the controller to move around Bigscreen.")
                    : i18n("If navigation does not start, press a controller button once or open Input settings to confirm it is detected."))
                : Script.controllerPairingHint(device)
            icon.name: "input-gamepad-symbolic"
        }

        Bigscreen.ButtonDelegate {
            id: forgetButton
            visible: device && device.paired
            text: i18n("Forget device")
            icon.name: "delete"

            KeyNavigation.down: trustedToggle
            Keys.onLeftPressed: root.close()

            onClicked: forgetDialog.open()

            Bigscreen.Dialog {
                id: forgetDialog
                standardButtons: Bigscreen.Dialog.Ok | Bigscreen.Dialog.Cancel
                title: i18n("Are you sure you want to forget the device %1?", device ? device.name : '')

                onAccepted: {
                    Script.makeCall(device.adapter.removeDevice(device), call => {
                        root.connecting = false;
                        if (call.error) {
                            console.log("makeCall error when forgetting: " + call.errorText);
                        }
                    });
                    forgetDialog.close();
                    root.close();
                }
                onRejected: {
                    forgetDialog.close();
                    forgetButton.forceActiveFocus();
                }
            }
        }

        Bigscreen.SwitchDelegate {
            id: trustedToggle
            visible: device && device.paired
            text: i18n("Trusted")
            description: i18n("Auto-accept incoming connections")
            checked: device && device.trusted

            KeyNavigation.down: blockedToggle
            Keys.onLeftPressed: root.close()

            onToggled: {
                device.trusted = checked;
            }
        }

        Bigscreen.SwitchDelegate {
            id: blockedToggle
            visible: device && device.paired
            text: i18n("Blocked")
            description: i18n("Reject all connections from this device")
            checked: device && device.blocked

            Keys.onLeftPressed: root.close()

            onToggled: {
                device.blocked = checked;
            }
        }

        Item {
            Layout.fillHeight: true
        }

        Bigscreen.Dialog {
            id: infoDialog
            title: i18n("Device details")

            onOpened: changeNameButton.forceActiveFocus()
            onClosed: deviceInfoButton.forceActiveFocus()

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    id: typeLabel
                    text: i18n("Type: %1", Script.deviceTypeToString(device))
                }

                QQC2.Label {
                    id: addressLabel
                    text: i18n("Address: %1", device ? device.address : "")
                }

                QQC2.Label {
                    id: batteryLabel
                    visible: device && device.battery
                    text: i18n("Battery: %1", device && device.battery
                        ? i18nc("Battery percentage level", "%1%", device.battery.percentage)
                        : "")
                }

                Bigscreen.ButtonDelegate {
                    id: changeNameButton
                    text: i18n("Change name")
                    icon.name: "document-edit-symbolic"
                    visible: device && device.paired
                    onClicked: changeNameDialog.open()

                    Bigscreen.Dialog {
                        id: changeNameDialog
                        title: i18n("Change name")
                        standardButtons: Bigscreen.Dialog.Ok | Bigscreen.Dialog.Cancel

                        onOpened: nameTextField.forceActiveFocus()
                        onClosed: changeNameButton.forceActiveFocus()
                        onAccepted: {
                            device.name = nameTextField.text
                            changeNameDialog.close()
                        }

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Bigscreen.TextField {
                                id: nameTextField
                                text: device ? device.name : ""
                                placeholderText: device ? device.name : ""
                                Keys.onReturnPressed: changeNameDialog.accept()
                                KeyNavigation.down: changeNameDialog.footer
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }
}
