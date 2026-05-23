#include <gtest/gtest.h>
#include "core.h"

TEST(Core, AddWorks)
{
    EXPECT_EQ(add(2, 3), 5);
}
