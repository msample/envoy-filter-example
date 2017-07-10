#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

  const std::vector<Http::LowerCaseString>& trigger_headers() { return trigger_headers_; }
  const std::vector<Http::LowerCaseString>& antitrigger_headers() { return antitrigger_headers_; }
  const std::vector<Http::LowerCaseString>& include_headers() { return include_headers_; }
  const std::vector<Http::LowerCaseString>& inject_headers() { return inject_headers_; }
  const std::vector<Http::LowerCaseString>& remove_headers() { return remove_headers_; }
  Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>* inject_client() { return inject_client_; }
  const google::protobuf::MethodDescriptor& method_descriptor() { return *method_descriptor_; }

 private:
  std::vector<Http::LowerCaseString> trigger_headers_;
  std::vector<Http::LowerCaseString> antitrigger_headers_;
  std::vector<Http::LowerCaseString> include_headers_;
  std::vector<Http::LowerCaseString> inject_headers_;
  std::vector<Http::LowerCaseString> remove_headers_;
  Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>* inject_client_;
  const google::protobuf::MethodDescriptor* method_descriptor_;
};

typedef std::shared_ptr<InjectFilterConfig> InjectFilterConfigSharedPtr;

class InjectFilter : public StreamDecoderFilter {
public:
 InjectFilter(InjectFilterConfigSharedPtr config): config_(config) {}

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  FilterHeadersStatus decodeHeaders(HeaderMap& headers, bool end_stream) override;
  FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  FilterTrailersStatus decodeTrailers(HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) override;

private:

  InjectFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* callbacks_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
};

} // Http
} // Envoy
