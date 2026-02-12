#pragma once

#include "network/AuthenticationService.h"

/**
 * @brief Mock implementation of AuthenticationService for visual regression testing.
 * 
 * This service simulates an authenticated session without making network requests.
 * It returns a deterministic authenticated state for consistent UI testing.
 */
class MockAuthenticationService : public AuthenticationService
{
    Q_OBJECT

public:
    explicit MockAuthenticationService(ISecretStore *secretStore = nullptr, QObject *parent = nullptr);
    
    /**
     * @brief Initialize the mock service with pre-authenticated state.
     * @param configManager Pointer to ConfigManager (unused in mock).
     */
    void initialize(ConfigManager *configManager) override;

    /**
     * @brief Simulate authentication (immediately succeeds).
     * @param serverUrl Jellyfin server URL (stored but not used).
     * @param username User's username (stored but not used).
     * @param password User's password (ignored).
     */
    Q_INVOKABLE void authenticate(const QString &serverUrl, const QString &username, const QString &password) override;
    
    /**
     * @brief Simulate session restoration (immediately succeeds).
     */
    void restoreSession(const QString &serverUrl, const QString &userId, const QString &accessToken) override;
    
    /**
     * @brief Simulate logout (clears authentication state).
     */
    Q_INVOKABLE void logout() override;
    
    /**
     * @brief Check for pending session expiry (no-op in mock).
     */
    Q_INVOKABLE void checkPendingSessionExpiry() override;
};
