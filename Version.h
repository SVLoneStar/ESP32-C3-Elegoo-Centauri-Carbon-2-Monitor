#pragma once

// Arduino IDE does not provide a project-local pre-build hook for Git metadata.
// Update the hash and manual dirty flag as described in README.md before builds.
#define FIRMWARE_VERSION "0.1.0-dev"
#define FIRMWARE_GIT_HASH "9c3221c"
#define FIRMWARE_BUILD_DIRTY 0
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

#if FIRMWARE_BUILD_DIRTY
#define FIRMWARE_GIT_REVISION FIRMWARE_GIT_HASH "+"
#define FIRMWARE_BUILD_STATE "DIRTY"
#else
#define FIRMWARE_GIT_REVISION FIRMWARE_GIT_HASH
#define FIRMWARE_BUILD_STATE "CLEAN"
#endif

#define FIRMWARE_IDENTIFIER "FW " FIRMWARE_VERSION " / " FIRMWARE_GIT_REVISION
#define FIRMWARE_COMPACT_IDENTIFIER "FW" FIRMWARE_VERSION "/" FIRMWARE_GIT_REVISION
