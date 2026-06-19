/*
    SPDX-FileCopyrightText: 2019 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2013-2017 Jan Grulich <jgrulich@redhat.com>
    SPDX-FileCopyrightText: 2025 Seshan Ravikumar <seshan@sineware.ca>
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

import QtQuick
import QtQuick.Layouts
import org.kde.bigscreen as Bigscreen

AbstractIndicator {
    id: button
    icon.name: Bigscreen.Global.launchReason === "swap" ? "window-close" : "system-shutdown"
    text: (Bigscreen.Global.launchReason === "swap") ? i18n("Exit Bigscreen") : i18n("Power")

    Bigscreen.Dialog {
        id: powerDialog
        title: i18n("Power")
        openFocusItem: turnScreenOffButton

        contentItem: ColumnLayout {
            spacing: 0

            Bigscreen.ButtonDelegate {
                id: turnScreenOffButton
                Layout.fillWidth: true
                icon.name: "video-display"
                text: i18n("Turn Screen Off")
                description: i18n("Keep apps and downloads running while the TV or monitor enters standby.")
                KeyNavigation.down: powerOptionsButton

                onClicked: {
                    powerDialog.close();
                    Bigscreen.Global.turnOffScreen();
                }
            }

            Bigscreen.ButtonDelegate {
                id: powerOptionsButton
                Layout.fillWidth: true
                icon.name: "system-shutdown"
                text: i18n("Power Options")
                description: i18n("Shut down, restart, suspend, or log out.")
                KeyNavigation.up: turnScreenOffButton

                onClicked: {
                    powerDialog.close();
                    Bigscreen.Global.promptLogoutGreeter("promptAll");
                }
            }
        }
    }

    onClicked: (event)=> {
        if (Bigscreen.Global.launchReason === "swap") {
            Bigscreen.Global.swapSession();
        } else {
            powerDialog.open();
        }
    }
}
