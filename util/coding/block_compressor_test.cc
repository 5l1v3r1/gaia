// Copyright 2017, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#include "util/coding/block_compressor.h"

#include "base/gtest.h"
#include "base/logging.h"
#include "strings/stringpiece.h"

namespace util {

using namespace std;
using strings::ByteRange;
using strings::ToByteRange;

class BlockCompressorTest : public testing::Test {
 public:
  BlockCompressorTest() {}

 protected:
  BlockCompressor bc_;
  BlockDecompressor bdc_;
};


TEST_F(BlockCompressorTest, Basic) {
  string inp[2];
  inp[0] = base::RandStr(BlockCompressor::BLOCK_SIZE);
  inp[1] = inp[0];


  bc_.Add(ToByteRange(inp[0]));
  bc_.Add(ToByteRange(inp[1]));
  bc_.Finalize();

  const auto& cb = bc_.compressed_blocks();


  unsigned index = 0;

  uint32_t consumed;
  for (const auto& range : cb) {
    int32_t res = bdc_.Decompress(range, &consumed);
    if (res == 0)
      break;
    ASSERT_GT(res, 0) << index << "/" << range.size();
    ASSERT_LT(index, 2);
    ASSERT_EQ(range.size(), consumed);
    EXPECT_EQ(inp[index].size(), bdc_.GetDecompressedBlock().size());
    ++index;
  }
  bc_.ClearCompressedData();

  bc_.Add(ToByteRange(inp[0]));
}


TEST_F(BlockCompressorTest, Unaligned) {
  string inp = base::RandStr(16);
  bc_.Add(ToByteRange(inp));
  bc_.Finalize();
  uint32_t consumed;
  ASSERT_EQ(1, bc_.compressed_blocks().size());

  EXPECT_EQ(0, bdc_.Decompress(bc_.compressed_blocks().front(), &consumed));
}

}  // namespace util

