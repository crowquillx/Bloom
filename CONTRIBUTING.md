# Contributing to Bloom

## Accessibility Guidelines

To ensure Bloom remains accessible to all users, including those using screen readers (10-foot UI with accessibility support), follow these guidelines when creating or modifying QML components.

### Mandatory Properties

All interactive elements must have the following properties:

1.  **`Accessible.role`**: Define the semantic role of the control.
2.  **`Accessible.name`**: A concise label for the control.
3.  **`Accessible.description`**: (Optional but recommended) Provide additional context for complex actions.

### Implementation Patterns

#### Buttons
Use the shared `ActionButton` (it sets `Accessible.role`, `Accessible.name` from
`text` or `accessibleLabel`, and a busy description), or annotate standard
`Button` components:
```qml
Button {
    text: "Play"
    Accessible.role: Accessible.Button
    Accessible.name: text
}
```

#### List Delegates
Always providing dynamic names based on the model data:
```qml
ItemDelegate {
    Accessible.role: Accessible.ListItem
    Accessible.name: modelData.name
    Accessible.description: "Press to view details"
}
```

#### Headings
Annotate section titles to provide structure:
```qml
Text {
    text: "Recently Added"
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
```

#### Shared Components
Prefer the shared components in `docs/theme.md` (`ActionButton`, `BloomMenu`,
`DialogSurface`, `StateMessage`, ...), which already expose roles and names.

### Validation
- Run `qmllint` on your changes.
- Use **Qt Accessibility Inspector** to verify roles and names are correctly exposed.
- Ensure keyboard navigation remains logical and focus is never trapped.
