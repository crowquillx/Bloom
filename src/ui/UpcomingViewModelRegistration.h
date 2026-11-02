#pragma once

#include "../viewmodels/UpcomingViewModel.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes UpcomingViewModel to BloomUI as a creatable type: each Upcoming
 * page owns one. The view model itself stays free of QtQml so tests link it
 * without the QML module.
 */
struct UpcomingViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(UpcomingViewModel)
    QML_NAMED_ELEMENT(UpcomingViewModel)
};
