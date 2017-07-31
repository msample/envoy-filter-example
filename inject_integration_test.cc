#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "inject.pb.h"

#include "test/integration/integration.h"
#include "test/integration/utility.h"


namespace Envoy {
class InjectIntegrationTest : public BaseIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  InjectIntegrationTest() : BaseIntegrationTest(GetParam()) {}

  static const int UPSTREAM_STREAM_IND = 0;
  static const int INJECT0_STREAM_IND = 1;
  static const int INJECT1_STREAM_IND = 2;
  void SetUp() override {
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("traffic_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("injector0_0", fake_upstreams_.back()->localAddress()->ip()->port());
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    registerPort("injector1_0", fake_upstreams_.back()->localAddress()->ip()->port());
    createTestServer("inject_server2.json", {"http"});

    upstream_response_.reset(new IntegrationStreamDecoder(*dispatcher_));
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstreams_.clear();
  }


  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn), Http::CodecClient::Type::HTTP1);
    Http::TestHeaderMapImpl headers{{":method", "GET"}, {":path", "/some/path?qp1=foo&qp2=bar"},
                                    {":scheme", "http"}, {":authority", "host"},
                                    {"cookie", "sessId=123"}};
    codec_client_->makeRequestWithBody(headers, request_size_, *upstream_response_);
  }

  void waitForInject0Request() {
    fake_inject0_connection_ = fake_upstreams_[INJECT0_STREAM_IND]->waitForHttpConnection(*dispatcher_);
    inject0_request_ = fake_inject0_connection_->waitForNewStream();
    inject0_request_->waitForEndStream(*dispatcher_);
    EXPECT_STREQ("POST", inject0_request_->headers().Method()->value().c_str());
    EXPECT_STREQ("/inject.InjectService/InjectHeaders",
                 inject0_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc", inject0_request_->headers().ContentType()->value().c_str());

    inject::InjectRequest expected_request_msg;
    inject::Header* ih = expected_request_msg.mutable_inputheaders()->Add();
    ih->set_key("cookie.sessId");
    ih->set_value("123");
    ih = expected_request_msg.mutable_inputheaders()->Add();
    ih->set_key(":path");
    ih->set_value("/some/path?qp1=foo&qp2=bar");
    expected_request_msg.add_injectheadernames("x-myco-jwt");

    Grpc::Decoder decoder;
    std::vector<Grpc::Frame> decoded_frames;
    EXPECT_TRUE(decoder.decode(inject0_request_->body(), decoded_frames));
    EXPECT_EQ(1, decoded_frames.size());
    inject::InjectRequest request_msg;
    Buffer::ZeroCopyInputStreamImpl stream(std::move(decoded_frames[0].data_));
    EXPECT_TRUE(decoded_frames[0].flags_ == Grpc::GRPC_FH_DEFAULT);
    EXPECT_TRUE(request_msg.ParseFromZeroCopyStream(&stream));
    EXPECT_EQ(expected_request_msg.DebugString(), request_msg.DebugString());
  }


  void waitForSuccessfulUpstreamResponse() {
    fake_upstream_connection_ = fake_upstreams_[UPSTREAM_STREAM_IND]->waitForHttpConnection(*dispatcher_);
    upstream_request_ = fake_upstream_connection_->waitForNewStream();
    upstream_request_->waitForEndStream(*dispatcher_);

    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);
    upstream_response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    EXPECT_TRUE(upstream_response_->complete());
    EXPECT_STREQ("200", upstream_response_->headers().Status()->value().c_str());
    EXPECT_EQ(response_size_, upstream_response_->body().size());
  }

  void waitForFailedUpstreamResponse(uint32_t response_code) {
    upstream_response_->waitForEndStream();
    EXPECT_TRUE(upstream_response_->complete());
    EXPECT_STREQ(std::to_string(response_code).c_str(),
                 upstream_response_->headers().Status()->value().c_str());
  }

  void sendInjectResponse() {
    inject0_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    inject::InjectResponse response_msg;
    inject::Header* ih = response_msg.mutable_headers()->Add();
    ih->set_key("x-myco-jwt");
    ih->set_value("(a-signed-jwt)");

    auto serialized_response = Grpc::Common::serializeBody(response_msg);
    inject0_request_->encodeData(*serialized_response, false);
    inject0_request_->encodeTrailers(Http::TestHeaderMapImpl{{"grpc-status", "0"}});
  }


  void cleanup() {
    codec_client_->close();
    if (fake_inject0_connection_ != nullptr) {
      fake_inject0_connection_->close();
      fake_inject0_connection_->waitForDisconnect();
    }
    if (fake_upstream_connection_ != nullptr) {
      fake_upstream_connection_->close();
      fake_upstream_connection_->waitForDisconnect();
    }
  }

  IntegrationCodecClientPtr codec_client_;
  FakeHttpConnectionPtr fake_upstream_connection_;
  FakeHttpConnectionPtr fake_inject0_connection_;
  IntegrationStreamDecoderPtr upstream_response_;
  FakeStreamPtr upstream_request_;
  FakeStreamPtr inject0_request_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;

};

INSTANTIATE_TEST_CASE_P(IpVersions, InjectIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));


TEST_P(InjectIntegrationTest, Ok) {
  initiateClientConnection();
  waitForInject0Request();
  sendInjectResponse();
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

} // Envoy


