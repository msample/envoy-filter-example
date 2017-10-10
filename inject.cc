#include "inject.h"

#include <string>
#include <vector>
#include <chrono>

#include "common/grpc/common.h"
#include "common/http/header_map_impl.h"
#include "common/buffer/buffer_impl.h"

#define PINT(a) reinterpret_cast<unsigned long long>(a)

namespace Envoy {
namespace Http {

// called for gRPC call to InjectHeader
void InjectFilter::onCreateInitialMetadata(Http::HeaderMap& ) {
}

// called for gRPC call to InjectHeader
void InjectFilter::onSuccess(std::unique_ptr<inject::InjectResponse>&& resp) {
  ENVOY_LOG(trace,"InjectFilter::onSuccess (wasSending={}), cb on filter: {}",state_ == State::SendingInjectRequest, PINT(this));

  // put response & matching action in filter state then run appropriate handler
  inject_action_ = &config_->action_matcher().match(resp->result());
  inject_response_ = std::move(resp);
  handleAction();
}

// called for gRPC call to InjectHeader
void InjectFilter::onFailure(Grpc::Status::GrpcStatus status, const std::string& message) {
  bool wasSending =   state_ == State::SendingInjectRequest;
  ENVOY_LOG(warn,"onFailure({}), wasSending={}, msg='{}' called on icb: {}", status, wasSending, message,  PINT(this));
  inject_action_ = &config_->action_matcher().errorAction();
  handleAction();
}

void InjectFilter::onDestroy() {
  if (state_ == State::InjectRequestSent) {
    if (req_) {
      req_->cancel();
      req_ = nullptr;
    }
  }
  state_ = State::Done;
  ENVOY_LOG(trace,"decoder filter onDestroy called on: {}", PINT(this));
}


void InjectFilter::handleAction()  {
  if (inject_action_->action_ == "passthrough"
      || (inject_action_->action_ == "dynamic" && inject_response_->action() == "passthrough")) {
    handlePassThroughAction();
  } else {
    handleAbortAction();
  }
}

void InjectFilter::handlePassThroughAction()  {
  ENVOY_LOG(trace,"decoder filter handling passthrough (post inj response) {}", PINT(this));
  const InjectAction& action = *inject_action_;
  if (action.upstream_inject_any_) {
    // inject every header returned in gRPC response #trust
    for (int i = 0; i < inject_response_->upstreamheaders_size(); ++i) {
      const inject::Header& h = inject_response_->upstreamheaders(i);
      Http::LowerCaseString lckey(h.key().c_str());
      upstream_headers_->addCopy(lckey, h.value());
    }
    for (int i = 0; i < inject_response_->upstreamremoveheadernames_size(); ++i) {
      const std::string h = inject_response_->upstreamremoveheadernames(i);
      Http::LowerCaseString lckey(h.c_str());
      upstream_headers_->remove(lckey);
    }
  } else {
    // just inject the ones allowed by filter config
    std::map<std::string,std::string> inject_hdrs;
    for (int i = 0; i < inject_response_->upstreamheaders_size(); ++i) {
      const inject::Header& h = inject_response_->upstreamheaders(i);
      inject_hdrs.insert(std::pair<std::string,std::string>(h.key(), h.value()));
    }
    std::map<std::string,std::string> remove_hdrs;
    for (int i = 0; i < inject_response_->upstreamremoveheadernames_size(); ++i) {
      const std::string h = inject_response_->upstreamremoveheadernames(i);
      remove_hdrs.insert(std::pair<std::string,std::string>(h, h));
    }
    for (const Http::LowerCaseString& element : action.upstream_inject_headers_) {
      std::map<std::string,std::string>::iterator it = remove_hdrs.find(element.get());
      if (it != remove_hdrs.end()) {
        ENVOY_LOG(info, "Removing upstream {}",element.get());
        upstream_headers_->remove(element);
        continue;
      }
      it = inject_hdrs.find(element.get());
      if (it != inject_hdrs.end()) {
        upstream_headers_->remove(element);
        if (it->second != "") {
          ENVOY_LOG(info, "Injecting upstream {}:{}",element.get(), it->second);
          upstream_headers_->addReferenceKey(element, it->second);
        }
      }
    }
  }

  // remove any headers named in the config
  for (const Http::LowerCaseString& element : action.upstream_remove_headers_) {
    upstream_headers_->remove(element);
  }

  // remove any cookies as defined in config
  for (const std::string& name: action.upstream_remove_cookie_names_) {
    removeNamedCookie(name, *upstream_headers_);
  }

  bool wasSending =   state_ == State::SendingInjectRequest;
  state_ = State::WaitingForUpstream;
  ENVOY_LOG(trace,"exiting onSuccess on icb: {}", PINT(this));

  if (!wasSending) {
    ENVOY_LOG(trace,"InjectFilter::handlePassThrough CONTINUING DECODING), cb on filter: {}", PINT(this));
    // continue decoding if it won't be done by control flow yet to
    // return from our decodeHeaders call send()
    decoder_callbacks_->continueDecoding();
  }
}

// abort - hairpin with response to original response from here (in
// Envoy) instead of passing to upstream server for handling.
void InjectFilter::handleAbortAction()  {
  ENVOY_LOG(trace,"decoder filter handling abort (post inj response/its absence) {}", PINT(this));
  //bool wasSending =   state_ == State::SendingInjectRequest;
  state_ = State::Aborting;

  int statusCode = 0;
  std::string body;
  Http::HeaderMapImpl* response_headers{new Http::HeaderMapImpl{}};

  if (inject_action_->use_rpc_response_ && inject_response_ != NULL) {
    statusCode = inject_response_->response_code();
    body = inject_response_->response_body();
    for (int i = 0; i < inject_response_->response_header_size(); i++) {
      auto h = inject_response_->response_header(i);
      Http::LowerCaseString lckey(h.key());
      response_headers->addCopy(lckey, h.value());
    }
  }
  if (statusCode == 0) {
    statusCode = inject_action_->response_code_;
    body = inject_action_->response_body_;
    for( const auto& h_pair : inject_action_->response_headers_) {
      Http::LowerCaseString lckey(h_pair.first);
      response_headers->addCopy(lckey, h_pair.second);
    }
  }
  bool hasBody =  body.size() > 0;
  response_headers->addReferenceKey(Http::Headers::get().Status, std::to_string(statusCode));
  if (hasBody) {
    response_headers->addReferenceKey(Http::Headers::get().ContentLength, body.size());
  }

  ENVOY_LOG(trace,"Calling Encoders w hairpin response {}", PINT(this));
  Http::HeaderMapPtr rh(response_headers);
  decoder_callbacks_->encodeHeaders(std::move(rh), !hasBody);
  if (hasBody) {
    Buffer::OwnedImpl bodyBuf(body);
    decoder_callbacks_->encodeData(bodyBuf, true);
  }
  //Http::Utility::sendLocalReply(*decoder_callbacks_, false, static_cast<Http::Code>(statusCode), body);

  ENVOY_LOG(trace,"exiting decoder filter abort handling (post inj response/its absence) {}", PINT(this));
}


// decodeHeaders - see if any configured headers are present, and if so send them to
// the configured header injection service
FilterHeadersStatus InjectFilter::decodeHeaders(HeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace,"InjectFilter::decodeHeaders(end_stream={}) called on filter: {}", end_stream, PINT(this));
  // don't inject for internal calls
  if (headers.EnvoyInternalRequest() && (headers.EnvoyInternalRequest()->value() == "true")) {
    ENVOY_LOG(trace,"leaving InjectFilter::decodeHeaders, internal req, not triggered, filter inst: {}", PINT(this));
    return FilterHeadersStatus::Continue;
  }

