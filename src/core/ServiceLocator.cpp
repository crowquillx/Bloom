#include "ServiceLocator.h"

// ServiceLocator is a header-only implementation using templates and static methods.
// This .cpp file exists for potential future extensions such as:
// - Lifecycle management hooks (initialize/cleanup callbacks)
// - Debug logging of service registration/retrieval
// - Service dependency graph validation
// - Runtime service replacement for testing

// Note: The static storage (services hash and mutex) is defined inline in the header
// using Meyer's singleton pattern to ensure thread-safe initialization while
// maintaining header-only simplicity.
