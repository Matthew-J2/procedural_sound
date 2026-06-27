#include <gtest/gtest.h>
#include <core.h>

TEST(RingBuffer, PushPopValue) {
    SPSCRingBuffer<int> buffer(4);
    EXPECT_TRUE(buffer.push(42));
    
    int out = 0;
    EXPECT_TRUE(buffer.pop(out));
    EXPECT_EQ(out, 42);
}

TEST(RingBuffer, PopOnEmpty) {
    SPSCRingBuffer<int> buffer(4);
    int out = 0;
    EXPECT_FALSE(buffer.pop(out));
}

TEST(RingBuffer, IsFIFO) {
    SPSCRingBuffer<int> buffer(4);

   for (int i = 0; i < 3; i++)
        EXPECT_TRUE(buffer.push(i));

    for (int i = 0; i < 3; i++) {
        int out = -1;
        EXPECT_TRUE(buffer.pop(out));
        EXPECT_EQ(out, i);
    }
}

TEST(RingBuffer, CapacityPower2Minus1) {
    SPSCRingBuffer<int> buffer(5); // should round to 8
    int pushed = 0;
    while (buffer.push(pushed))
        pushed++;
    
    EXPECT_EQ(pushed, 7);
    EXPECT_EQ(buffer.get_dropped(), 1);
}

TEST(RingBuffer, PushOnFull) {
    SPSCRingBuffer<int> buffer(2);
    EXPECT_TRUE(buffer.push(1));
    EXPECT_FALSE(buffer.push(2)); // full
    EXPECT_EQ(buffer.get_dropped(), 1);

    EXPECT_FALSE(buffer.push(3));
    EXPECT_EQ(buffer.get_dropped(), 2); // check counts properly
}

TEST(RingBuffer, Wraps) {
    SPSCRingBuffer<int> buffer(4);
    
    // cycle pushing and popping to advance index
    for (int cycle = 0; cycle < 5; cycle++) {
        EXPECT_TRUE(buffer.push(cycle * 10));
        EXPECT_TRUE(buffer.push(cycle * 10 + 1));

        int out = -1;
        EXPECT_TRUE(buffer.pop(out));
        EXPECT_EQ(out, cycle * 10);
        EXPECT_TRUE(buffer.pop(out));
        EXPECT_EQ(out, cycle * 10 + 1);
    }
}

TEST(RingBuffer, PeekDoesntConsume) {
    SPSCRingBuffer<int> buffer(4);

    buffer.push(99);
    
    int peeked = -1;
    EXPECT_TRUE(buffer.peek(peeked));
    EXPECT_EQ(peeked, 99);

    int popped = -1;
    EXPECT_TRUE(buffer.pop(popped));
    EXPECT_EQ(popped, 99); // should still be there
}

TEST(RingBuffer, PeekEmpty) {
    SPSCRingBuffer<int> buffer(4);
    int out = 0;
    EXPECT_FALSE(buffer.peek(out));
}

TEST(RingBuffer, MinimumSize) {
    SPSCRingBuffer<int> buffer(0);
    EXPECT_TRUE(buffer.push(1));
    EXPECT_FALSE(buffer.push(2)); // usable capacity = 1
}
