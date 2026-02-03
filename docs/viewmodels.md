# ViewModel Architecture

Guidance for standardizing ViewModels with shared loading/error state and consistent QML bindings.

## BaseViewModel (C++)
- Inherits `QAbstractListModel`; exposes `isLoading`, `hasError`, `errorMessage` (read-only, QML-visible).
- Signals: `isLoadingChanged`, `hasErrorChanged`, `errorMessageChanged`.
- Standard retry hook: `Q_INVOKABLE reload()` (no-op by default; override to call your load/refresh entry point).
- Helpers:
  - `setLoading(bool)` to flip busy state (emits change once).
  - `setError(QString)` / `clearError()` to manage error flags and text.
  - `emitModelReset(fn)` wraps `beginResetModel`/`endResetModel` around a mutator.
  - `mapNetworkError(endpoint, error)` maps common service failures to user-friendly strings.
  - `setBusyWhile(QFutureWatcherBase&)` for Qt Concurrent tasks (auto-clears loading on finish/cancel).

## Migration guidance
- Derive list-style ViewModels from `BaseViewModel` (e.g., `LibraryViewModel`, `SeriesDetailsViewModel`).
- Remove bespoke `isLoading`/`errorMessage` flags and use the base helpers.
- Route network callbacks through `setError(mapNetworkError(...))` and clear errors before new loads.
- Use `emitModelReset([&]{ ... })` when replacing model contents; use `dataChanged` for partial updates.
- Keep role definitions and business logic localized; only loading/error plumbing moves to the base.

## QML usage
- Bind spinners/toasts to `isLoading`/`hasError`/`errorMessage` without custom glue per ViewModel.
- After model reloads that affect focusable delegates, use `Qt.callLater` to restore focus per `docs/developer_notes.md`.
- Retry pattern (with shared `RetryButton.qml`):
```qml
Column {
    BusyIndicator { running: model.isLoading; visible: running }
    ToastNotification {
        visible: model.hasError
        text: model.errorMessage
    }
    RetryButton {
        target: model        // any BaseViewModel-derived instance
        visible: model.hasError
    }
}
```

## Current migrations
- `LibraryViewModel` now inherits `BaseViewModel`; loading/error wiring is centralized while pagination, caching, and signals (`loadComplete`, `loadMoreComplete`) remain intact.
- `SeriesDetailsViewModel` inherits `BaseViewModel` for standardized loading/error properties; nested `SeasonsModel`/`EpisodesModel` keep their role-focused implementations.

## Async work and setBusyWhile
Use `setBusyWhile` to keep loading flags accurate for Qt Concurrent tasks:
```cpp
class ArtworkViewModel : public BaseViewModel {
    Q_OBJECT
public:
    Q_INVOKABLE void reload() override {
        watcher.setFuture(QtConcurrent::run([=]{
            // expensive work here
            return 42;
        }));
        setBusyWhile(watcher);
    }
private:
    QFutureWatcher<int> watcher;
};
```







