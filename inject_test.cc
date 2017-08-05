#include <memory>
#include <string>
#include <vector>

#include "inject_config.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/filter/ratelimit.h"
#include "common/http/headers.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgReferee;
using testing::WithArgs;
using testing::_;

namespace Http {

class InjectFilterTest : public testing::Test {
public:
  InjectFilterTest() {}

  NiceMock<Server::Configuration::MockFactoryContext> fac_ctx_;
};

TEST_F(InjectFilterTest, BadConfigNoCluster) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": ["cookie.sessId"],
    "include_headers": [":path"],
    "upstream_inject_headers": ["x-myco-jwt"],
    "upstream_remove_headers": ["cookie.sessId"]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), Json::Exception);
}

TEST_F(InjectFilterTest, BadConfigUnknownCluster) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": ["cookie.sessId"],
    "include_headers": [":path"],
    "upstream_inject_headers": ["x-myco-jwt"],
    "upstream_remove_headers": ["cookie.sessId"],
    "cluster_name": "fookd"
  }
  )EOF";

  ON_CALL(fac_ctx_.cluster_manager_, get(_)).WillByDefault(Return(nullptr)); // make lookup for fookd fail

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), EnvoyException);
}

TEST_F(InjectFilterTest, CookieParserMiddle) {
  std::string c("geo=x; sessionId=939133-x9393; dnt=a314");
  InjectFilter::removeNamedCookie("sessionId", c);
  EXPECT_EQ("geo=x; dnt=a314", c);
}

TEST_F(InjectFilterTest, CookieParserCaseSensitive) {
  std::string c("geo=x; sessionId=939133-x9393; dnt=a314");
  InjectFilter::removeNamedCookie("sessionid", c); // should not match
  EXPECT_EQ("geo=x; sessionId=939133-x9393; dnt=a314", c);
}

TEST_F(InjectFilterTest, CookieParserFront) {
  std::string c("sessionId=939133-x9393; dnt=a314 ");
  InjectFilter::removeNamedCookie("sessionId", c);
  EXPECT_EQ("dnt=a314 ", c);
}

TEST_F(InjectFilterTest, CookieParserEnd) {
  std::string c("geo=-122.2/49.2; sessionId=939133-x9393; dnt=a314 ");
  InjectFilter::removeNamedCookie("dnt", c);
  EXPECT_EQ("geo=-122.2/49.2; sessionId=939133-x9393", c);
}

TEST_F(InjectFilterTest, CookieParserMiddleNoSpaces) {
  std::string c("geo=x;sessionId=939133-x9393;dnt=a314");
  InjectFilter::removeNamedCookie("sessionId", c);
  EXPECT_EQ("geo=x;dnt=a314", c);
}

TEST_F(InjectFilterTest, CookieParserNameInValues) {
  std::string c("geo=sessionId=393; sessionId=939133-x9393; dnt=sessionId=3914");
  InjectFilter::removeNamedCookie("sessionId", c);
  EXPECT_EQ("geo=sessionId=393; dnt=sessionId=3914", c);
}

TEST_F(InjectFilterTest, CookieParserNameInValues2) {
  std::string c("geo=sessionId=393; sessionId=939133-x9393; dnt=sessionId");
  InjectFilter::removeNamedCookie("sessionId", c);
  EXPECT_EQ("geo=sessionId=393; dnt=sessionId", c);
}


} // namespace Http
} // namespace Envoy
