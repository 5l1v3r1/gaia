// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "absl/strings/str_format.h"
#include "base/logging.h"

#include "mr/operator_executor.h"
#include "mr/pipeline.h"

using namespace boost;
using namespace std;

namespace mr3 {
namespace rj = rapidjson;

namespace detail {

void TableBase::SetOutput(const std::string& name, pb::WireFormat::Type type) {
  CHECK(!name.empty());

  if (!op_.has_output()) {
    pipeline_->tables_.emplace_back(this);
  }

  auto* out = op_.mutable_output();
  out->set_name(name);
  out->mutable_format()->set_type(type);

  std::unique_ptr<InputBase> ib(new InputBase(name, type, out));
  auto res = pipeline_->inputs_.emplace(name, std::move(ib));
  CHECK(res.second) << "Input '" << name << "' already exists";
}

TableBase* TableBase::MappedTableFromMe(const string& name) const {
  pb::Operator new_op = GetDependeeOp();
  new_op.set_op_name(name);
  new_op.set_type(pb::Operator::MAP);

  return new TableBase(std::move(new_op), pipeline());
}

pb::Operator TableBase::GetDependeeOp() const {
  pb::Operator res;

  if (!is_identity_) {
    CHECK(!op_.output().name().empty());
    res.add_input_name(op_.output().name());
  } else {
    res = op_;
    res.clear_output();
  }
  return res;
}

HandlerWrapperBase* TableBase::CreateHandler(RawContext* context) {
  CHECK(defined());

  return handler_factory_(context);
}

void TableBase::CheckFailIdentity() { CHECK(defined() && is_identity_); }

}  // namespace detail

RawContext::RawContext() { counter_map_.set_empty_key(StringPiece{}); }

RawContext::~RawContext() {}


std::string ShardId::ToString(absl::string_view basename) const {
  if (absl::holds_alternative<string>(*this)) {
    return absl::get<string>(*this);
  }
  return absl::StrCat(basename, "-", absl::Dec(absl::get<uint32_t>(*this), absl::kZeroPad4));
}

void OutputBase::SetCompress(pb::Output::CompressType ct, unsigned level) {
  auto* co = out_->mutable_compress();
  co->set_type(ct);
  if (level) {
    co->set_level(level);
  }
}

void OutputBase::SetShardSpec(pb::ShardSpec::Type st, unsigned modn) {
  CHECK(!out_->has_shard_spec()) << "Must be defined only once. \n" << out_->ShortDebugString();

  out_->mutable_shard_spec()->set_type(st);
  if (st == pb::ShardSpec::MODN) {
    CHECK_GT(modn, 0);
    out_->mutable_shard_spec()->set_modn(modn);
  }
}

std::string RecordTraits<rj::Document>::Serialize(bool is_binary, rj::Document&& doc) {
  sb_.Clear();
  rj::Writer<rj::StringBuffer> writer(sb_);
  doc.Accept(writer);

  return string(sb_.GetString(), sb_.GetLength());
}

bool RecordTraits<rj::Document>::Parse(bool is_binary, std::string&& tmp, rj::Document* res) {
  tmp_ = std::move(tmp);

  constexpr unsigned kFlags = rj::kParseTrailingCommasFlag | rj::kParseCommentsFlag;
  res->ParseInsitu<kFlags>(&tmp_.front());

  bool has_error = res->HasParseError();
  LOG_IF(INFO, has_error) << rj::GetParseError_En(res->GetParseError()) << " for string " << tmp_;
  return !has_error;
}

void OperatorExecutor::FinalizeContext(long items_cnt, RawContext* raw_context) {
  raw_context->Flush();
  parse_errors_.fetch_add(raw_context->parse_errors, std::memory_order_relaxed);
  do_fn_calls_.fetch_add(items_cnt, std::memory_order_relaxed);

  std::lock_guard<fibers::mutex> lk(mu_);
  for (const auto& k_v : raw_context->counter_map()) {
    counter_map_[k_v.first] += k_v.second;
  }
}

}  // namespace mr3

ostream& operator<<(ostream& os, const mr3::ShardId& sid) {
  if (absl::holds_alternative<string>(sid)) {
    os << absl::get<string>(sid);
  } else {
    os << absl::get<uint32_t>(sid);
  }
  return os;
}
