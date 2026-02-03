#pragma once

#include "ISecretStore.h"
#include <memory>

/**
 * @brief Factory for creating platform-specific SecretStore implementations
 */
class SecretStoreFactory {
public:
    /**
     * @brief Create the appropriate SecretStore for the current platform
     * @return Unique pointer to ISecretStore implementation
     */
    static std::unique_ptr<ISecretStore> create();
};
