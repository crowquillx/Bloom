#pragma once

#include "../viewmodels/ContinueWatchingViewModel.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes ContinueWatchingViewModel to BloomUI as a creatable type: each
 * Home owns one, so a new Home (sign-in, space switch) starts empty. The
 * view model itself stays free of QtQml so tests link it without the QML
 * module.
 */
struct ContinueWatchingViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(ContinueWatchingViewModel)
    QML_NAMED_ELEMENT(ContinueWatchingViewModel)
};

// The shelves' list models, read through ContinueWatchingViewModel only.
struct HomeCardListModelRegistration
{
    Q_GADGET
    QML_FOREIGN(HomeCardListModel)
    QML_ANONYMOUS
};