  bool triggered = config_->always_triggered();

  // don't attempt to inject anything if any anti-trigger header is in
  // the request and we're not in always_triggered mode.
  if (!triggered ) {
    if (matchAnyHeaders(headers, config_->antitrigger_headers())) {
      ENVOY_LOG(trace,"leaving InjectFilter::decodeHeaders, antitrigger headers match, inst: {}", PINT(this));
      return FilterHeadersStatus::Continue;
    }
  }

  inject::InjectRequest ir; // sizeof is 72
  for (const Router::ConfigUtility::HeaderData& hd : config_->trigger_headers()) {
    const Http::HeaderEntry* h = headers.get(hd.name_);
    if (h == nullptr) {
      continue;
    }
    if (matchHeader(*h, hd)) {
      triggered = true;
      if (config_->include_all_headers()) {
        break;
      }
      inject::Header* ih = ir.mutable_inputheaders()->Add();
      ih->set_key(h->key().c_str());
      ih->set_value(h->value().c_str());
    }
  }

  // check for cookies with names that trigger injection and add them.
  if (!triggered || !config_->include_all_headers()) {
    for (const std::string& name: config_->trigger_cookie_names()) {
      std::string cookie_value = Http::Utility::parseCookieValue(headers, name);
      if (cookie_value.empty()) {
        continue;
      }
      triggered = true;
      if (config_->include_all_headers()) {
        break;
      }
      inject::Header* ih = ir.mutable_inputheaders()->Add();
      ih->mutable_key()->append("cookie.").append(name);
      ih->set_value(cookie_value.c_str());
    }
  }

