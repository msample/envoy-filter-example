#include "inject.h"

#include <string>
#include <vector>
#include <chrono>

#include "common/grpc/common.h"

#define PINT(a) reinterpret_cast<unsigned long>(a)

namespace Envoy {
namespace Http {

// called for gRPC call to InjectHeader
void InjectFilter::onCreateInitialMetadata(Http::HeaderMap& ) {
}

// called for gRPC call to InjectHeader
void InjectFilter::onSuccess(std::unique_ptr<inject::InjectResponse>&& resp) {
  ENVOY_LOG(trace,"InjectFilter::onSuccess (wasSending={}), cb on filter: {}",state_ == State::SendingInjectRequest, PINT(this));
  std::map<std::string,std::string> inject_hdrs_;
  for (int i = 0; i < resp->headers_size(); ++i) {
    const inject::Header& h = resp->headers(i);
    inject_hdrs_.insert(std::pair<std::string,std::string>(h.key(), h.value()));
  }

  for (const Http::LowerCaseString& element : config_->upstream_inject_headers()) {
    ENVOY_LOG(info, "Injecting {}",element.get());
    std::map<std::string,std::string>::iterator it = inject_hdrs_.find(element.get());
    if (it != inject_hdrs_.end()) {
      hdrs_->addStaticKey(element, it->second);
    }
  }

  // remove any headers named in the config
  for (const Http::LowerCaseString& element : config_->upstream_remove_headers()) {
    hdrs_->remove(element);
  }

  // remove any cookies as defined in config
  for (const std::string& name: config_->upstream_remove_cookie_names()) {
    removeNamedCookie(name, *hdrs_);
  }

  bool wasSending =   state_ == State::SendingInjectRequest;
  state_ = State::WaitingForUpstream;
  ENVOY_LOG(trace,"exiting onSuccess on icb: {}", PINT(this));

  if (!wasSending) {
    // continue decoding if it won't be done by control flow yet to
    // return from our decodeHeaders call send()
    decoder_callbacks_->continueDecoding();
  }
}

// called for gRPC call to InjectHeader
void InjectFilter::onFailure(Grpc::Status::GrpcStatus status) {
  bool wasSending =   state_ == State::SendingInjectRequest;
  ENVOY_LOG(warn,"onFailure({}), wasSending={} called on icb: {}", status, wasSending, PINT(this));
  state_ = State::WaitingForUpstream;
  if (!wasSending) {
    // continue decoding if it won't be done by control flow yet to
    // return from our decodeHeaders call send()
    decoder_callbacks_->continueDecoding();
  }
}

void InjectFilter::onDestroy() {
  if (state_ == State::InjectRequestSent) {
    state_ = State::Done;
    if (req_) {
      req_->cancel();
    }
  }
  ENVOY_LOG(trace,"decoder filter onDestroy called on: {}", PINT(this));
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
    for (const Http::LowerCaseString& element : config_->antitrigger_headers()) {
      const Http::HeaderEntry* h = headers.get(element);
      if (h) {
        ENVOY_LOG(trace,"leaving InjectFilter::decodeHeaders, antitrigger header, inst: {}", PINT(this));
        return FilterHeadersStatus::Continue;
      }
    }
  }

  inject::InjectRequest ir;
  for (const Http::LowerCaseString& element : config_->trigger_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
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

  // add names of headers we want/allow injected to inject request
  for (const Http::LowerCaseString& element : config_->upstream_inject_headers()) {
    ir.add_injectheadernames(element.get());
  }

  client_ = config_->inject_client();

  // SendingInjectRequest state signals our onSuccess() impl to not
  // continuing decode if we get an inject response after passing
  // control to event loop via send() call below and onSuccess() is
  // called before it returns.
  state_ = State::SendingInjectRequest;
  req_ = client_->send(config_->method_descriptor(), ir, *this, std::chrono::milliseconds(config_->timeout_ms()));

  if (state_ == State::SendingInjectRequest) {
    state_ = State::InjectRequestSent;
  }

  if (!req_) {
    ENVOY_LOG(warn, "Could not send inject gRPC request. Null req returned by send(). {}", PINT(this));
    state_ = State::WaitingForUpstream;
    return FilterHeadersStatus::Continue;
  }

  if (state_ == State::WaitingForUpstream) {
    // send call about yielded for long enough to get answer
    return FilterHeadersStatus::Continue;
  }

  hdrs_ = &headers;
  // give control back to event loop so gRPC inject response can be received
  // FIXME: set timeout with dispatcher to either contine or fail the original call
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus InjectFilter::decodeData(Buffer::Instance&, bool end_stream) {
  ENVOY_LOG(trace,"InjectFilter::decodeData(end_stream={}) called on filter: {}", end_stream, PINT(this));
  return state_ == State::InjectRequestSent ? FilterDataStatus::StopIterationAndBuffer
                                            : FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::decodeTrailers(HeaderMap&) {
  ENVOY_LOG(trace,"InjectFilter::decodeTrailers() called on filter: {}", PINT(this));
  return state_ == State::InjectRequestSent ? FilterTrailersStatus::StopIteration
                                            : FilterTrailersStatus::Continue;
}

void InjectFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

FilterHeadersStatus InjectFilter::encodeHeaders(HeaderMap&, bool) {
  return FilterHeadersStatus::Continue;
}

FilterDataStatus InjectFilter::encodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::encodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void InjectFilter::setEncoderFilterCallbacks(StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}



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
    headers.addStaticKey(cookie_hdr_name, cookie_hdr_value);
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


} // Http
} // Envoy
