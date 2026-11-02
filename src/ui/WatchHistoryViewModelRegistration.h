#pragma once

#include "../viewmodels/WatchHistoryViewModel.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes WatchHistoryViewModel to BloomUI as a creatable type: each History
 * page owns one. The view model itself stays free of QtQml so tests link it
 * without the QML module.
 */
struct WatchHistoryViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(WatchHistoryViewModel)
    QML_NAMED_ELEMENT(WatchHistoryViewModel)
};
