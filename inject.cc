#include "inject.h"

#include <string>
#include <vector>
#include <iostream>
#include <chrono>

#include "common/grpc/common.h"

namespace Envoy {
namespace Http {


//  To be completed - stubbed out callbacks for getting gRPC response to inject request
class InjectCallbacks: public Grpc::AsyncClientCallbacks<inject::InjectResponse> {

public:

  void onCreateInitialMetadata(Http::HeaderMap& ) { 
    std::cout << "called onCreateInitialMetadata on icb: " << this << std::endl;
  }

  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) { 
    std::cout << "called on onReceiveInitialMetadata on icb: " << this << std::endl;
  }

  void onReceiveMessage(std::unique_ptr<inject::InjectResponse>&& )  {
    std::cout << "called on onReceiveMessage on icb: " << this << std::endl;
  }

  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&)  { 
    std::cout << "called on onReceiveTrailingMetadata on icb: " << this << std::endl;
  }

  void onRemoteClose(Grpc::Status::GrpcStatus) {
    std::cout << "called on remote close on icb: " << this << std::endl;
  }
};


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

  // at work here...
  // InjectCallbacks* ic = new InjectCallbacks();
  // Grpc::AsyncClientStream<inject::InjectRequest>* req = config_->inject_client()->start(config_->method_descriptor(), *ic, std::chrono::milliseconds(10));
  // req->sendMessage(ir);

  // just add some test headers for now
  for (const Http::LowerCaseString& element : config_->inject_headers()) {
    std::cout << "Injecting " << element.get() << std::endl;
    headers.addStaticKey(element, "fofofo");
  }

  for (const Http::LowerCaseString& element : config_->remove_headers()) {
    headers.remove(element);
  }

  std::cout << "exiting decode headers filter method" << std::endl;
  return FilterHeadersStatus::Continue;
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
}



} // Http
} // Envoy
