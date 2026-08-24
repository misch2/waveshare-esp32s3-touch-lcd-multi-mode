#pragma once

#include <cstddef>
#include <cstdint>

namespace app_core {

/**
 * Versioned application configuration shared by the host and screen
 * modules.  The object deliberately contains only fixed-size fields so it
 * can be stored or copied without a heap allocation and can be tested on a
 * desktop build without Arduino headers.
 */
struct AppConfig {
    static constexpr std::uint16_t kSchemaVersion = 3;
    static constexpr std::uint8_t kMaxScreens = 8;
    static constexpr std::size_t kMaxScreenIdLength = 31;
    static constexpr std::size_t kScreenIdStorage = kMaxScreenIdLength + 1;

    struct Screen {
        // A non-empty printable ASCII ID (without whitespace), max 31 chars.
        char id[kScreenIdStorage];
        std::uint8_t enabled;
    };

    std::uint16_t schemaVersion;
    std::uint8_t screenCount;
    Screen screens[kMaxScreens];

    /** Return the initial configuration for a new device. */
    static AppConfig defaults();

    /**
     * Repair fields that can safely be repaired in-place.
     *
     * Invalid and duplicate screen entries are removed while preserving the
     * order of the remaining entries. Unknown but syntactically valid IDs
     * are retained, which keeps configuration forward-compatible with new
     * modules. If no valid screen remains, the built-in defaults are
     * installed. Missing built-in screens are appended in default order so
     * older persisted configurations learn about newly integrated modules.
     * The return value is true when any field was changed.
     */
    bool normalize();

    /** Check the structural invariants without modifying the object. */
    bool validate() const;

    /** Return the zero-based ordered index, or -1 when the ID is absent. */
    std::int8_t findScreen(const char* id) const;

    /** Return false for an absent or disabled screen. */
    bool isEnabled(const char* id) const;

    /** Enable or disable an existing screen. Returns false for an unknown ID. */
    bool setEnabled(const char* id, bool enabled);

    /**
     * Move an existing screen to an absolute ordered index. The other entries
     * shift to make room. Returns false for an unknown ID or out-of-range
     * target index.
     */
    bool moveScreen(const char* id, std::uint8_t targetIndex);
};

}  // namespace app_core
