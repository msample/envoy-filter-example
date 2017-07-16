#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/common/exception.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/json/json_validator.h"
#include "inject.h"

namespace Envoy {
namespace Server {
namespace Configuration {

const std::string INJECT_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "type" : "object",
    "description": "JSON object to configrure an instance of the gRPC-powered header injection HTTP filter",
    "properties":{
      "trigger_headers" : {
        "type" : "array",
        "minItems" : 1,
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "if any of these request header names have non-empty values, attempt to add 'inject_headers' to the request"
      },
      "antitrigger_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "if any of these request header names have non-empty values, skip this attempt to inject headers even if trigger headers exist."
      },
      "include_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "these request headers will be added to the inject RPC call along with the trigger headers to compute the injected headers."
      },
      "inject_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "minItems" : 1,
        "items" : {"type" : "string"},
        "description": "names of headers desired & allowed be injected into the request. Included in inject RPC to indicate desired headers. Also prevents arbitrary header name injection."
      },
      "remove_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "only after successful injection, remove these headers - typically the trigger and include headers. Consider security - e.g remove session cookie after converting to short-lived jwt by injection."
      },
      "cluster_name": {
        "type" : "string",
        "description": "name of the upstream cluster to handle the gRPC call that computes the injected header(s)"
      }
    },
    "required": ["trigger_headers","inject_headers","cluster_name"],
    "additionalProperties": false
  }
)EOF"); // "

/**
 * Config registration for the header injection filter
 */
class InjectFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;
  std::string name() override { return "inject"; }
  HttpFilterType type() override { return HttpFilterType::Decoder; }
};

HttpFilterFactoryCb InjectFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string&,
                                                            FactoryContext& fac_ctx) {
  json_config.validateSchema(INJECT_SCHEMA);

  std::vector<std::string> thdrs = json_config.getStringArray("trigger_headers");
  std::vector<Http::LowerCaseString> thdrs_lc;
  std::vector<std::string> trigger_cookie_names;
  thdrs_lc.reserve(thdrs.size());
  for (std::string element : thdrs) {
    if (element.find("cookie.") == 0) {
        trigger_cookie_names.push_back(element.substr(7));
        continue;
    }
    Http::LowerCaseString lcstr(element);
    thdrs_lc.push_back(lcstr);
  }

  std::vector<Http::LowerCaseString> antithdrs_lc;
  if (json_config.hasObject("antitrigger_headers") ) {
    std::vector<std::string> antithdrs = json_config.getStringArray("antitrigger_headers");
    antithdrs_lc.reserve(antithdrs.size());
    for (std::string element : antithdrs) {
      Http::LowerCaseString lcstr(element);
      antithdrs_lc.push_back(lcstr);
    }
  }

  std::vector<Http::LowerCaseString> inc_hdrs_lc;
  if (json_config.hasObject("include_headers") ) {
    std::vector<std::string> inc_hdrs = json_config.getStringArray("include_headers");
    inc_hdrs_lc.reserve(inc_hdrs.size());
    for (std::string element : inc_hdrs) {
      Http::LowerCaseString lcstr(element);
      inc_hdrs_lc.push_back(lcstr);
    }
  }

  std::vector<std::string> inj_hdrs = json_config.getStringArray("inject_headers");
  std::vector<Http::LowerCaseString> inj_hdrs_lc;
  inj_hdrs_lc.reserve(inj_hdrs.size());
  for (std::string element : inj_hdrs) {
    Http::LowerCaseString lcstr(element);
    inj_hdrs_lc.push_back(lcstr);
  }

  std::vector<Http::LowerCaseString> remove_hdrs_lc;
  std::vector<std::string> remove_cookie_names;
  if (json_config.hasObject("remove_headers") ) {
    std::vector<std::string> remove_hdrs = json_config.getStringArray("remove_headers");
    remove_hdrs_lc.reserve(remove_hdrs.size());
    for (std::string element : remove_hdrs) {
      if (element.find("cookie.") == 0) {
        remove_cookie_names.push_back(element.substr(7));
        continue;
      }
      Http::LowerCaseString lcstr(element);
      remove_hdrs_lc.push_back(std::move(lcstr));
    }
  }

  const std::string& cluster_name = json_config.getString("cluster_name");

  // verify that target cluster exists
  if (!fac_ctx.clusterManager().get(cluster_name)) {
    throw EnvoyException("Inject filter requires 'cluster_name' cluster for gRPC inject request to be configured statically in the config file. No such cluster: " + cluster_name);
  }

  // nice to have: ensure no dups in trig vs include hdrs

  Http::InjectFilterConfigSharedPtr config(new Http::InjectFilterConfig(thdrs_lc, trigger_cookie_names, antithdrs_lc, inc_hdrs_lc,
                                                                        inj_hdrs_lc, remove_hdrs_lc, remove_cookie_names,
                                                                        fac_ctx.clusterManager(), cluster_name));
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        Http::StreamDecoderFilterSharedPtr{new Http::InjectFilter(config)});
  };

}

/**
 * Register Inject filter so http filter entries with name "inject"
 * and type "decoder" in the config will create this filter
 */
static Registry::RegisterFactory<InjectFilterConfig, NamedHttpFilterConfigFactory> register_;




} // Configuration
} // Server
} // Envoy
