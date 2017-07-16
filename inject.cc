#include "inject.h"

#include <string>
#include <vector>
#include <iostream>
#include <chrono>

#include "common/grpc/common.h"

namespace Envoy {
namespace Http {

// called for gRPC call to InjectHeader
void InjectFilter::onCreateInitialMetadata(Http::HeaderMap& ) {
}

// called for gRPC call to InjectHeader
void InjectFilter::onReceiveInitialMetadata(Http::HeaderMapPtr&&) {
}

// called for gRPC call to InjectHeader
void InjectFilter::onReceiveMessage(std::unique_ptr<inject::InjectResponse>&& resp) {
  std::cout << "called on onReceiveMessage on icb: " << this << std::endl;
  if (req_) {
    std::cout << "onRecvMsmg non-nil req_ on  " << this << std::endl;
    req_->resetStream();
    req_ = 0;
  }
  std::map<std::string,std::string> inject_hdrs_;
  for (int i = 0; i < resp->headers_size(); ++i) {
    const inject::Header& h = resp->headers(i);
    inject_hdrs_.insert(std::pair<std::string,std::string>(h.key(), h.value()));
  }

  for (const Http::LowerCaseString& element : config_->inject_headers()) {
    std::cout << "Injecting " << element.get() << std::endl;
    std::map<std::string,std::string>::iterator it = inject_hdrs_.find(element.get());
    if (it != inject_hdrs_.end()) {
      hdrs_->addStaticKey(element, it->second);
    }
  }

  // remove any headers named in the config
  for (const Http::LowerCaseString& element : config_->remove_headers()) {
    hdrs_->remove(element);
  }

  // remove any cookies as defined in config
  for (const std::string& name: config_->remove_cookie_names()) {
    removeNamedCookie(name, *hdrs_);
  }

  callbacks_->continueDecoding();
  std::cout << "exiting onReceiveMessage on icb: " << this << std::endl;
}

// called for gRPC call to InjectHeader
void InjectFilter::onReceiveTrailingMetadata(Http::HeaderMapPtr&&)  {
  std::cout << "called on onReceiveTrailingMetadata on icb: " << this << std::endl;
}

// called for gRPC call to InjectHeader
void InjectFilter::onRemoteClose(Grpc::Status::GrpcStatus) {
  std::cout << "onRemoteClose called on icb: " << this << std::endl;
}

// decodeHeaders - see if any configured headers are present, and if so send them to
// the configured header injection service
FilterHeadersStatus InjectFilter::decodeHeaders(HeaderMap& headers, bool) {

  // don't inject for internal calls
  if (headers.EnvoyInternalRequest() && (headers.EnvoyInternalRequest()->value() == "true")) {
      return FilterHeadersStatus::Continue;
  }

  // don't attempt to inject anything if any anti-trigger header is in the request
  for (const Http::LowerCaseString& element : config_->antitrigger_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
      return FilterHeadersStatus::Continue;
    }
  }

  inject::InjectRequest ir;
  bool triggered = false;
  for (const Http::LowerCaseString& element : config_->trigger_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
      triggered = true;
      inject::Header* h =  ir.mutable_inputheaders()->Add();
      h->set_key(h->key().c_str());
      h->set_value(h->value().c_str());
    }
  }

  // check for cookies with names that trigger injection
  for (const std::string& name: config_->trigger_cookie_names()) {
    std::string cookie_value = Http::Utility::parseCookieValue(headers, name);
    if (cookie_value.empty()) {
      continue;
    }
    triggered = true;
    inject::Header* h =  ir.mutable_inputheaders()->Add();
    h->set_key(name);
    h->set_value(cookie_value);
  }

  if (!triggered) {
    return FilterHeadersStatus::Continue;
  }
  std::cout << "Inject trigger matched: " << this << std::endl;

  // add non-triggering headers of interest to inject request
  for (const Http::LowerCaseString& element : config_->include_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
      inject::Header* h =  ir.mutable_inputheaders()->Add();
      h->set_key(h->key().c_str());
      h->set_value(h->value().c_str());
    }
  }

  // add names of headers we want/allow injected to inject request
  for (const Http::LowerCaseString& element : config_->inject_headers()) {
    ir.add_injectheadernames(element.get());
  }


  // issues here - if don't reset stream in this fcn (we can't if we want the reply) we get seg fault.
  // req_ = config_->inject_client()->start(config_->method_descriptor(), *this, std::chrono::milliseconds(10));
  // req_->sendMessage(ir);
  // req_->closeStream();
  // no segv if reset... but no progress either
  // req_->resetStream();

  // START TEMP SECTION - REMOVE WHEN gRPC working
  // demo - fake out injected headers
  for (const Http::LowerCaseString& element : config_->inject_headers()) {
    headers.addStaticKey(element, "example-injected-header-value");
  }

  // remove any cookies as defined in config
  for (const std::string& name: config_->remove_cookie_names()) {
    removeNamedCookie(name, headers);
  }

  // remove any headers as defined in config
  for (const Http::LowerCaseString& name: config_->remove_headers()) {
    headers.remove(name);
  }

  // pretend we're done
  return FilterHeadersStatus::Continue;
  // END TEMP SECTION - REMOVE WHEN gRPC working

  hdrs_ = &headers;
  // give control back to event loop so gRPC inject response can be received
  // FIXME: set timeout with dispatcher to either contine or fail the original call
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus InjectFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void InjectFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void InjectFilter::onDestroy() {
  std::cout << "decoder filter onDestroy called on: " << this << std::endl;
  if (req_) {
    std::cout << "req_ non-nil" << std::endl;
    req_->resetStream();
  }
}

static const Http::LowerCaseString cookie_hdr_name{"cookie"};

// Removes the cookie header from the headers and replaces it with one
// whose value does not include the named cookie(s).
void InjectFilter::removeNamedCookie(const std::string& key, Http::HeaderMap& headers) {
  const Http::HeaderEntry* h = headers.get(cookie_hdr_name);
  if (!h) {
    return;
  }
  std::string cookie_hdr_value(h->value().c_str());

  // optimization - no mutation if doesn't exist
  if (cookie_hdr_value.find(key + "=") == std::string::npos) {
    return;
  }
  // modify cookie hdr value
  removeNamedCookie(key, cookie_hdr_value);

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
      end_prev_cookie_idx = cookie_hdr_value.find_last_not_of(" ;", start_idx-1); // skip semi-colon too!
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
