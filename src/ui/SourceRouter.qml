import QtQuick

// Runs an action once the live session belongs to the connection that owns
// an item. In merged workspaces details, playback and writes must reach the
// owning server; single workspaces run the action immediately.
Item {
    id: router

    property var pendingAction: null
    property string pendingConnectionId: ""
    readonly property bool busy: pendingAction !== null

    signal failed(string message)

    function run(connectionId, action) {
        if (!connectionId || typeof Workspaces === "undefined" || !Workspaces.merged) {
            action()
            return
        }
        // A newer selection replaces one still waiting for its switch.
        pendingAction = null
        pendingConnectionId = connectionId
        if (Workspaces.routeTo(connectionId)) {
            pendingConnectionId = ""
            action()
            return
        }
        if (pendingConnectionId === connectionId)
            pendingAction = action
    }

    function cancel() {
        pendingAction = null
        pendingConnectionId = ""
    }

    Connections {
        target: typeof Workspaces !== "undefined" ? Workspaces : null
        ignoreUnknownSignals: true

        function onRouteReady(connectionId) {
            if (connectionId !== router.pendingConnectionId || !router.pendingAction)
                return
            var action = router.pendingAction
            router.cancel()
            action()
        }

        function onRouteFailed(connectionId, error) {
            if (connectionId !== router.pendingConnectionId)
                return
            router.cancel()
            router.failed(error)
        }
    }
}
