// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <gmock/gmock.h>

#include <rapidjson/error/en.h>

#include "mr/pipeline.h"

#include "base/gtest.h"
#include "base/logging.h"
#include "mr/test_utils.h"

#include "strings/numbers.h"
#include "util/asio/io_context_pool.h"

using namespace std;

namespace mr3 {

using namespace util;
using namespace boost;
using testing::Contains;
using testing::ElementsAre;
using testing::Pair;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;
using other::StrVal;

namespace rj = rapidjson;


class StrValMapper {
 public:
  void Do(string val, mr3::DoContext<StrVal>* cnt) {
    StrVal a;
    val.append("a");
    a.val = std::move(val);

    cnt->Write(std::move(a));
  }
};


class MrTest : public testing::Test {
 protected:
  void SetUp() final {
    pool_.reset(new IoContextPool{1});
    pool_->Run();
    pipeline_.reset(new Pipeline(pool_.get()));
  }

  void TearDown() final { pool_.reset(); }

  auto MatchShard(const string& sh_name, const vector<string>& elems) {
    // matcher references the array, so we must to store it in case we pass a temporary rvalue.
    tmp_.push_back(elems);

    return Pair(ShardId{sh_name}, UnorderedElementsAreArray(tmp_.back()));
  }

  auto MatchShard(unsigned index, const vector<string>& elems) {
    // matcher references the array, so we must to store it in case we pass a temporary rvalue.
    tmp_.push_back(elems);

    return Pair(ShardId{index}, UnorderedElementsAreArray(tmp_.back()));
  }

  std::unique_ptr<IoContextPool> pool_;
  std::unique_ptr<Pipeline> pipeline_;
  TestRunner runner_;

  vector<vector<string>> tmp_;
};

TEST_F(MrTest, Basic) {
  EXPECT_EQ(ShardId{1}, ShardId{1});
  EXPECT_NE(ShardId{1}, ShardId{"foo"});

  StringTable str1 = pipeline_->ReadText("read_bar", "bar.txt");
  str1.Write("new_table", pb::WireFormat::TXT)
      .AndCompress(pb::Output::GZIP, 1)
      .WithCustomSharding([](const std::string& rec) { return "shard1"; });

  vector<string> elements{"1", "2", "3", "4"};

  runner_.AddInputRecords("bar.txt", elements);
  pipeline_->Run(&runner_);

  EXPECT_THAT(runner_.Table("new_table"), ElementsAre(MatchShard("shard1", elements)));
}

TEST_F(MrTest, Json) {
  PTable<rapidjson::Document> json_table = pipeline_->ReadText("read_bar", "bar.txt").AsJson();
  auto json_shard_func = [](const rapidjson::Document& doc) {
    return doc.HasMember("foo") ? "shard0" : "shard1";
  };

  json_table.Write("json_table", pb::WireFormat::TXT)
      .AndCompress(pb::Output::GZIP, 1)
      .WithCustomSharding(json_shard_func);

  const char kJson1[] = R"({"foo":"bar"})";
  const char kJson2[] = R"({"id":1})";
  const char kJson3[] = R"({"foo":null})";

  vector<string> elements{kJson2, kJson1, kJson3};

  runner_.AddInputRecords("bar.txt", elements);
  pipeline_->Run(&runner_);
  EXPECT_THAT(
      runner_.Table("json_table"),
      UnorderedElementsAre(MatchShard("shard0", {kJson3, kJson1}), MatchShard("shard1", {kJson2})));
}

TEST_F(MrTest, InvalidJson) {
  char str[] = R"({"roman":"��i���u�.nW��'$��uٿ�����d�ݹ��5�"} )";

  rapidjson::Document doc;
  doc.Parse(str);

  rj::StringBuffer s;
  rj::Writer<rj::StringBuffer> writer(s);
  doc.Accept(writer);
  LOG(INFO) << s.GetString();
}

TEST_F(MrTest, Map) {
  StringTable str1 = pipeline_->ReadText("read_bar", "bar.txt");
  PTable<StrVal> str2 = str1.Map<StrValMapper>("Map1");

  str2.Write("table", pb::WireFormat::TXT).WithModNSharding(10, [](const StrVal&) { return 11; });
  vector<string> elements{"1", "2", "3", "4"};

  runner_.AddInputRecords("bar.txt", elements);
  pipeline_->Run(&runner_);

  vector<string> expected;
  for (const auto& e : elements)
    expected.push_back(e + "a");

  EXPECT_THAT(runner_.Table("table"), ElementsAre(MatchShard(1, expected)));
}

struct IntVal {
  int val;

  operator string() const { return std::to_string(val); }
};

template <> class RecordTraits<IntVal> {
 public:
  static std::string Serialize(IntVal&& doc) { return std::to_string(doc.val); }

  bool Parse(std::string&& tmp, IntVal* res) { return safe_strto32(tmp, &res->val); }
};

class IntMapper {
 public:
  void Do(StrVal a, mr3::DoContext<IntVal>* cnt) {
    CHECK_GT(a.val.size(), 1);
    a.val.pop_back();

    IntVal iv;
    CHECK(safe_strto32(a.val, &iv.val)) << a.val;
    cnt->Write(std::move(iv));
  }
};

TEST_F(MrTest, MapAB) {
  vector<string> elements{"1", "2", "3", "4"};

  runner_.AddInputRecords("bar.txt", elements);
  PTable<IntVal> itable =
      pipeline_->ReadText("read_bar", "bar.txt").As<IntVal>();  // Map<StrValMapper>("Map1");
  PTable<StrVal> atable = itable.Map<StrValMapper>("Map1");

  atable.Write("table", pb::WireFormat::TXT).WithModNSharding(10, [](const StrVal&) { return 11; });

  PTable<IntVal> final_table = atable.Map<IntMapper>("IntMap");
  final_table.Write("final_table", pb::WireFormat::TXT).WithModNSharding(7, [](const IntVal&) {
    return 10;
  });

  pipeline_->Run(&runner_);

  vector<string> expected;
  for (const auto& e : elements)
    expected.push_back(e + "a");

  EXPECT_THAT(runner_.Table("table"), ElementsAre(MatchShard(1, expected)));
  EXPECT_THAT(runner_.Table("final_table"), ElementsAre(MatchShard(3, elements)));
}

class StrJoiner {
  absl::flat_hash_map<int, int> counts_;

  public:
  void On1(IntVal&& iv, DoContext<string>* out) {

  }

  void On2(IntVal&& iv, DoContext<string>* out) {

  }
};

TEST_F(MrTest, Join) {
  vector<string> stream1{"1", "2", "3", "4"}, stream2{"2", "3"};

  runner_.AddInputRecords("stream1.txt", stream1);
  runner_.AddInputRecords("stream2.txt", stream2);

  PTable<IntVal> itable1 = pipeline_->ReadText("read1", "stream1.txt").As<IntVal>();
  PTable<IntVal> itable2 = pipeline_->ReadText("read2", "stream2.txt").As<IntVal>();
  PTable<string> res = pipeline_->Join("join_tables", {JoinInput(itable1, &StrJoiner::On1),
                                                       JoinInput(itable2, &StrJoiner::On2)});

}

}  // namespace mr3
