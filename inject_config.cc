#include "inject_config.h"

#include "envoy/registry/registry.h"
#include "envoy/common/exception.h"
#include "common/json/config_schemas.h"
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
      "always_triggered" : {
        "type" : "boolean",
        "description": "ignore trigger and antrigger configuration and run on every request. Defaults to false."
      },
      "include_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "these request headers will be added to the inject RPC call along with the trigger headers to compute the injected headers."
      },
      "include_all_headers" : {
        "type" : "boolean",
        "description": "Send all headers and pseudo headers with the gRPC inject request. if true, include_headers is ignored. Defaults to false."
      },
      "upstream_inject_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "minItems" : 1,
        "items" : {"type" : "string"},
        "description": "names of headers desired & allowed be injected into the request. Included in inject RPC to indicate desired headers. Also prevents arbitrary header name injection."
      },
      "upstream_inject_any" : {
        "type" : "boolean",
        "description": "if true, inject all upstream headers returned in gRPC response, not just those in upstream_inject_headers."
      },
      "upstream_remove_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "only after successful injection, remove these headers - typically the trigger and include headers. Consider security - e.g remove session cookie after converting to short-lived jwt by injection."
      },
      "downstream_inject_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "minItems" : 1,
        "items" : {"type" : "string"},
        "description": "names of headers desired & allowed be injected into the downstream response. Included in inject RPC to indicate desired headers. Also prevents arbitrary header name injection."
      },
      "downstream_inject_any" : {
        "type" : "boolean",
        "description": "if true, inject all downstream headers returned in gRPC response, not just those in downstream_inject_headers."
      },
      "downstream_remove_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "string"},
        "description": "only after successful injection, remove these headers from the downstream response."
      },
      "cluster_name": {
        "type" : "string",
        "description": "name of the upstream cluster to handle the gRPC call that computes the injected header(s)"
      },
      "timeout_ms": {
        "type" : "integer",
        "minimum": 1,
        "description": "milliseconds to wait for gRPC response before taking configurable error handling action. Defaults to 120."
      }
    },
    "required": ["cluster_name"],
    "additionalProperties": false
  }
)EOF"); // "

/**
 * Register Inject filter so http filter entries with name "inject"
 * and type "decoder" in the config will create this filter
 */
static Registry::RegisterFactory<InjectFilterConfig, NamedHttpFilterConfigFactory> register_;


HttpFilterFactoryCb InjectFilterConfig::createFilterFactory(const Json::Object& json_config,
                                                            const std::string& statsd_prefix,
                                                            FactoryContext& fac_ctx) {

  Http::InjectFilterConfigSharedPtr config = createConfig(json_config, statsd_prefix, fac_ctx);
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        Http::StreamFilterSharedPtr{new Http::InjectFilter(config)});
  };

}

