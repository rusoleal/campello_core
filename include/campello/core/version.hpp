#pragma once

// Campello Core — Version information
//
// Semantic versioning: MAJOR.MINOR.PATCH

#define CAMPELLO_CORE_VERSION_MAJOR 0
#define CAMPELLO_CORE_VERSION_MINOR 1
#define CAMPELLO_CORE_VERSION_PATCH 0

#define CAMPELLO_CORE_VERSION \
    ((CAMPELLO_CORE_VERSION_MAJOR * 10000) + \
     (CAMPELLO_CORE_VERSION_MINOR * 100)   + \
      CAMPELLO_CORE_VERSION_PATCH)

namespace campello::core {

inline constexpr int version_major = CAMPELLO_CORE_VERSION_MAJOR;
inline constexpr int version_minor = CAMPELLO_CORE_VERSION_MINOR;
inline constexpr int version_patch = CAMPELLO_CORE_VERSION_PATCH;
inline constexpr int version       = CAMPELLO_CORE_VERSION;

} // namespace campello::core
