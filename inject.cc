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
  std::cout << "called onCreateInitialMetadata on icb: " << this << std::endl;
}

// called for gRPC call to InjectHeader
void InjectFilter::onReceiveInitialMetadata(Http::HeaderMapPtr&&) {
  std::cout << "called on onReceiveInitialMetadata on icb: " << this << std::endl;
}

// called for gRPC call to InjectHeader
void InjectFilter::onReceiveMessage(std::unique_ptr<inject::InjectResponse>&& resp)  {
  std::cout << "called on onReceiveMessage on icb: " << this << std::endl;
  if (req_) {
    req_->resetStream();
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

  for (const Http::LowerCaseString& element : config_->remove_headers()) {
    hdrs_->remove(element);
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
  std::cout << "called on onRemoteClose on icb: " << this << std::endl;
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

  bool triggered = false;
  std::vector<const Http::HeaderEntry*> to_send;
  for (const Http::LowerCaseString& element : config_->trigger_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
      if (!triggered) {
        triggered = true;
        to_send.reserve(config_->trigger_headers().size() + config_->include_headers().size());
      }
      to_send.push_back(h);
    }
  }

  if (!triggered) {
    return FilterHeadersStatus::Continue;
  }

  for (const Http::LowerCaseString& element : config_->include_headers()) {
    const Http::HeaderEntry* h = headers.get(element);
    if (h) {
      to_send.push_back(h);
    }
  }
  std::cout << "Inject trigger matched" << std::endl;

  inject::InjectRequest ir;

  for (const Http::HeaderEntry* element : to_send) {
    inject::Header* h =  ir.mutable_inputheaders()->Add();
    h->set_key(element->key().c_str());
    h->set_value(element->value().c_str());
  }

  for (const Http::LowerCaseString& element : config_->inject_headers()) {
    ir.add_injectheadernames(element.get());
  }

  //  issues here - if don't reset stream in this fcn (we can't if we want the reply) we get seg fault.
  //std::unique_ptr<Grpc::AsyncClientStream<inject::InjectRequest>> x{config_->inject_client()->start(config_->method_descriptor(), *this, std::chrono::milliseconds(10))};
  //req_ = std::move(x);
  req_ = std::move(config_->inject_client()->start(config_->method_descriptor(), *this, std::chrono::milliseconds(10)));
  req_->sendMessage(ir);
  req_->closeStream();

  // no segv if do this... but no progress either
  // req_->resetStream();

  hdrs_ = &headers;
  // give control back to event loop so gRPC inject response can be received
  // FIXME: set timeout with dispatcher to either contine or fail the original call
  std::cout << "exiting decode headers filter method, waiting for grpc response" << std::endl;
  return FilterHeadersStatus::StopIteration;
}

FilterDataStatus InjectFilter::decodeData(Buffer::Instance&, bool) {
  std::cout << "decode data called on: " << this << std::endl;
  return FilterDataStatus::Continue;
}

FilterTrailersStatus InjectFilter::decodeTrailers(HeaderMap&) {
  std::cout << "decode trailers called on: " << this << std::endl;
  return FilterTrailersStatus::Continue;
}

void InjectFilter::setDecoderFilterCallbacks(StreamDecoderFilterCallbacks& callbacks) {
  std::cout << "decoder filter callbacks set on: " << this << std::endl;
  callbacks_ = &callbacks;
}

void InjectFilter::onDestroy() {
  std::cout << "decoder filter onDestroy called on: " << this << std::endl;
  if (req_) {
    req_->resetStream();
  }
}




} // Http
} // Envoy
