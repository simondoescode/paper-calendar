#pragma once

#include <stdint.h>

namespace calendar {

enum class OtaCheckResult : uint8_t {
  Skipped,
  UpToDate,
  UpdateInstalled,
  Failed,
};

OtaCheckResult checkForFirmwareUpdate(bool force, uint32_t nowEpoch);

} // namespace calendar
