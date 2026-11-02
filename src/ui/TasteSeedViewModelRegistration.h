#pragma once

#include "../viewmodels/TasteSeedViewModels.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes the taste seeding view models to BloomUI as creatable types: Home
 * owns one TasteSeedPromptViewModel, each picker page one TasteSeedViewModel.
 * The view models themselves stay free of QtQml so tests link them without
 * the QML module.
 */
struct TasteSeedPromptViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(TasteSeedPromptViewModel)
    QML_NAMED_ELEMENT(TasteSeedPromptViewModel)
};

struct TasteSeedViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(TasteSeedViewModel)
    QML_NAMED_ELEMENT(TasteSeedViewModel)
};
