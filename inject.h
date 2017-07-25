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

  InjectFilterConfig(std::vector<Http::LowerCaseString>& trigger_headers,
                     std::vector<std::string>& trigger_cookie_names,
                     std::vector<Http::LowerCaseString>& antitrigger_headers,
                     std::vector<Http::LowerCaseString>& include_headers,
                     std::vector<Http::LowerCaseString>& upstream_inject_headers,
                     std::vector<Http::LowerCaseString>& upstream_remove_headers,
                     std::vector<std::string>& upstream_remove_cookie_names,
                     Upstream::ClusterManager& cluster_mgr,
                     std::string cluster_name):
  trigger_headers_(trigger_headers), trigger_cookie_names_(trigger_cookie_names), antitrigger_headers_(antitrigger_headers),
    include_headers_(include_headers), upstream_inject_headers_(upstream_inject_headers), upstream_remove_headers_(upstream_remove_headers),
    upstream_remove_cookie_names_(upstream_remove_cookie_names),
    //inject_client_(new Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>(cluster_mgr, cluster_name);),
    cluster_name_(cluster_name), cluster_mgr_(cluster_mgr),
    method_descriptor_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders")) {
    ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders"))
  }

  const std::vector<Http::LowerCaseString>& trigger_headers() { return trigger_headers_; }
  const std::vector<std::string>& trigger_cookie_names() { return trigger_cookie_names_; }
  const std::vector<Http::LowerCaseString>& antitrigger_headers() { return antitrigger_headers_; }
  const std::vector<Http::LowerCaseString>& include_headers() { return include_headers_; }
  const std::vector<Http::LowerCaseString>& upstream_inject_headers() { return upstream_inject_headers_; }
  const std::vector<Http::LowerCaseString>& upstream_remove_headers() { return upstream_remove_headers_; }
  const std::vector<std::string>& upstream_remove_cookie_names() { return upstream_remove_cookie_names_; }

  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client() {
    return std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>>(new Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>(cluster_mgr_, cluster_name_));
  }
  const google::protobuf::MethodDescriptor& method_descriptor() { return method_descriptor_; }

 private:
  std::vector<Http::LowerCaseString> trigger_headers_;
  std::vector<std::string> trigger_cookie_names_;
  std::vector<Http::LowerCaseString> antitrigger_headers_;
  std::vector<Http::LowerCaseString> include_headers_;
  std::vector<Http::LowerCaseString> upstream_inject_headers_;
  std::vector<Http::LowerCaseString> upstream_remove_headers_;
  std::vector<std::string> upstream_remove_cookie_names_;
  std::string cluster_name_;
  Upstream::ClusterManager& cluster_mgr_;
  //std::shared_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client_;
  const google::protobuf::MethodDescriptor& method_descriptor_;
};

typedef std::shared_ptr<InjectFilterConfig> InjectFilterConfigSharedPtr;

class InjectFilter : Logger::Loggable<Logger::Id::filter>, public StreamDecoderFilter, Grpc::AsyncRequestCallbacks<inject::InjectResponse> {
public:
 InjectFilter(InjectFilterConfigSharedPtr config): config_(config) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onSuccess(std::unique_ptr<inject::InjectResponse>&& response) override;
  void onFailure(Grpc::Status::GrpcStatus status) override;

  static void removeNamedCookie(const std::string& key, Http::HeaderMap& headers);
  static void removeNamedCookie(const std::string& cookie_name, std::string& cookie_hdr_value);
private:

  InjectFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_;
  bool inject_resp_received_;
  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> client_;
  Grpc::AsyncRequest* req_{};
  HeaderMap* hdrs_;
};

} // Http
} // Envoy
