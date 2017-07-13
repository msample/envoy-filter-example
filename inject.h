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
#include "common/grpc/async_client_impl.h"
#include "inject.pb.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {



/**
 * Global configuration for the Injector
 */
class InjectFilterConfig {
public:

 InjectFilterConfig(std::vector<Http::LowerCaseString>& trigger_headers,
                    std::vector<Http::LowerCaseString>& antitrigger_headers,
                    std::vector<Http::LowerCaseString>& include_headers,
                    std::vector<Http::LowerCaseString>& inject_headers,
                    std::vector<Http::LowerCaseString>& remove_headers,
                    Upstream::ClusterManager& cluster_mgr,
                    std::string cluster_name):
  trigger_headers_(trigger_headers), antitrigger_headers_(antitrigger_headers), include_headers_(include_headers),
    inject_headers_(inject_headers), remove_headers_(remove_headers),
    inject_client_(new Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>(cluster_mgr, cluster_name)),
    method_descriptor_(inject::inject::descriptor()->FindMethodByName("injectHeaders")) {}

  ~InjectFilterConfig() { std::cout << "Injectfilt config destroyed: " << this << std::endl; }

  const std::vector<Http::LowerCaseString>& trigger_headers() { return trigger_headers_; }
  const std::vector<Http::LowerCaseString>& antitrigger_headers() { return antitrigger_headers_; }
  const std::vector<Http::LowerCaseString>& include_headers() { return include_headers_; }
  const std::vector<Http::LowerCaseString>& inject_headers() { return inject_headers_; }
  const std::vector<Http::LowerCaseString>& remove_headers() { return remove_headers_; }
  std::shared_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client() { return inject_client_; }
  const google::protobuf::MethodDescriptor& method_descriptor() { return *method_descriptor_; }

 private:
  std::vector<Http::LowerCaseString> trigger_headers_;
  std::vector<Http::LowerCaseString> antitrigger_headers_;
  std::vector<Http::LowerCaseString> include_headers_;
  std::vector<Http::LowerCaseString> inject_headers_;
  std::vector<Http::LowerCaseString> remove_headers_;
  std::shared_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client_;
  const google::protobuf::MethodDescriptor* method_descriptor_;
};

typedef std::shared_ptr<InjectFilterConfig> InjectFilterConfigSharedPtr;

class InjectFilter : public StreamDecoderFilter, Grpc::AsyncClientCallbacks<inject::InjectResponse> {
public:
 InjectFilter(InjectFilterConfigSharedPtr config): config_(config) { std::cout << "created!" << this << std::endl; }
  ~InjectFilter() { std::cout << "destroyed: " << this << std::endl; }

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

  // Grpc::AsyncClientCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& ) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override;
  void onReceiveMessage(std::unique_ptr<inject::InjectResponse>&& ) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override;
  void onRemoteClose(Grpc::Status::GrpcStatus) override;

private:

  InjectFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_;
  bool inject_resp_received_;
  Grpc::AsyncClientStream<inject::InjectRequest>* req_{};
  HeaderMap* hdrs_;
};

} // Http
} // Envoy
