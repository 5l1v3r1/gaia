// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <gmock/gmock.h>

#include <rapidjson/error/en.h>

#include "mr/pipeline.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/container/flat_hash_map.h"
#include "base/gtest.h"
#include "base/logging.h"
#include "strings/numbers.h"
#include "util/asio/io_context_pool.h"

using namespace std;
struct StrVal {
  string val;
};

namespace mr3 {

// For gcc less than 7 we should enclose the specialization into the original namespace.
// https://stackoverflow.com/questions/25594644/warning-specialization-of-template-in-different-namespace
template <> class RecordTraits<StrVal> {
 public:
  static std::string Serialize(StrVal&& doc) { return std::move(doc.val); }

  bool Parse(std::string&& tmp, StrVal* res) {
    res->val = tmp;
    return true;
  }
};

using namespace util;
using namespace boost;
using testing::Contains;
using testing::ElementsAre;
using testing::Pair;
using testing::UnorderedElementsAre;
using testing::UnorderedElementsAreArray;

using ShardedOutput = std::unordered_map<ShardId, std::vector<string>>;

namespace rj = rapidjson;

void PrintTo(const ShardId& src, std::ostream* os) {
  if (absl::holds_alternative<uint32_t>(src)) {
    *os << absl::get<uint32_t>(src);
  } else {
    *os << absl::get<std::string>(src);
  }
}

class TestContext : public RawContext {
  ShardedOutput& outp_;
  fibers::mutex& mu_;

 public:
  TestContext(ShardedOutput* outp, fibers::mutex* mu) : outp_(*outp), mu_(*mu) {}

  void WriteInternal(const ShardId& shard_id, std::string&& record) {
    std::lock_guard<fibers::mutex> lk(mu_);

    outp_[shard_id].push_back(record);
  }
};

class StrValMapper {
 public:
  void Do(string val, mr3::DoContext<StrVal>* cnt) {
    StrVal a;
    val.append("a");
    a.val = std::move(val);

    cnt->Write(std::move(a));
  }
};

class TestRunner : public Runner {
 public:
  void Init() final;

  void Shutdown() final;

  RawContext* CreateContext(const pb::Operator& op) final;

  void ExpandGlob(const std::string& glob, std::function<void(const std::string&)> cb) final;

  // Read file and fill queue. This function must be fiber-friendly.
  size_t ProcessInputFile(const std::string& filename, pb::WireFormat::Type type,
                          RecordQueue* queue) final;

  void OperatorStart() final {}
  void OperatorEnd(std::vector<std::string>* out_files) final;

  void AddInputRecords(const string& fl, const std::vector<string>& records) {
    std::copy(records.begin(), records.end(), back_inserter(input_fs_[fl]));
  }

  const ShardedOutput& Table(const string& tb_name) const {
    auto it = out_fs_.find(tb_name);
    CHECK(it != out_fs_.end()) << "Missing table file " << tb_name;

    return it->second;
  }

 private:
  absl::flat_hash_map<string, vector<string>> input_fs_;
  absl::flat_hash_map<string, ShardedOutput> out_fs_;
  string last_out_name_;

  fibers::mutex mu_;
};

void TestRunner::Init() {}

void TestRunner::Shutdown() {}

RawContext* TestRunner::CreateContext(const pb::Operator& op) {
  CHECK(!op.output().name().empty());
  last_out_name_= op.output().name();
  return new TestContext(&out_fs_[last_out_name_], &mu_);
}

void TestRunner::ExpandGlob(const std::string& glob, std::function<void(const std::string&)> cb) {
  auto it = input_fs_.find(glob);
  if (it != input_fs_.end()) {
    cb(it->first);
  }
}

void TestRunner::OperatorEnd(std::vector<std::string>* out_files) {
  auto it = out_fs_.find(last_out_name_);
  CHECK(it != out_fs_.end());
  for (const auto& k_v : it->second) {
    string name = last_out_name_ + "/" + k_v.first.ToString("shard");
    out_files->push_back(name);
    input_fs_[name] = k_v.second;
  }
}

// Read file and fill queue. This function must be fiber-friendly.
size_t TestRunner::ProcessInputFile(const std::string& filename, pb::WireFormat::Type type,
                                    RecordQueue* queue) {
  auto it = input_fs_.find(filename);
  CHECK(it != input_fs_.end());
  for (const auto& str : it->second) {
    queue->Push(str);
  }

  return it->second.size();
}

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

}  // namespace mr3
