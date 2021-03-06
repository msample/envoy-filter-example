#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "inject_config.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/http/filter/ratelimit.h"
#include "common/http/headers.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/http/mocks.h"
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
    "trigger_headers": [{ "name": "cookie.sessId"}],
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
    "trigger_headers": [{ "name": "cookie.sessId"}],
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

TEST_F(InjectFilterTest, GoodConfigWithTimeout) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_headers": [":path"],
    "cluster_name": "sessionCheck",
    "timeout_ms": 2222,
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  int64_t t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->timeout_ms();
  EXPECT_EQ(2222, t);
}

TEST_F(InjectFilterTest, GoodConfigWithDefaultTimeout) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_headers": [":path"],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  int64_t t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->timeout_ms();
  EXPECT_EQ(120, t);
}

TEST_F(InjectFilterTest, BadConfigWithStringTimeout) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_headers": [":path"],
    "upstream_inject_headers": ["x-myco-jwt"],
    "upstream_remove_headers": ["cookie.sessId"],
    "cluster_name": "sessionCheck",
    "timeout_ms": "2222"
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), Json::Exception);
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredExplcitFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "always_triggered": false,
    "include_headers": [":path"],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->always_triggered();
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredExplcitTrue) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "always_triggered": true,
    "include_headers": [":path"],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->always_triggered();
  EXPECT_EQ(true, t);
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredImplicitFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_headers": [":path"],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->always_triggered();
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredDefaultWorks) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "x-da-trigger"}],
    "include_headers": ["cookie"],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]

  }
  )EOF";
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  Http::InjectFilter f(fconfig);
  // trigger header absent
  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/some/path?qp1=foo&qp2=bar"},
                                  {":scheme", "http"}, {":authority", "host"},
                                  {"cookie", "sessId=123"}};
  Http::FilterHeadersStatus s = f.decodeHeaders(headers, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, s);
  EXPECT_EQ(Http::InjectFilter::State::NotTriggered, f.getState());
}
TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredFalseWorks) {
  const std::string filter_config = R"EOF(
  {
    "include_headers": ["cookie"],
    "cluster_name": "sessionCheck",
    "always_triggered": false,
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]
  }
  )EOF";
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  Http::InjectFilter f(fconfig);
  // trigger head absent
  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/some/path?qp1=foo&qp2=bar"},
                                  {":scheme", "http"}, {":authority", "host"},
                                  {"cookie", "sessId=123"}};
  Http::FilterHeadersStatus s = f.decodeHeaders(headers, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, s);
  EXPECT_EQ(Http::InjectFilter::State::NotTriggered, f.getState());
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredTrueWorks) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "x-da-trigger"}],
    "include_headers": ["cookie"],
    "cluster_name": "sessionCheck",
    "always_triggered": true,
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]
  }
  )EOF";
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  Http::InjectFilter f(fconfig);
  MockStreamDecoderFilterCallbacks mdcb{};
  MockStreamEncoderFilterCallbacks mecb{};
  f.setDecoderFilterCallbacks(mdcb);
  f.setEncoderFilterCallbacks(mecb);
  EXPECT_CALL(mdcb, encodeHeaders_(_,_)).Times(2); // why not 1 time?
  // trigger head absent
  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/some/path?qp1=foo&qp2=bar"},
                                  {":scheme", "http"}, {":authority", "host"},
                                  {"cookie", "sessId=123"}};
  Http::FilterHeadersStatus s = f.decodeHeaders(headers, true);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, s);
  EXPECT_EQ(Http::InjectFilter::State::Aborting, f.getState());
}

TEST_F(InjectFilterTest, GoodConfigAlwaysTriggeredExplicitFalseWorks) {
  const std::string filter_config = R"EOF(
  {
    "include_headers": ["cookie"],
    "cluster_name": "sessionCheck",
    "always_triggered": false,
    "actions":[
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]
  }
  )EOF";  // note trigger_headers not req'd if explicit value for always_triggered
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  Http::InjectFilter f(fconfig);
  Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/some/path?qp1=foo&qp2=bar"},
                                  {":scheme", "http"}, {":authority", "host"},
                                  {"cookie", "sessId=123"}};
  Http::FilterHeadersStatus s = f.decodeHeaders(headers, false);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, s);
  EXPECT_EQ(Http::InjectFilter::State::NotTriggered, f.getState());
}

TEST_F(InjectFilterTest, BadConfigTriggers) {
  const std::string filter_config = R"EOF(
  {
    "include_headers": [{ "name": "cookie"}],
    "cluster_name": "sessionCheck",
    "actions":[
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]
  }
  )EOF";  // no trigger_headers & no always_triggered not allowed
  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), EnvoyException);
}

TEST_F(InjectFilterTest, GoodConfigIncludeAllHeaders) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_all_headers": true,
    "cluster_name": "sessionCheck",
    "actions":[
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->include_all_headers();
  EXPECT_EQ(true, t);
}

TEST_F(InjectFilterTest, GoodConfigExplicitDontIncludeAllHeaders) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "include_all_headers": false,
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->include_all_headers();
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigImplicitDontIncludeAllHeaders) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions":[
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->include_all_headers();
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigUpstreamAllowAnyExplicitTrue) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_inject_any": true
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->action_matcher().match("ok").upstream_inject_any_;
  EXPECT_EQ(true, t);
}

