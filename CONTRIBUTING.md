# Contributing to Bloom

## Accessibility Guidelines

To ensure Bloom remains accessible to all users, including those using screen readers (10-foot UI with accessibility support), follow these guidelines when creating or modifying QML components.

### Mandatory Properties

All interactive elements must have the following properties:

1.  **`Accessible.role`**: Define the semantic role of the control.
2.  **`Accessible.name`**: A concise label for the control. Use `AccessibilityHelpers.a11yName()` for safety.
3.  **`Accessible.description`**: (Optional but recommended) Provide additional context for complex actions.

### Implementation Patterns

#### Buttons
Use `A11yButton` when possible, or annotate standard `Button` components:
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
    Accessible.name: modelData.Name
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

#### Helper Singleton
Use `AccessibilityHelpers` for consistent roles and utility functions:
```qml
import "." // or appropriate import

Accessible.role: AccessibilityHelpers.roleButton
```

### Validation
- Run `qmllint` on your changes.
- Use **Qt Accessibility Inspector** to verify roles and names are correctly exposed.
- Ensure keyboard navigation remains logical and focus is never trapped.
