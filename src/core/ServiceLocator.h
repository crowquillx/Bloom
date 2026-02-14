#pragma once

#include <QObject>
#include <QMutex>
#include <QHash>
#include <QString>
#include <typeinfo>
#include <stdexcept>
#if defined(__cpp_concepts)
#include <concepts>
#else
#include <type_traits>
#endif

/**
 * @brief Thread-safe Service Locator for managing application-wide dependencies.
 * 
 * The ServiceLocator provides a centralized registry for services used throughout
 * the application. It uses a type-safe template interface while storing services
 * as QObject pointers internally.
 * 
 * ## Initialization Order Dependencies
 * 
 * Services must be registered in the following order due to dependencies:
 * 
 * 1. **ConfigManager** - No dependencies, loads configuration
 * 2. **IPlayerBackend** - No dependencies, active playback backend implementation
 * 3. **AuthenticationService** - No dependencies, handles authentication/session
 * 4. **LibraryService** - Depends on: AuthenticationService (library/data APIs)
 * 5. **PlaybackService** - Depends on: AuthenticationService (playback/reporting APIs)
 * 6. **PlayerController** - Depends on: IPlayerBackend, ConfigManager, TrackPreferencesManager, DisplayManager, LibraryService, PlaybackService, AuthenticationService
 * 7. **InputModeManager** - Depends on: QGuiApplication (passed at construction)
 * 
 * ## Usage Example
 * 
 * ```cpp
 * // Registration (in main.cpp)
 * ServiceLocator::registerService<ConfigManager>(configManager);
 * ServiceLocator::registerService<AuthenticationService>(authService);
 * 
 * // Retrieval (anywhere in the application)
 * auto* auth = ServiceLocator::get<AuthenticationService>();
 * ```
 * 
 * ## Thread Safety
 * 
 * All methods are thread-safe and can be called from any thread.
 */
class ServiceLocator
{
public:
    /**
     * @brief Registers a service instance with the locator.
     * 
     * @tparam T The service type (must inherit from QObject)
     * @param service Pointer to the service instance (ownership is NOT transferred)
     * @throws std::runtime_error if a service of the same type is already registered
     */
    template<typename T>
#if defined(__cpp_concepts)
        requires std::derived_from<T, QObject>
#endif
    static void registerService(T* service)
    {
#if !defined(__cpp_concepts)
        static_assert(std::is_base_of_v<QObject, T>, "Service must inherit from QObject");
#endif

        QMutexLocker locker(&mutex());
        QString typeName = QString::fromLatin1(typeid(T).name());
        
        if (services().contains(typeName)) {
            throw std::runtime_error(
                QString("Service '%1' is already registered").arg(typeName).toStdString()
            );
        }
        
        services().insert(typeName, static_cast<QObject*>(service));
    }
    
    /**
     * @brief Retrieves a registered service.
     * 
     * @tparam T The service type to retrieve
     * @return Pointer to the service instance
     * @throws std::runtime_error if the service is not registered
     */
    template<typename T>
#if defined(__cpp_concepts)
        requires std::derived_from<T, QObject>
#endif
    static T* get()
    {
#if !defined(__cpp_concepts)
        static_assert(std::is_base_of_v<QObject, T>, "Service must inherit from QObject");
#endif

        QMutexLocker locker(&mutex());
        QString typeName = QString::fromLatin1(typeid(T).name());
        
        auto it = services().find(typeName);
        if (it == services().end()) {
            throw std::runtime_error(
                QString("Service '%1' is not registered").arg(typeName).toStdString()
            );
        }
        
        return qobject_cast<T*>(it.value());
    }
    
    /**
     * @brief Tries to retrieve a registered service without throwing.
     * 
     * @tparam T The service type to retrieve
     * @return Pointer to the service instance, or nullptr if not registered
     */
    template<typename T>
#if defined(__cpp_concepts)
        requires std::derived_from<T, QObject>
#endif
    static T* tryGet()
    {
#if !defined(__cpp_concepts)
        static_assert(std::is_base_of_v<QObject, T>, "Service must inherit from QObject");
#endif

        QMutexLocker locker(&mutex());
        QString typeName = QString::fromLatin1(typeid(T).name());
        
        auto it = services().find(typeName);
        if (it == services().end()) {
            return nullptr;
        }
        
        return qobject_cast<T*>(it.value());
    }
    
    /**
     * @brief Checks if a service is registered.
     * 
     * @tparam T The service type to check
     * @return true if the service is registered, false otherwise
     */
    template<typename T>
#if defined(__cpp_concepts)
        requires std::derived_from<T, QObject>
#endif
    static bool isRegistered()
    {
#if !defined(__cpp_concepts)
        static_assert(std::is_base_of_v<QObject, T>, "Service must inherit from QObject");
#endif

        QMutexLocker locker(&mutex());
        QString typeName = QString::fromLatin1(typeid(T).name());
        return services().contains(typeName);
    }
    
    /**
     * @brief Unregisters a service from the locator.
     * 
     * @tparam T The service type to unregister
     * @note This does NOT delete the service instance
     */
    template<typename T>
#if defined(__cpp_concepts)
        requires std::derived_from<T, QObject>
#endif
    static void unregister()
    {
#if !defined(__cpp_concepts)
        static_assert(std::is_base_of_v<QObject, T>, "Service must inherit from QObject");
#endif

        QMutexLocker locker(&mutex());
        QString typeName = QString::fromLatin1(typeid(T).name());
        services().remove(typeName);
    }
    
    /**
     * @brief Clears all registered services.
     * 
     * @note This does NOT delete the service instances. Call this during
     * application shutdown after all services are no longer needed.
     */
    static void clear()
    {
        QMutexLocker locker(&mutex());
        services().clear();
    }

private:
    // Private constructor to prevent instantiation
    ServiceLocator() = default;
    ~ServiceLocator() = default;
    
    // Static storage for services (Meyer's singleton pattern for thread-safe initialization)
    static QHash<QString, QObject*>& services()
    {
        static QHash<QString, QObject*> s_services;
        return s_services;
    }
    
    static QMutex& mutex()
    {
        static QMutex s_mutex;
        return s_mutex;
    }
};
