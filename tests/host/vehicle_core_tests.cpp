#include <doctest/doctest.h>

#include "vehicle_core/vehicle_core.hpp"

TEST_CASE("vehicle_core smoke API is linkable") {
  CHECK(vehicle_core::kApiVersion == 1);
  CHECK(vehicle_core::library_is_available());
}
