#include <gtest/gtest.h>

#include "util/DockSleepPolicy.h"

TEST(DockSleepPolicy, BatteryTimeout) {
  DockSleepPolicy policy;
  EXPECT_FALSE(policy.shouldSleep(999, 0, 1000, false));
  EXPECT_TRUE(policy.shouldSleep(1000, 0, 1000, false));
  EXPECT_FALSE(policy.shouldSleep(1000, 500, 1000, false));
}

TEST(DockSleepPolicy, PoweredAndUnplugged) {
  DockSleepPolicy policy;
  EXPECT_FALSE(policy.shouldSleep(100000, 0, 1000, true));
  EXPECT_FALSE(policy.shouldSleep(100999, 0, 1000, false));
  EXPECT_TRUE(policy.shouldSleep(101000, 0, 1000, false));
}

TEST(DockSleepPolicy, NeverSleep) {
  DockSleepPolicy policy;
  EXPECT_FALSE(policy.shouldSleep(100000, 0, 0, true));
  EXPECT_FALSE(policy.shouldSleep(200000, 0, 0, false));
}

TEST(DockSleepPolicy, Rollover) {
  DockSleepPolicy policy;
  EXPECT_FALSE(policy.shouldSleep(UINT32_MAX - 100, UINT32_MAX - 200, 1000, true));
  EXPECT_FALSE(policy.shouldSleep(500, UINT32_MAX - 200, 1000, false));
  EXPECT_TRUE(policy.shouldSleep(1000, UINT32_MAX - 200, 1000, false));
}

TEST(DockSleepPolicy, DockDoesNotReplaceUserActivity) {
  DockSleepPolicy policy;
  EXPECT_FALSE(policy.shouldSleep(10000, 0, 1000, true));
  EXPECT_FALSE(policy.shouldSleep(11000, 10500, 1000, false));
  EXPECT_TRUE(policy.shouldSleep(11500, 10500, 1000, false));
}