Http::InjectFilterConfigSharedPtr InjectFilterConfig::createConfig(const Json::Object& json_config,
                                                                   const std::string&,
                                                                   FactoryContext& fac_ctx) {
  json_config.validateSchema(INJECT_SCHEMA);

  std::vector<Http::LowerCaseString> thdrs_lc;
  std::vector<std::string> trigger_cookie_names;
  if (json_config.hasObject("trigger_headers") ) {
    std::vector<std::string> thdrs = json_config.getStringArray("trigger_headers");
    thdrs_lc.reserve(thdrs.size());
    for (std::string element : thdrs) {
      if (element.find("cookie.") == 0) {
        trigger_cookie_names.push_back(element.substr(7));
        continue;
      }
      Http::LowerCaseString lcstr(element);
      thdrs_lc.push_back(lcstr);
    }
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

  std::vector<Http::LowerCaseString> upstream_inject_headers_lc;
  if (json_config.hasObject("upstream_inject_headers") ) {
    std::vector<std::string> upstream_inject_headers = json_config.getStringArray("upstream_inject_headers");
    upstream_inject_headers_lc.reserve(upstream_inject_headers.size());
    for (std::string element : upstream_inject_headers) {
      Http::LowerCaseString lcstr(element);
      upstream_inject_headers_lc.push_back(lcstr);
    }
  }

  std::vector<Http::LowerCaseString> upstream_remove_headers_lc;
  std::vector<std::string> upstream_remove_cookie_names;
  if (json_config.hasObject("upstream_remove_headers") ) {
    std::vector<std::string> upstream_remove_headers = json_config.getStringArray("upstream_remove_headers");
    upstream_remove_headers_lc.reserve(upstream_remove_headers.size());
    for (std::string element : upstream_remove_headers) {
      if (element.find("cookie.") == 0) {
        upstream_remove_cookie_names.push_back(element.substr(7));
        continue;
      }
      Http::LowerCaseString lcstr(element);
      upstream_remove_headers_lc.push_back(std::move(lcstr));
    }
  }

  std::vector<Http::LowerCaseString> downstream_inject_headers_lc;
  if (json_config.hasObject("downstream_inject_headers") ) {
    std::vector<std::string> downstream_inject_headers = json_config.getStringArray("downstream_inject_headers");
    downstream_inject_headers_lc.reserve(downstream_inject_headers.size());
    for (std::string element : downstream_inject_headers) {
      Http::LowerCaseString lcstr(element);
      downstream_inject_headers_lc.push_back(lcstr);
    }
  }

  std::vector<Http::LowerCaseString> downstream_remove_headers_lc;
  if (json_config.hasObject("downstream_remove_headers") ) {
    std::vector<std::string> downstream_remove_headers = json_config.getStringArray("downstream_remove_headers");
    downstream_remove_headers_lc.reserve(downstream_remove_headers.size());
    for (std::string element : downstream_remove_headers) {
      Http::LowerCaseString lcstr(element);
      downstream_remove_headers_lc.push_back(std::move(lcstr));
    }
  }

  const std::string& cluster_name = json_config.getString("cluster_name");
  const int64_t timeout_ms = json_config.getInteger("timeout_ms", 120);
  const bool always_triggered = json_config.getBoolean("always_triggered", false);
  const bool include_all_headers = json_config.getBoolean("include_all_headers", false);
  const bool upstream_inject_any  = json_config.getBoolean("upstream_inject_any", false);
  const bool downstream_inject_any  = json_config.getBoolean("downstream_inject_any", false);

  bool always_triggered_not_specified  = json_config.getBoolean("always_triggered", true) && !json_config.getBoolean("always_triggered", false);
  bool disabled = !always_triggered_not_specified && !always_triggered  && (thdrs_lc.size() == 0) && (trigger_cookie_names.size() == 0);

  if ((thdrs_lc.size() == 0) && (trigger_cookie_names.size() == 0) && !disabled) {
    throw EnvoyException("Inject filter requires a non-empty trigger_headers list or always_triggered to be explicitly set.");
  }

  // ensure we have something to do unless disabled
  if (!disabled && ((upstream_inject_headers_lc.size() == 0) && !upstream_inject_any) &&
      ((downstream_inject_headers_lc.size() == 0) && !downstream_inject_any)) {
    throw EnvoyException("Inject filter has no effect - requires some headers to inject, either upstream, downstream or both.");
  }

  // verify that target cluster exists
  if (!fac_ctx.clusterManager().get(cluster_name)) {
    throw EnvoyException("Inject filter requires 'cluster_name' cluster for gRPC inject request to be configured statically in the config file. No such cluster: " + cluster_name);
  }
  // nice to have: ensure no dups in trig vs include hdrs
  Http::InjectFilterConfigSharedPtr config(new Http::InjectFilterConfig(thdrs_lc, trigger_cookie_names, antithdrs_lc, always_triggered, inc_hdrs_lc,
                                                                        include_all_headers, upstream_inject_headers_lc, upstream_inject_any,
                                                                        upstream_remove_headers_lc, upstream_remove_cookie_names,
                                                                        downstream_inject_headers_lc, downstream_inject_any, downstream_remove_headers_lc,
                                                                        fac_ctx.clusterManager(), cluster_name, timeout_ms));
  return config;
}





} // Configuration
} // Server
} // Envoy