TEST_F(InjectFilterTest, GoodConfigUpstreamAllowAnyExplicitFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_inject_any": false
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->action_matcher().match("ok").upstream_inject_any_;
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigUpstreamAllowAnyImplictFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  bool t = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_)->action_matcher().match("ok").upstream_inject_any_;
  EXPECT_EQ(false, t);
}

TEST_F(InjectFilterTest, GoodConfigDownstreamAllowAnyExplicitTrue) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "downstream_inject_any": true
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  bool t = fconfig->action_matcher().match("ok").downstream_inject_any_;
  EXPECT_EQ(true, t);
  std::vector<Http::LowerCaseString> downstreamInjectHeaders = fconfig->action_matcher().match("ok").downstream_inject_headers_;
  EXPECT_EQ(0, downstreamInjectHeaders.size());
}

TEST_F(InjectFilterTest, GoodConfigDownstreamAllowAnyExplicitFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "downstream_inject_headers": ["x-myco-jwt", "x-foo-baffity"],
        "downstream_inject_any": false
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  bool t = fconfig->action_matcher().match("ok").downstream_inject_any_;
  EXPECT_EQ(false, t);
  std::vector<Http::LowerCaseString> downstreamInjectHeaders = fconfig->action_matcher().match("ok").downstream_inject_headers_;
  EXPECT_EQ(2, downstreamInjectHeaders.size());
}

TEST_F(InjectFilterTest, GoodConfigDownstreamAllowAnyImplictFalse) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "downstream_inject_headers": ["x-myco-jwt", "x-foo", "x-bar", "x-baz"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  bool t = fconfig->action_matcher().match("ok").downstream_inject_any_;
  EXPECT_EQ(t, t);
  std::vector<Http::LowerCaseString> downstreamInjectHeaders = fconfig->action_matcher().match("ok").downstream_inject_headers_;
  EXPECT_EQ(4, downstreamInjectHeaders.size());
  EXPECT_EQ("x-myco-jwt", downstreamInjectHeaders.at(0).get());
  EXPECT_EQ("x-foo", downstreamInjectHeaders.at(1).get());
  EXPECT_EQ("x-bar", downstreamInjectHeaders.at(2).get());
  EXPECT_EQ("x-baz", downstreamInjectHeaders.at(3).get());
}

TEST_F(InjectFilterTest, GoodConfigWithParams) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "params": { "a": "b", "c": "3", "foo": "bar" },
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);
  EXPECT_EQ("b", fconfig->params()["a"]);
  EXPECT_EQ("3", fconfig->params()["c"]);
  EXPECT_EQ("bar", fconfig->params()["foo"]);
}

TEST_F(InjectFilterTest, BadConfigWithParams) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "params": { "a": "b", "c": "3", "foo": 99 },
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), EnvoyException);
}

TEST_F(InjectFilterTest, BadConfigWithParams2) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "params": { "a": "b", "c": "3", "foo": false },
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), EnvoyException);
}

TEST_F(InjectFilterTest, BadConfigWithParams3) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "params": { "a": "b", "c": "3", "foo": 38.9 },
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": ["ok"],
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_THROW(Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_), EnvoyException);
}

TEST_F(InjectFilterTest, ActionsBasic) {
  const std::string filter_config = R"EOF(
  {
    "trigger_headers": [{ "name": "cookie.sessId"}],
    "cluster_name": "sessionCheck",
    "actions": [
      {
        "result": [ "ok" ],
        "action": "passthrough",
        "upstream_inject_headers": ["x-myco-jwt"],
        "upstream_remove_headers": ["cookie.sessId"]
      },
      {
        "result": [ "no-user" ],
        "action": "passthrough",
        "upstream_remove_headers": ["cookie.sessId"]
      },
      {
        "result": [ "local.any" ],
        "action": "abort",
        "use_rpc_response": false,
        "response_code": 777,
        "response_headers": [{ "key": "content-type", "value": "text/html"}],
        "response_body": "<html><body>identity injection failed</body></html>"
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(filter_config);
  EXPECT_EQ("passthrough", config->getObjectArray("actions")[0]->getString("action"));
  EXPECT_EQ("ok", config->getObjectArray("actions")[0]->getStringArray("result")[0]);

  Http::InjectFilterConfigSharedPtr fconfig = Server::Configuration::InjectFilterConfig::createConfig(*config, "", fac_ctx_);

  const Http::InjectAction& iaOk = fconfig->action_matcher().match("ok");
  const Http::InjectAction& iaNoUser = fconfig->action_matcher().match("no-user");
  const Http::InjectAction& iaAny = fconfig->action_matcher().errorAction();

  EXPECT_EQ(false, iaOk.use_rpc_response_);
  EXPECT_EQ(false, iaNoUser.use_rpc_response_);
  EXPECT_EQ(false, iaAny.use_rpc_response_);

  EXPECT_EQ(0, iaOk.upstream_remove_headers_.size());
  EXPECT_EQ(0, iaNoUser.upstream_remove_headers_.size());
  EXPECT_EQ(0, iaAny.upstream_remove_headers_.size());

  EXPECT_EQ("sessId", iaOk.upstream_remove_cookie_names_[0]);
  EXPECT_EQ("sessId", iaNoUser.upstream_remove_cookie_names_[0]);

  EXPECT_EQ("passthrough", iaOk.action_);
  EXPECT_EQ("abort", iaAny.action_);
  EXPECT_EQ(777, iaAny.response_code_);

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
