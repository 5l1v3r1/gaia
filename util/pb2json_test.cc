// Copyright 2018, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "util/pb2json.h"

#include "util/plang/addressbook.pb.h"
#include "base/gtest.h"
#include "absl/strings/str_cat.h"

namespace util {

using namespace tutorial;
using namespace std;

class Pb2JsonTest : public testing::Test {
protected:
};

TEST_F(Pb2JsonTest, Basic) {
  Person person;
  person.set_name("Roman");
  person.mutable_account()->set_bank_name("Leumi");
  person.set_id(5);

  string res = Pb2Json(person);
  EXPECT_EQ(R"({"name":"Roman","id":5,"account":{"bank_name":"Leumi"},"dval":0.0})", res);
}

TEST_F(Pb2JsonTest, Unicode) {
  Person person;
  person.set_name("Роман");
  person.mutable_account()->set_bank_name("לאומי");
  person.set_id(5);

  string res = Pb2Json(person);
  EXPECT_EQ(R"({"name":"Роман","id":5,"account":{"bank_name":"לאומי"},"dval":0.0})", res);
}

TEST_F(Pb2JsonTest, Escape) {
  Person person;
  person.set_name("\x01\"");
  person.mutable_account()->set_bank_name("\\");
  person.set_id(5);

  string res = Pb2Json(person);
  EXPECT_EQ(R"({"name":"\u0001\"","id":5,"account":{"bank_name":"\\"},"dval":0.0})", res);
}

const char* kExpected = R"({"name":"","id":0,"phone":[{"number":"1","type":"HOME"},)"
                        R"({"number":"2","type":"WORK"}],"tag":["good","young"],"dval":0.0})";

TEST_F(Pb2JsonTest, EnumAndRepeated) {
  Person p;
  Person::PhoneNumber* n = p.add_phone();
  n->set_type(Person::HOME);
  n->set_number("1");
  n = p.add_phone();
  n->set_type(Person::WORK);
  n->set_number("2");
  p.add_tag("good");
  p.add_tag("young");
  string res = Pb2Json(p);

  EXPECT_EQ(kExpected, res);
}

TEST_F(Pb2JsonTest, Double) {
  static_assert(26.100000381f == 26.1f, "");
  absl::AlphaNum al(26.1f);
  EXPECT_EQ(4, al.size());

  Person p;
  p.set_fval(26.1f);
  string res = Pb2Json(p);

  // Json has only double representation so it may change floats.
  // If we want to fix it, we must use different formatter for floats.
  // For some reason RawNumber adds quotes, need to fix it or fork rapidjson.
  EXPECT_EQ(R"({"name":"","id":0,"dval":0.0,"fval":26.100000381})", res);
}

}  // namespace util