  if (!triggered) {
    ENVOY_LOG(trace,"leaving InjectFilter::decodeHeaders, no triggered filter inst: {}", PINT(this));
    return FilterHeadersStatus::Continue;
  }
  ENVOY_LOG(info, "Inject trigger matched: {}", PINT(this));

  // add additional headers of interest to inject request
  if (config_->include_all_headers()) {
    headers.iterate([](const HeaderEntry& h, void* irp) -> void {
        inject::InjectRequest* ir = static_cast<inject::InjectRequest*>(irp);
        inject::Header* ih = ir->mutable_inputheaders()->Add();
        ih->set_key(h.key().c_str());
        ih->set_value(h.value().c_str());
      }, static_cast<void*>(&ir));
  } else {
    // just include extras asked for
    for (const Http::LowerCaseString& element : config_->include_headers()) {
      const Http::HeaderEntry* h = headers.get(element);
      if (h) {
        inject::Header* ih =  ir.mutable_inputheaders()->Add();
        ih->set_key(h->key().c_str());
        ih->set_value(h->value().c_str());
      }
    }
  }

  if (!config_->params().empty()) {
    google::protobuf::Map<std::string,std::string>* p = ir.mutable_params();
    for (std::map<std::string,std::string>::iterator it=config_->params().begin(); it!=config_->params().end(); it++) {
      (*p)[it->first] = it->second;
    }
  }

  client_ = config_->inject_client();

  // SendingInjectRequest state signals our onSuccess() impl to not
  // continuing decode if we get an inject response after passing
  // control to event loop via send() call below and onSuccess() is
  // called before it returns.
  state_ = State::SendingInjectRequest;
  req_ = client_->send(config_->method_descriptor(), ir, *this, std::chrono::milliseconds(config_->timeout_ms()));

  if (!req_) {
    ENVOY_LOG(warn, "Could not send inject gRPC request. Null req returned by send(). Using error action. {}", PINT(this));
    inject_action_ = &config_->action_matcher().errorAction();
    handleAction();
    if (state_ == State::WaitingForUpstream) {
      return FilterHeadersStatus::Continue;
    }
    return FilterHeadersStatus::StopIteration;
  }

  if (state_ == State::Aborting) {
    return FilterHeadersStatus::StopIteration;
  }

  if (state_ == State::SendingInjectRequest) {
    state_ = State::InjectRequestSent;
  }

  if (state_ == State::WaitingForUpstream) {
    // send call about yielded for long enough to get ersponse to inject request
    return FilterHeadersStatus::Continue;
  }

