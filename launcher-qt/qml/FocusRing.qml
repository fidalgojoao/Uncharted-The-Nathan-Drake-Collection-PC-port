import QtQuick

// The controller / keyboard focus: a yellow outline around the focused control (navigator.current), drawn only while
// a controller or the keyboard is in use (navigator.shown), so the window looks the same for the mouse.
Rectangle {
    property real gap: 3   // outside the control; negative: inside (a control in a clipped box)
    property real round: parent && parent.radius !== undefined ? parent.radius : 8
    anchors.fill: parent
    anchors.margins: -gap
    radius: Math.max(0, round + gap)
    color: "transparent"
    border.color: Theme.goldHi
    border.width: 2
    visible: navigator.shown && navigator.current === parent
    z: 100
}
