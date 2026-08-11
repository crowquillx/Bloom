#pragma once

#include "SoftwareRoundedImage.h"

#include <QtQml/qqmlregistration.h>

/**
 * Exposes the production-owned SoftwareRoundedImage type to BloomUI without
 * rebuilding its implementation or QObject metaobject in each QML module.
 */
struct SoftwareRoundedImageRegistration
{
    Q_GADGET
    QML_FOREIGN(SoftwareRoundedImage)
    QML_NAMED_ELEMENT(SoftwareRoundedImage)
};
