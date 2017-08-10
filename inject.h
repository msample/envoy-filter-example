#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "envoy/http/filter.h"
#include "envoy/local_info/local_info.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/grpc/async_client.h"
#include "common/grpc/async_client_impl.h"
#include "common/router/config_utility.h"
#include "inject.pb.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Http {



/**
 * Global configuration for the Injector
 */
class InjectFilterConfig {
public:

  InjectFilterConfig(std::vector<Router::ConfigUtility::HeaderData>& trigger_headers,
                     std::vector<std::string>& trigger_cookie_names,
                     std::vector<Router::ConfigUtility::HeaderData>& antitrigger_headers,
                     bool always_triggered,
                     std::vector<Http::LowerCaseString>& include_headers,
                     bool include_all_headers,
                     std::vector<Http::LowerCaseString>& upstream_inject_headers,
                     bool upstream_inject_any,
                     std::vector<Http::LowerCaseString>& upstream_remove_headers,
                     std::vector<std::string>& upstream_remove_cookie_names,
                     std::vector<Http::LowerCaseString>& downstream_inject_headers,
                     bool downstream_inject_any,
                     std::vector<Http::LowerCaseString>& downstream_remove_headers,
                     Upstream::ClusterManager& cluster_mgr,
                     const std::string cluster_name,
                     int64_t timeout_ms):
  trigger_headers_(trigger_headers), trigger_cookie_names_(trigger_cookie_names), antitrigger_headers_(antitrigger_headers),
    always_triggered_(always_triggered), include_headers_(include_headers), include_all_headers_(include_all_headers),
    upstream_inject_headers_(upstream_inject_headers), upstream_inject_any_(upstream_inject_any),
    upstream_remove_headers_(upstream_remove_headers), upstream_remove_cookie_names_(upstream_remove_cookie_names),
    downstream_inject_headers_(downstream_inject_headers), downstream_inject_any_(downstream_inject_any),
    downstream_remove_headers_(downstream_remove_headers), cluster_name_(cluster_name), timeout_ms_(timeout_ms),
    cluster_mgr_(cluster_mgr),
    method_descriptor_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders")) {
    ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders"))
  }

  const std::vector<Router::ConfigUtility::HeaderData>& trigger_headers() { return trigger_headers_; }
  const std::vector<std::string>& trigger_cookie_names() { return trigger_cookie_names_; }
  const std::vector<Router::ConfigUtility::HeaderData>& antitrigger_headers() { return antitrigger_headers_; }
  bool always_triggered() { return always_triggered_; }
  const std::vector<Http::LowerCaseString>& include_headers() { return include_headers_; }
  bool include_all_headers() { return include_all_headers_; }
  const std::vector<Http::LowerCaseString>& upstream_inject_headers() { return upstream_inject_headers_; }
  bool upstream_inject_any() { return upstream_inject_any_; }
  const std::vector<Http::LowerCaseString>& upstream_remove_headers() { return upstream_remove_headers_; }
  const std::vector<std::string>& upstream_remove_cookie_names() { return upstream_remove_cookie_names_; }
  const std::vector<Http::LowerCaseString>& downstream_inject_headers() { return downstream_inject_headers_; }
  bool downstream_inject_any() { return downstream_inject_any_; }
  const std::vector<Http::LowerCaseString>& downstream_remove_headers() { return downstream_remove_headers_; }

  int64_t timeout_ms() { return timeout_ms_; }

  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client() {
    return std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>>(new Grpc::AsyncClientImpl<inject::InjectRequest,
                                                                                                 inject::InjectResponse>(cluster_mgr_, cluster_name_));
  }
  const google::protobuf::MethodDescriptor& method_descriptor() { return method_descriptor_; }

 private:

  std::vector<Router::ConfigUtility::HeaderData> trigger_headers_;
  std::vector<std::string> trigger_cookie_names_;
  std::vector<Router::ConfigUtility::HeaderData> antitrigger_headers_;
  const bool always_triggered_;
  std::vector<Http::LowerCaseString> include_headers_;
  const bool include_all_headers_;
  std::vector<Http::LowerCaseString> upstream_inject_headers_;
  const bool upstream_inject_any_;
  std::vector<Http::LowerCaseString> upstream_remove_headers_;
  std::vector<std::string> upstream_remove_cookie_names_;
  std::vector<Http::LowerCaseString> downstream_inject_headers_;
  const bool downstream_inject_any_;
  std::vector<Http::LowerCaseString> downstream_remove_headers_;
  const std::string cluster_name_;
  const int64_t timeout_ms_;
  Upstream::ClusterManager& cluster_mgr_;
  const google::protobuf::MethodDescriptor& method_descriptor_;
};

typedef std::shared_ptr<InjectFilterConfig> InjectFilterConfigSharedPtr;

class InjectFilter : Logger::Loggable<Logger::Id::filter>, public StreamFilter, Grpc::AsyncRequestCallbacks<inject::InjectResponse> {
public:
 InjectFilter(InjectFilterConfigSharedPtr config): config_(config) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Http::StreamEncoderFilter
  virtual FilterHeadersStatus encodeHeaders(HeaderMap& headers, bool end_stream) override;
  virtual FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  virtual FilterTrailersStatus encodeTrailers(HeaderMap& trailers) override;
  virtual void setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onSuccess(std::unique_ptr<inject::InjectResponse>&& response) override;
  void onFailure(Grpc::Status::GrpcStatus status) override;

  enum class State { NotTriggered, SendingInjectRequest, InjectRequestSent, WaitingForUpstream, Done };
  State getState() { return state_; }  // testing aid

  static void removeNamedCookie(const std::string& cookie_name, Http::HeaderMap& headers);
  static void removeNamedCookie(const std::string& cookie_name, std::string& cookie_hdr_value);

  static bool matchAnyHeaders(const Http::HeaderMap& request_headers,
                              const std::vector<Router::ConfigUtility::HeaderData>& config_headers);

  static bool matchHeader(const Http::HeaderMap& request_headers,
                          const Router::ConfigUtility::HeaderData& config_header);

  static bool matchHeader(const Http::HeaderEntry& request_header,
                          const Router::ConfigUtility::HeaderData& config_header);

private:

  InjectFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* decoder_callbacks_;
  StreamEncoderFilterCallbacks* encoder_callbacks_;
  State state_{State::NotTriggered};
  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> client_;
  Grpc::AsyncRequest* req_{};
  HeaderMap* upstream_headers_;
  std::unique_ptr<inject::InjectResponse> inject_response_;
};

} // Http
} // Envoy