  // give control back to event loop so gRPC inject response or timeout
  // can initiate next steps. Stash headers for mutation based on response.
  upstream_headers_ = &headers;
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus InjectFilter::decodeData(Buffer::Instance&, bool end_stream) {
  ENVOY_LOG(trace,"InjectFilter::decodeData(end_stream={}) called on filter: {}", end_stream, PINT(this));
  if (state_ == State::Aborting) {
    return FilterDataStatus::StopIterationNoBuffer;
  }
  return state_ == State::InjectRequestSent ? FilterDataStatus::StopIterationAndBuffer
                                            : FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::decodeTrailers(HeaderMap&) {
  ENVOY_LOG(trace,"InjectFilter::decodeTrailers() called on filter: {}", PINT(this));
  if (state_ == State::Aborting) {
    return FilterTrailersStatus::StopIteration;
  }
  return state_ == State::InjectRequestSent ? FilterTrailersStatus::StopIteration
                                            : FilterTrailersStatus::Continue;
}

void InjectFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

FilterHeadersStatus InjectFilter::encodeHeaders(HeaderMap& headers, bool) {
  ENVOY_LOG(trace,"InjectFilter: encodeHeaders entered {}", PINT(this));
  if (state_ == State::NotTriggered) {
    return FilterHeadersStatus::Continue;
  }

  if (inject_response_ != nullptr) {
    if (inject_action_->downstream_inject_any_) {
      for (int i = 0; i < inject_response_->downstreamheaders_size(); ++i) {
        const inject::Header& h = inject_response_->downstreamheaders(i);
        Http::LowerCaseString lckey(h.key().c_str());
        headers.addCopy(lckey, h.value());
        ENVOY_LOG(info, "downstream injecting header {}: {}", h.key(), h.value());
      }
      for (int i = 0; i < inject_response_->downstreamremoveheadernames_size(); ++i) {
        const std::string h = inject_response_->downstreamremoveheadernames(i);
        Http::LowerCaseString lckey(h.c_str());
        headers.remove(lckey);
      }
    } else {
      std::map<std::string,std::string> inject_hdrs;
      for (int i = 0; i < inject_response_->downstreamheaders_size(); ++i) {
        const inject::Header& h = inject_response_->downstreamheaders(i);
        inject_hdrs.insert(std::pair<std::string,std::string>(h.key(), h.value()));
      }
      std::map<std::string,std::string> remove_hdrs;
      for (int i = 0; i < inject_response_->downstreamremoveheadernames_size(); ++i) {
        const std::string h = inject_response_->downstreamremoveheadernames(i);
        remove_hdrs.insert(std::pair<std::string,std::string>(h, h));
      }
      for (const Http::LowerCaseString& element : inject_action_->downstream_inject_headers_) {
        ENVOY_LOG(info, "downstream injecting {}",element.get());
        std::map<std::string,std::string>::iterator it = remove_hdrs.find(element.get());
        if (it != remove_hdrs.end()) {
          headers.remove(element);
          continue;
        }
        it = inject_hdrs.find(element.get());
        if (it != inject_hdrs.end()) {
          headers.remove(element);
          if (it->second != "") {
            headers.addReferenceKey(element, it->second);
          }
        }
      }
    }
  }
  // remove any headers named in the config
  for (const Http::LowerCaseString& element : inject_action_->downstream_remove_headers_) {
    ENVOY_LOG(info, "downstream removing header {}",element.get());
    headers.remove(element);
  }
  return FilterHeadersStatus::Continue;
}

FilterDataStatus InjectFilter::encodeData(Buffer::Instance&, bool) {
  ENVOY_LOG(trace,"InjectFilter: encodeData entered {}", PINT(this));
  return FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::encodeTrailers(HeaderMap&) {
  ENVOY_LOG(trace,"InjectFilter: encodeTrailers entered {}", PINT(this));
  return FilterTrailersStatus::Continue;
}

void InjectFilter::setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

// FIXME: move these cookie fcns into envoy cookie utils if wanted

static const Http::LowerCaseString cookie_hdr_name{"cookie"};

// Removes the cookie header from the headers and replaces it with one
// whose value does not include the named cookie(s).
void InjectFilter::removeNamedCookie(const std::string& cookie_name, Http::HeaderMap& headers) {
  const Http::HeaderEntry* h = headers.get(cookie_hdr_name);
  if (!h) {
    return;
  }
  std::string cookie_hdr_value(h->value().c_str());

  // optimization - no mutation if doesn't exist
  if (cookie_hdr_value.find(cookie_name + "=") == std::string::npos) {
    return;
  }
  // modify cookie hdr value
  removeNamedCookie(cookie_name, cookie_hdr_value);

  headers.remove(cookie_hdr_name);  // addStaticKey appends unless remove first
  if (!cookie_hdr_value.empty()) {
    headers.addReferenceKey(cookie_hdr_name, cookie_hdr_value);
  }
}

// Alters given cookie header value by removing cookie(s) of the given
// name (case sensitive).  Assumes cookie header value looks like
// "foo=fooval; bar=barval; baz=bazval",
// "foo=fooval;bar=barval;baz=bazval" or a combination of that spacing
//
void InjectFilter::removeNamedCookie(const std::string& cookie_name, std::string& cookie_hdr_value) {
  size_t start_idx = cookie_hdr_value.find(cookie_name + "=");

  // iterates in case the same cookie name appears multiple times
  while (start_idx != std::string::npos) {
    size_t end_prev_cookie_idx = std::string::npos;
    bool is_first = true;
    if (start_idx != 0) {
      size_t prev_semicolon_idx = cookie_hdr_value.find_last_of(";", start_idx-1);
      end_prev_cookie_idx = cookie_hdr_value.find_last_not_of(" ;", start_idx-1); // skip semi-colon too!
      if ((end_prev_cookie_idx != std::string::npos)
          && ((prev_semicolon_idx == std::string::npos)
              || (prev_semicolon_idx < end_prev_cookie_idx))) {
        // we matched in cookie data (RHS of equal) so leave as is
        start_idx = cookie_hdr_value.find(cookie_name + "=", start_idx + cookie_name.length() + 2); // any more?
        continue;
      }
      if (std::string::npos != end_prev_cookie_idx) {
        is_first = false;
      }
    }
    size_t end_idx = cookie_hdr_value.find(";", start_idx + cookie_name.length() + 2);
    bool is_last = true;
    size_t start_next_cookie_idx = std::string::npos;
    if (std::string::npos != end_idx) {
      start_next_cookie_idx = cookie_hdr_value.find_first_not_of(" ", end_idx+1);
      // only consider non-last if there's more than whitespace after the semi-colon
      if (start_next_cookie_idx != std::string::npos) {
        is_last = false;
      }
    }

    // this is only cookie value, blow away entire cookie value & return
    if (is_first && is_last) {
      cookie_hdr_value.clear();
      return;
    }

    // it's the last cookie. erase & return (we've found 'em all)
    if (is_last) {
      cookie_hdr_value.erase(end_prev_cookie_idx + 1);
      return;
    }

    // erase first cookie
    if (is_first) {
      cookie_hdr_value.erase(0, start_next_cookie_idx);
    } else {
      // erase middle cookie
      cookie_hdr_value.erase(end_prev_cookie_idx + 1, end_idx - end_prev_cookie_idx - 1);
    }
    start_idx = cookie_hdr_value.find(cookie_name + "="); // any more?
  }
}


// FIXME: move these match fcns into envoy Router::ConfigUtility

/**
 * See if any of the specified headers are present in the request headers.
 * @param request_headers supplies the list of headers to match
 * @param config_headers supplies the list of header matching constraints
 * @return true one or more for the config_headers match a request_headers entry
 */
bool InjectFilter::matchAnyHeaders(const Http::HeaderMap& request_headers,
                                   const std::vector<Router::ConfigUtility::HeaderData>& config_headers) {

  if (!config_headers.empty()) {
    for (const Router::ConfigUtility::HeaderData& config_header : config_headers) {
      if (matchHeader(request_headers, config_header)) {
        return true;
      }
    }
  }
  return false;
}

/**
 * See if any of the specified header is present in the request headers.
 * @param request_headers supplies the list of headers to match
 * @param config_header supplies the header macthing constraint
 * @return true if config_header matches one of the request_headers
 */
bool InjectFilter::matchHeader(const Http::HeaderMap& request_headers,
                               const Router::ConfigUtility::HeaderData& config_header) {
  const Http::HeaderEntry* header = request_headers.get(config_header.name_);
  if (header == nullptr) {
    return false;
  }
  return matchHeader(*header, config_header);
}

 bool InjectFilter::matchHeader(const Http::HeaderEntry& request_header,
                                const Router::ConfigUtility::HeaderData& config_header) {
  if (config_header.value_.empty()) {
    return true;
  }
  if (!config_header.is_regex_) {
        return request_header.value() == config_header.value_.c_str();
  }
  return std::regex_match(request_header.value().c_str(), config_header.regex_pattern_);
}


} // Http
} // Envoy
