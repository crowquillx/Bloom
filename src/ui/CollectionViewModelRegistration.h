#pragma once

#include "../viewmodels/CollectionViewModels.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes the collection view models to BloomUI as creatable types: each
 * library page, collection page and detail view owns its own. The view
 * models stay free of QtQml so tests link them without the QML module.
 */
struct LibraryCollectionsViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(LibraryCollectionsViewModel)
    QML_NAMED_ELEMENT(LibraryCollectionsViewModel)
};

struct CollectionViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(CollectionViewModel)
    QML_NAMED_ELEMENT(CollectionViewModel)
};

struct ItemCollectionsViewModelRegistration
{
    Q_GADGET
    QML_FOREIGN(ItemCollectionsViewModel)
    QML_NAMED_ELEMENT(ItemCollectionsViewModel)
};
