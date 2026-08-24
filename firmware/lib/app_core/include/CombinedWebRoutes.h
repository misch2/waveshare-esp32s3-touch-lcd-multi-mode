#pragma once

#include <cstddef>

// This header is deliberately independent of Arduino, WebServer and
// ArduinoJson.  Combined firmware updates are manual-only; update discovery
// and installation are intentionally outside this route contract.
namespace app_core {

inline constexpr char COMBINED_WEB_STATUS_PATH[] = "/api/status";
inline constexpr char COMBINED_WEB_DIAGNOSTICS_PATH[] = "/api/diagnostics";
inline constexpr char COMBINED_WEB_EXPORT_PATH[] = "/api/config/export";
inline constexpr char COMBINED_WEB_IMPORT_PATH[] = "/api/config/import";
// The generated combined envelope is well below this bound. Keeping the
// request small matters because WebServer materializes the POST body as an
// internal-RAM String before the route callback runs.
inline constexpr std::size_t COMBINED_WEB_MAX_IMPORT_BYTES = 16384;

// A load callback writes a complete JSON response to `out` and returns its
// byte count.  The route adapter owns the bounded-buffer error handling.
using CombinedJsonLoadCallback = std::size_t (*)(char* out,
                                                 std::size_t capacity);

// An import callback validates and applies one complete JSON payload.  On
// failure it may write a concise Czech/detail message to `message`.
using CombinedJsonImportCallback = bool (*)(const char* json,
                                            std::size_t length,
                                            char* message,
                                            std::size_t messageCapacity);

using CombinedAccessCallback = bool (*)();
using CombinedStorageBeginCallback = bool (*)();
using CombinedStorageEndCallback = bool (*)();

struct CombinedWebRoutes {
  CombinedJsonLoadCallback loadStatus = nullptr;
  CombinedJsonLoadCallback loadDiagnostics = nullptr;
  CombinedJsonLoadCallback loadExport = nullptr;
  CombinedJsonImportCallback validateImport = nullptr;
  CombinedJsonImportCallback importConfig = nullptr;
  CombinedAccessCallback accessAllowed = nullptr;

  // Configuration import writes multiple namespaces and must bracket them
  // with the paired display-safe storage callbacks when supplied.
  CombinedStorageBeginCallback storageBegin = nullptr;
  CombinedStorageEndCallback storageEnd = nullptr;

  bool hasStatusCallback() const noexcept { return loadStatus != nullptr; }

  bool hasDiagnosticsCallback() const noexcept {
    return loadDiagnostics != nullptr;
  }

  bool hasExportCallback() const noexcept { return loadExport != nullptr; }

  bool hasImportValidationCallback() const noexcept {
    return validateImport != nullptr;
  }

  bool hasImportCallback() const noexcept { return importConfig != nullptr; }

  bool hasStorageCallbacks() const noexcept {
    return storageBegin != nullptr && storageEnd != nullptr;
  }
};

using CombinedWebOptions = CombinedWebRoutes;

}  // namespace app_core
