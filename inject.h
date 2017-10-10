#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

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


class InjectAction {
public:
 InjectAction(std::vector<std::string> result, std::string action,
              std::vector<Http::LowerCaseString>& upstream_inject_headers,
              bool upstream_inject_any,
              std::vector<Http::LowerCaseString>& upstream_remove_headers,
              std::vector<std::string>& upstream_remove_cookie_names,
              std::vector<Http::LowerCaseString>& downstream_inject_headers,
              bool downstream_inject_any,
              std::vector<Http::LowerCaseString>& downstream_remove_headers,
              bool use_rpc_response,
              int response_code, std::map<std::string,std::string>& response_headers, std::string response_body):
    result_(result), action_(action),
    upstream_inject_headers_(upstream_inject_headers), upstream_inject_any_(upstream_inject_any),
    upstream_remove_headers_(upstream_remove_headers), upstream_remove_cookie_names_(upstream_remove_cookie_names),
    downstream_inject_headers_(downstream_inject_headers), downstream_inject_any_(downstream_inject_any),
    downstream_remove_headers_(downstream_remove_headers),
    use_rpc_response_(use_rpc_response), response_code_(response_code), response_headers_(response_headers), response_body_(response_body) { }

  const std::vector<std::string> result_;
  const std::string action_;
  const std::vector<Http::LowerCaseString> upstream_inject_headers_;
  const bool upstream_inject_any_;
  const std::vector<Http::LowerCaseString> upstream_remove_headers_;
  const std::vector<std::string> upstream_remove_cookie_names_;
  const std::vector<Http::LowerCaseString> downstream_inject_headers_;
  const bool downstream_inject_any_;
  const std::vector<Http::LowerCaseString> downstream_remove_headers_;
  const bool use_rpc_response_;
  const int response_code_;
  const std::map<std::string,std::string> response_headers_;
  const std::string response_body_;
};



class InjectActionMatcher {
public:
  InjectActionMatcher(int maxActions) {
    std::vector<Http::LowerCaseString> empty_lc_str_vec;
    std::vector<std::string> empty_str_vec;
    std::map<std::string,std::string> empty_hdrs;
    std::vector<std::string> result;
    result.push_back("local.any");

    actions_.reserve(maxActions+1);
    this->add(InjectAction(result, "abort",
                           empty_lc_str_vec, false,
                           empty_lc_str_vec, empty_str_vec,
                           empty_lc_str_vec, false,
                           empty_lc_str_vec, false,
                           500, empty_hdrs, ""));

  }

  // given result from injection response (e.g. "ok") find & return
  // approrpriate action. If no exact match or wildcard for any
  // injection response action (local.grpc-response) action is found
  // the "local.any" action is returned which defaults to abort/500.
  //
  // Inject responses should not use the "local." prefix on their
  // result string since that is just for 'local to Envoy' stuff like
  // errors. If they try to, the errorAction is used.
  const InjectAction& match(const std::string& result) const {
    if (result.find("local.") == 0) {
      return errorAction();
    }
    auto iaPair = action_map_.find(result);
    if (iaPair == action_map_.end()) {
      iaPair = action_map_.find("local.grpc-response");
    }
    if (iaPair == action_map_.end()) {
      // we have ensured this exists in ctor (user-provided config may
      // have overwritten it).
      iaPair = action_map_.find("local.any");
    }
    return *(iaPair->second);
  }

  const InjectAction& errorAction() const {
    auto iaPair = action_map_.find("local.error");
    if (iaPair != action_map_.end()) {
      return *(iaPair->second);
    }
    InjectAction* ia = action_map_.at("local.any");
    return *ia;
  }


  void add(InjectAction&& action) {
    actions_.push_back(std::move(action));
    InjectAction* ia = &actions_.back();
    for (unsigned int i = 0; i < ia->result_.size(); i++ ) {
      action_map_[ia->result_[i]] = ia;
    }
  }

 private:
  std::vector<InjectAction> actions_;
  std::map<std::string,InjectAction*> action_map_;
};

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
                     std::map<std::string,std::string> params,
                     Upstream::ClusterManager& cluster_mgr,
                     const std::string cluster_name,
                     int64_t timeout_ms,
                     const InjectActionMatcher& action_matcher):
    trigger_headers_(trigger_headers), trigger_cookie_names_(trigger_cookie_names), antitrigger_headers_(antitrigger_headers),
    always_triggered_(always_triggered), include_headers_(include_headers), include_all_headers_(include_all_headers),
    params_(params), cluster_name_(cluster_name), timeout_ms_(timeout_ms),
    cluster_mgr_(cluster_mgr), action_matcher_(action_matcher),
    method_descriptor_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders")) {
    ASSERT(Protobuf::DescriptorPool::generated_pool()->FindMethodByName("inject.InjectService.InjectHeaders"))
  }

  const std::vector<Router::ConfigUtility::HeaderData>& trigger_headers() { return trigger_headers_; }
  const std::vector<std::string>& trigger_cookie_names() { return trigger_cookie_names_; }
  const std::vector<Router::ConfigUtility::HeaderData>& antitrigger_headers() { return antitrigger_headers_; }
  bool always_triggered() { return always_triggered_; }
  const std::vector<Http::LowerCaseString>& include_headers() { return include_headers_; }
  bool include_all_headers() { return include_all_headers_; }
  std::map<std::string,std::string>& params() { return params_; }

  int64_t timeout_ms() { return timeout_ms_; }

  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> inject_client() {
    return std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>>(new Grpc::AsyncClientImpl<inject::InjectRequest,
                                                                                                 inject::InjectResponse>(cluster_mgr_, cluster_name_));
  }
  const google::protobuf::MethodDescriptor& method_descriptor() { return method_descriptor_; }
  const InjectActionMatcher& action_matcher() { return action_matcher_; }

 private:

  std::vector<Router::ConfigUtility::HeaderData> trigger_headers_;
  std::vector<std::string> trigger_cookie_names_;
  std::vector<Router::ConfigUtility::HeaderData> antitrigger_headers_;
  const bool always_triggered_;
  std::vector<Http::LowerCaseString> include_headers_;
  const bool include_all_headers_;
  std::map<std::string,std::string> params_;
  const std::string cluster_name_;
  const int64_t timeout_ms_;
  Upstream::ClusterManager& cluster_mgr_;
  const InjectActionMatcher& action_matcher_;
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
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message) override;

  enum class State { NotTriggered, SendingInjectRequest, InjectRequestSent, Aborting,  WaitingForUpstream, Done };
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

  void handleAction();
  void handleAbortAction();
  void handlePassThroughAction();

  InjectFilterConfigSharedPtr config_;
  StreamDecoderFilterCallbacks* decoder_callbacks_;
  StreamEncoderFilterCallbacks* encoder_callbacks_;
  State state_{State::NotTriggered};
  std::unique_ptr<Grpc::AsyncClientImpl<inject::InjectRequest, inject::InjectResponse>> client_;
  Grpc::AsyncRequest* req_{};
  HeaderMap* upstream_headers_;
  std::unique_ptr<inject::InjectResponse> inject_response_;
  const InjectAction* inject_action_;
};

} // Http
} // Envoy
