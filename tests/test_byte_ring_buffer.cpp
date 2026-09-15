// The byte ring under every IPC connection.
//
// IpcConnection gives each socket an rx and a tx ByteRingBuffer and reads and writes them through
// the zero-copy pair — writePtr()/writeLen()/produce() on the way in, readPtr()/readLen()/consume()
// on the way out — so a partial recv() and a partial send() are the normal case, not the edge one.
// That makes two properties load-bearing:
//
//   · writeLen()/readLen() report the CONTIGUOUS run, not the total. A caller that took them for
//     the total would hand recv() a length that runs off the end of the allocation.
//   · Bytes come back in the order they went in, across any number of wraps. A ring that reorders
//     under wrap does not lose an IPC frame, it silently corrupts one.
//
// Nothing here needs a socket, so all of it is checked directly.

#include "algorithm/ByteRingBuffer.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using pz::algorithm::ByteRingBuffer;

namespace
{

std::vector<std::uint8_t> bytes(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::size_t write(ByteRingBuffer& ring, const std::string& s)
{
    const auto v = bytes(s);
    return ring.write(v.data(), v.size());
}

std::string read(ByteRingBuffer& ring, std::size_t n)
{
    std::vector<std::uint8_t> out(n, 0);
    const std::size_t got = ring.read(out.data(), n);
    return std::string(out.begin(), out.begin() + got);
}

std::string peek(const ByteRingBuffer& ring, std::size_t n)
{
    std::vector<std::uint8_t> out(n, 0);
    const std::size_t got = ring.peek(out.data(), n);
    return std::string(out.begin(), out.begin() + got);
}

// Drive the buffer the way IpcConnection does: hand the contiguous window to a "recv" that fills
// it, then produce() what it claims to have written.
std::size_t writeThroughPointer(ByteRingBuffer& ring, const std::string& s)
{
    const auto src = bytes(s);
    std::size_t done = 0;
    while (done < src.size() && ring.writable() > 0)
    {
        std::uint8_t* p = ring.writePtr();
        const std::size_t n = std::min(ring.writeLen(), src.size() - done);
        if (p == nullptr || n == 0)
            break;
        std::memcpy(p, src.data() + done, n);
        ring.produce(n);
        done += n;
    }
    return done;
}

std::string readThroughPointer(ByteRingBuffer& ring, std::size_t want)
{
    std::string out;
    while (out.size() < want && ring.readable() > 0)
    {
        const std::uint8_t* p = ring.readPtr();
        const std::size_t n = std::min(ring.readLen(), want - out.size());
        if (p == nullptr || n == 0)
            break;
        out.append(reinterpret_cast<const char*>(p), n);
        ring.consume(n);
    }
    return out;
}

}

// ── Accounting ──────────────────────────────────────────────────────────────────────────────────

TEST(RingBuffer, StartsEmptyAtItsFullCapacity)
{
    const ByteRingBuffer ring(16);
    EXPECT_EQ(16u, ring.capacity());
    EXPECT_EQ(0u, ring.readable());
    EXPECT_EQ(16u, ring.writable());
}

TEST(RingBuffer, ReadableAndWritableAlwaysSumToCapacity)
{
    ByteRingBuffer ring(8);

    for (int i = 0; i < 20; ++i)
    {
        write(ring, "abc");
        EXPECT_EQ(ring.capacity(), ring.readable() + ring.writable()) << "after write " << i;
        read(ring, 2);
        EXPECT_EQ(ring.capacity(), ring.readable() + ring.writable()) << "after read " << i;
    }
}

// ── Round trip ──────────────────────────────────────────────────────────────────────────────────

TEST(RingBuffer, ReadsBackExactlyWhatWasWritten)
{
    ByteRingBuffer ring(32);

    EXPECT_EQ(5u, write(ring, "hello"));
    EXPECT_EQ(5u, ring.readable());
    EXPECT_EQ("hello", read(ring, 5));
    EXPECT_EQ(0u, ring.readable());
}

TEST(RingBuffer, SeparateWritesFormOneContinuousStream)
{
    ByteRingBuffer ring(32);

    write(ring, "one");
    write(ring, "two");
    write(ring, "three");

    // The ring is a byte stream, not a message queue — IpcCodec is what finds frame boundaries.
    EXPECT_EQ("onetwothree", read(ring, 11));
}

TEST(RingBuffer, PreservesByteValuesAcrossTheWholeRange)
{
    ByteRingBuffer ring(256);

    std::vector<std::uint8_t> in(256);
    std::iota(in.begin(), in.end(), static_cast<std::uint8_t>(0));
    ASSERT_EQ(256u, ring.write(in.data(), in.size()));

    std::vector<std::uint8_t> out(256, 0);
    ASSERT_EQ(256u, ring.read(out.data(), out.size()));
    EXPECT_EQ(in, out) << "0x00 and 0x80..0xFF must survive unchanged";
}

// ── Limits ──────────────────────────────────────────────────────────────────────────────────────

TEST(RingBuffer, WriteStopsAtCapacityAndReportsWhatItTook)
{
    ByteRingBuffer ring(4);

    EXPECT_EQ(4u, write(ring, "abcdefgh")) << "a short write must be reported, not silently truncated";
    EXPECT_EQ(0u, ring.writable());
    EXPECT_EQ("abcd", read(ring, 4));
}

TEST(RingBuffer, WriteIntoAFullRingTakesNothing)
{
    ByteRingBuffer ring(4);
    ASSERT_EQ(4u, write(ring, "abcd"));

    EXPECT_EQ(0u, write(ring, "e"));
    EXPECT_EQ(4u, ring.readable()) << "the existing contents must not be overwritten";
    EXPECT_EQ("abcd", read(ring, 4));
}

TEST(RingBuffer, ReadTakesOnlyWhatIsThereAndReportsIt)
{
    ByteRingBuffer ring(16);
    write(ring, "abc");

    std::vector<std::uint8_t> out(16, 0xEE);
    EXPECT_EQ(3u, ring.read(out.data(), out.size()));
    EXPECT_EQ(0xEE, out[3]) << "the rest of the caller's buffer must be left alone";
}

TEST(RingBuffer, ReadFromAnEmptyRingTakesNothing)
{
    ByteRingBuffer ring(16);
    std::vector<std::uint8_t> out(4, 0xEE);

    EXPECT_EQ(0u, ring.read(out.data(), out.size()));
    EXPECT_EQ(0xEE, out[0]);
}

TEST(RingBuffer, ZeroLengthOperationsAreNoOps)
{
    ByteRingBuffer ring(8);
    write(ring, "abcd");

    EXPECT_EQ(0u, ring.write(nullptr, 0));
    EXPECT_EQ(0u, ring.read(nullptr, 0));
    EXPECT_EQ(0u, ring.peek(nullptr, 0));
    EXPECT_EQ(4u, ring.readable()) << "nothing should have moved";
}

// ── Wrap-around ─────────────────────────────────────────────────────────────────────────────────

TEST(RingBuffer, KeepsOrderWhenAWriteWrapsPastTheEnd)
{
    ByteRingBuffer ring(8);

    ASSERT_EQ(6u, write(ring, "abcdef"));
    ASSERT_EQ("abc", read(ring, 3));       // head now at 3, tail at 6
    ASSERT_EQ(4u, write(ring, "ghij"));    // writes 6,7 then wraps to 0,1

    EXPECT_EQ("defghij", read(ring, 7));
}

TEST(RingBuffer, KeepsOrderAcrossManyWrapsOfTheSameRing)
{
    ByteRingBuffer ring(7);   // deliberately not a power of two, so the wrap lands unaligned

    std::string expected;
    std::string got;
    for (int i = 0; i < 200; ++i)
    {
        const std::string chunk(1 + (i % 5), static_cast<char>('a' + (i % 26)));
        const std::size_t took = write(ring, chunk);
        expected.append(chunk, 0, took);

        got += read(ring, 1 + (i % 3));
    }
    got += read(ring, ring.readable());

    EXPECT_EQ(expected, got) << "a ring that reorders under wrap corrupts frames rather than losing them";
}

TEST(RingBuffer, FillsEmptiesAndRefillsWithoutDrift)
{
    ByteRingBuffer ring(5);

    for (int i = 0; i < 50; ++i)
    {
        ASSERT_EQ(5u, write(ring, "abcde")) << "cycle " << i;
        ASSERT_EQ(0u, ring.writable()) << "cycle " << i;
        ASSERT_EQ("abcde", read(ring, 5)) << "cycle " << i;
        ASSERT_EQ(0u, ring.readable()) << "cycle " << i;
    }
}

// ── The zero-copy pair, as IpcConnection drives it ──────────────────────────────────────────────

TEST(RingBufferZeroCopy, WriteLenIsTheContiguousRunNotTheTotalFree)
{
    ByteRingBuffer ring(8);

    ASSERT_EQ(6u, write(ring, "abcdef"));
    ASSERT_EQ("abcdef", read(ring, 6));   // head and tail both at 6, ring empty

    // 8 bytes are free but only 2 of them sit before the end of the allocation. Handing recv() the
    // total here is how a ring buffer writes past its own buffer.
    EXPECT_EQ(8u, ring.writable());
    EXPECT_EQ(2u, ring.writeLen());
}

TEST(RingBufferZeroCopy, ReadLenIsTheContiguousRunNotTheTotalReadable)
{
    ByteRingBuffer ring(8);

    ASSERT_EQ(6u, write(ring, "abcdef"));
    ASSERT_EQ("abcde", read(ring, 5));    // head at 5
    ASSERT_EQ(4u, write(ring, "ghij"));   // wraps: 6,7,0,1

    EXPECT_EQ(5u, ring.readable());
    EXPECT_EQ(3u, ring.readLen()) << "f, g, h sit before the end; i and j are after the wrap";
}

TEST(RingBufferZeroCopy, WritePtrIsNullExactlyWhenThereIsNoRoom)
{
    ByteRingBuffer ring(4);
    EXPECT_NE(nullptr, ring.writePtr());

    ASSERT_EQ(4u, write(ring, "abcd"));
    EXPECT_EQ(nullptr, ring.writePtr()) << "a full ring must not hand out a pointer to write through";
    EXPECT_EQ(0u, ring.writeLen());
}

TEST(RingBufferZeroCopy, ReadPtrIsNullExactlyWhenThereIsNothingToRead)
{
    ByteRingBuffer ring(4);
    EXPECT_EQ(nullptr, ring.readPtr());
    EXPECT_EQ(0u, ring.readLen());

    write(ring, "a");
    EXPECT_NE(nullptr, ring.readPtr());
    EXPECT_EQ(1u, ring.readLen());
}

TEST(RingBufferZeroCopy, PointerPathAndCopyPathAgreeAcrossAWrap)
{
    // The two ways into the ring have to produce the same bytes, because recv() uses one and the
    // frame writer uses the other on the same buffer.
    ByteRingBuffer viaPointer(8);
    ByteRingBuffer viaCopy(8);

    for (const char* chunk : {"abcdef", "gh", "ijkl"})
    {
        writeThroughPointer(viaPointer, chunk);
        write(viaCopy, chunk);

        const std::size_t take = viaPointer.readable() / 2;
        EXPECT_EQ(readThroughPointer(viaPointer, take), read(viaCopy, take)) << chunk;
    }

    EXPECT_EQ(viaPointer.readable(), viaCopy.readable());
    EXPECT_EQ(readThroughPointer(viaPointer, 8), read(viaCopy, 8));
}

TEST(RingBufferZeroCopy, ProducingLessThanWasOfferedIsTheNormalPartialRecv)
{
    ByteRingBuffer ring(16);

    // recv() filled only part of the window it was given; only that part may become readable.
    std::uint8_t* p = ring.writePtr();
    ASSERT_NE(nullptr, p);
    ASSERT_EQ(16u, ring.writeLen());

    std::memcpy(p, "abc", 3);
    ring.produce(3);

    EXPECT_EQ(3u, ring.readable());
    EXPECT_EQ(13u, ring.writable());
    EXPECT_EQ("abc", read(ring, 3));
}

TEST(RingBufferZeroCopy, ConsumingLessThanWasReadableIsTheNormalPartialSend)
{
    ByteRingBuffer ring(16);
    write(ring, "abcdef");

    // send() took only the first two bytes; the rest must still be there, in order.
    ASSERT_EQ(6u, ring.readLen());
    ring.consume(2);

    EXPECT_EQ(4u, ring.readable());
    EXPECT_EQ("cdef", read(ring, 4));
}

// ── peek ────────────────────────────────────────────────────────────────────────────────────────

TEST(RingBufferPeek, LooksWithoutConsuming)
{
    ByteRingBuffer ring(16);
    write(ring, "header-and-body");

    EXPECT_EQ("header", peek(ring, 6));
    EXPECT_EQ(15u, ring.readable()) << "peek must leave the stream where it was";
    EXPECT_EQ("header", peek(ring, 6)) << "and must be repeatable";
    EXPECT_EQ("header-and-body", read(ring, 15));
}

TEST(RingBufferPeek, ReadsAcrossTheWrapInOrder)
{
    ByteRingBuffer ring(8);

    ASSERT_EQ(6u, write(ring, "abcdef"));
    ASSERT_EQ("abcde", read(ring, 5));
    ASSERT_EQ(4u, write(ring, "ghij"));   // f | g h | wrap | i j

    // This is exactly what the codec does to read a frame header that straddles the wrap.
    EXPECT_EQ("fghij", peek(ring, 5));
    EXPECT_EQ(5u, ring.readable());
}

TEST(RingBufferPeek, TakesOnlyWhatIsThere)
{
    ByteRingBuffer ring(16);
    write(ring, "abc");

    std::vector<std::uint8_t> out(16, 0xEE);
    EXPECT_EQ(3u, ring.peek(out.data(), out.size()));
    EXPECT_EQ(0xEE, out[3]) << "the caller's buffer past the available bytes must be untouched";
}

TEST(RingBufferPeek, OnAnEmptyRingTakesNothing)
{
    ByteRingBuffer ring(16);
    std::vector<std::uint8_t> out(4, 0xEE);

    EXPECT_EQ(0u, ring.peek(out.data(), out.size()));
    EXPECT_EQ(0xEE, out[0]);
}

// ── clear ───────────────────────────────────────────────────────────────────────────────────────

TEST(RingBuffer, ClearEmptiesTheRingAndReturnsTheWholeBuffer)
{
    ByteRingBuffer ring(8);
    write(ring, "abcdef");
    read(ring, 3);

    ring.clear();

    EXPECT_EQ(0u, ring.readable());
    EXPECT_EQ(8u, ring.writable());
    EXPECT_EQ(nullptr, ring.readPtr());
}

TEST(RingBuffer, ClearAlsoResetsTheWrapSoTheNextWriteIsContiguous)
{
    ByteRingBuffer ring(8);
    write(ring, "abcdef");
    read(ring, 6);

    ASSERT_EQ(2u, ring.writeLen()) << "mid-ring before the clear";
    ring.clear();
    EXPECT_EQ(8u, ring.writeLen()) << "a cleared connection buffer should start from the top again";

    ASSERT_EQ(8u, write(ring, "12345678"));
    EXPECT_EQ("12345678", read(ring, 8));
}
