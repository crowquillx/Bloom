#pragma once

#include "../viewmodels/PersonViewModel.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes PersonViewModel to BloomUI as a creatable type: each Person page
 * owns one, so stacked person pages keep their own state. The view model
 * itself stays free of QtQml so tests link it without the QML module.
 */
struct PersonViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(PersonViewModel)
    QML_NAMED_ELEMENT(PersonViewModel)
};

/// The per-shelf title models PersonViewModel exposes (not creatable).
struct PersonTitlesModelRegistration
{
    Q_GADGET
    QML_FOREIGN(PersonTitlesModel)
    QML_ANONYMOUS
};
