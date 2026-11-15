/**
 * @file main.cpp
 * @brief Shared gtest entry point — linked into every test executable.
 */

#include <gtest/gtest.h>

int
main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
