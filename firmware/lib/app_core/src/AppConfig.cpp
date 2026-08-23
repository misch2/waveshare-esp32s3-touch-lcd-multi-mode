#include "AppConfig.h"

#include <cstring>

namespace app_core {
namespace {

constexpr char kClockScreenId[] = "clock.dashboard";
constexpr char kRadarScreenId[] = "meteo.radar";

std::size_t boundedLength(const char* text, std::size_t limit) {
    if (text == nullptr) {
        return 0;
    }

    std::size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool isValidId(const char* id) {
    if (id == nullptr) {
        return false;
    }

    const std::size_t length =
        boundedLength(id, AppConfig::kScreenIdStorage);
    if (length == 0 || length > AppConfig::kMaxScreenIdLength) {
        return false;
    }

    for (std::size_t i = 0; i < length; ++i) {
        // Keep the wire representation predictable while allowing module
        // namespaces beyond the built-in dot-separated IDs.
        const unsigned char value = static_cast<unsigned char>(id[i]);
        if (value <= 0x20 || value >= 0x7f) {
            return false;
        }
    }
    return true;
}

bool sameId(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return std::strncmp(left, right, AppConfig::kScreenIdStorage) == 0;
}

void copyId(char (&destination)[AppConfig::kScreenIdStorage],
            const char* source) {
    std::size_t i = 0;
    if (source != nullptr) {
        while (i < AppConfig::kMaxScreenIdLength && source[i] != '\0') {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
    ++i;
    while (i < AppConfig::kScreenIdStorage) {
        destination[i] = '\0';
        ++i;
    }
}

void clearScreen(AppConfig::Screen& screen) {
    std::memset(&screen, 0, sizeof(screen));
}

bool sameScreen(const AppConfig::Screen& left,
                const AppConfig::Screen& right) {
    return std::memcmp(&left, &right, sizeof(AppConfig::Screen)) == 0;
}

}  // namespace

// Out-of-class definitions keep the constants linkable when a native test
// odr-uses them while remaining compatible with the C++11 Arduino toolchain.
constexpr std::uint16_t AppConfig::kSchemaVersion;
constexpr std::uint8_t AppConfig::kMaxScreens;
constexpr std::size_t AppConfig::kMaxScreenIdLength;
constexpr std::size_t AppConfig::kScreenIdStorage;
constexpr std::uint16_t AppConfig::kDefaultAutoRotateSeconds;
constexpr std::uint16_t AppConfig::kMaxAutoRotateSeconds;

AppConfig AppConfig::defaults() {
    AppConfig config{};
    config.schemaVersion = kSchemaVersion;
    config.screenCount = 2;
    config.autoRotateSeconds = kDefaultAutoRotateSeconds;

    copyId(config.screens[0].id, kClockScreenId);
    config.screens[0].enabled = 1;
    copyId(config.screens[1].id, kRadarScreenId);
    config.screens[1].enabled = 1;

    for (std::uint8_t i = config.screenCount; i < kMaxScreens; ++i) {
        clearScreen(config.screens[i]);
    }
    return config;
}

bool AppConfig::normalize() {
    bool changed = false;

    if (schemaVersion != kSchemaVersion) {
        schemaVersion = kSchemaVersion;
        changed = true;
    }
    if (autoRotateSeconds > kMaxAutoRotateSeconds) {
        autoRotateSeconds = kMaxAutoRotateSeconds;
        changed = true;
    }

    const std::uint8_t inputCount =
        screenCount <= kMaxScreens ? screenCount : kMaxScreens;
    Screen normalized[kMaxScreens]{};
    std::uint8_t normalizedCount = 0;

    for (std::uint8_t i = 0; i < inputCount; ++i) {
        const Screen& candidate = screens[i];
        if (!isValidId(candidate.id)) {
            changed = true;
            continue;
        }

        bool duplicate = false;
        for (std::uint8_t j = 0; j < normalizedCount; ++j) {
            if (sameId(normalized[j].id, candidate.id)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            changed = true;
            continue;
        }

        copyId(normalized[normalizedCount].id, candidate.id);
        normalized[normalizedCount].enabled = candidate.enabled ? 1 : 0;
        if (normalized[normalizedCount].enabled != candidate.enabled) {
            changed = true;
        }
        ++normalizedCount;
    }

    if (normalizedCount == 0) {
        copyId(normalized[0].id, kClockScreenId);
        normalized[0].enabled = 1;
        copyId(normalized[1].id, kRadarScreenId);
        normalized[1].enabled = 1;
        normalizedCount = 2;
        changed = true;
    }

    bool anyEnabled = false;
    for (std::uint8_t i = 0; i < normalizedCount; ++i) {
        anyEnabled = anyEnabled || normalized[i].enabled != 0;
    }
    if (!anyEnabled) {
        normalized[0].enabled = 1;
        changed = true;
    }

    if (screenCount != normalizedCount || screenCount > kMaxScreens) {
        changed = true;
    }

    for (std::uint8_t i = 0; i < kMaxScreens; ++i) {
        const Screen& next = i < normalizedCount ? normalized[i] : Screen{};
        if (!sameScreen(screens[i], next)) {
            changed = true;
        }
        screens[i] = next;
    }
    screenCount = normalizedCount;

    return changed;
}

bool AppConfig::validate() const {
    if (schemaVersion != kSchemaVersion || screenCount == 0 ||
        screenCount > kMaxScreens ||
        autoRotateSeconds > kMaxAutoRotateSeconds) {
        return false;
    }

    bool anyEnabled = false;
    for (std::uint8_t i = 0; i < screenCount; ++i) {
        if (!isValidId(screens[i].id) || screens[i].enabled > 1) {
            return false;
        }
        for (std::uint8_t j = 0; j < i; ++j) {
            if (sameId(screens[i].id, screens[j].id)) {
                return false;
            }
        }
        anyEnabled = anyEnabled || screens[i].enabled != 0;
    }
    return anyEnabled;
}

std::int8_t AppConfig::findScreen(const char* id) const {
    if (!isValidId(id)) {
        return -1;
    }

    for (std::uint8_t i = 0; i < screenCount && i < kMaxScreens; ++i) {
        if (sameId(screens[i].id, id)) {
            return static_cast<std::int8_t>(i);
        }
    }
    return -1;
}

bool AppConfig::isEnabled(const char* id) const {
    const std::int8_t index = findScreen(id);
    return index >= 0 && screens[static_cast<std::uint8_t>(index)].enabled != 0;
}

bool AppConfig::setEnabled(const char* id, bool enabled) {
    const std::int8_t index = findScreen(id);
    if (index < 0) {
        return false;
    }

    screens[static_cast<std::uint8_t>(index)].enabled = enabled ? 1 : 0;
    return true;
}

bool AppConfig::moveScreen(const char* id, std::uint8_t targetIndex) {
    const std::int8_t sourceIndex = findScreen(id);
    if (sourceIndex < 0 || targetIndex >= screenCount) {
        return false;
    }

    const std::uint8_t source = static_cast<std::uint8_t>(sourceIndex);
    if (source == targetIndex) {
        return true;
    }

    const Screen moved = screens[source];
    if (source < targetIndex) {
        for (std::uint8_t i = source; i < targetIndex; ++i) {
            screens[i] = screens[static_cast<std::uint8_t>(i + 1)];
        }
    } else {
        for (std::uint8_t i = source; i > targetIndex; --i) {
            screens[i] = screens[static_cast<std::uint8_t>(i - 1)];
        }
    }
    screens[targetIndex] = moved;
    return true;
}

}  // namespace app_core
