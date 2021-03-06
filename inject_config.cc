#include "inject_config.h"

#include "envoy/registry/registry.h"
#include "envoy/common/exception.h"
#include "common/json/config_schemas.h"
#include "common/json/json_validator.h"
#include "inject.h"
#include <iostream>

namespace Envoy {
namespace Server {
namespace Configuration {

const std::string INJECT_SCHEMA(R"EOF(
  {
    "$schema": "http://json-schema.org/schema#",
    "type" : "object",
    "description": "JSON object to configrure an instance of the gRPC-powered header injection HTTP filter",
    "required": ["cluster_name"],
    "additionalProperties": false,
    "properties":{
      "trigger_headers" : {
        "type" : "array",
        "minItems" : 1,
        "uniqueItems" : true,
        "items" : {"type" : "object"},
        "description": "if any of these request header constraints have non-empty values, attempt to add 'inject_headers' to the request"
      },
      "antitrigger_headers" : {
        "type" : "array",
        "uniqueItems" : true,
        "items" : {"type" : "object"},
        "description": "if any of these request header constraints have non-empty values, skip this attempt to inject headers even if trigger headers exist."
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
      "params" : {
        "type" : "object",
        "additionalProperties" : true,
        "description": "opaque k/vs (string,string) to pass to the gRCP injection service. Optional. Use these to control implementation specific behaviour (e.g. testing)"
      },
      "cluster_name": {
        "type" : "string",
        "description": "name of the upstream cluster to handle the gRPC call that computes the injected header(s)"
      },
      "timeout_ms": {
        "type" : "integer",
        "minimum": 1,
        "description": "milliseconds to wait for gRPC response before taking configurable error handling action. Defaults to 120."
      },
      "actions": {
        "type" : "array",
        "minimum": 1,
        "description": "configure reaction to inject request timeouts, errors and response codes. For example passing the request on anyway or aborting with an 503.",
        "items" : {
          "type" : "object",
          "required": ["result"],
          "additionalProperties": false,
          "properties":{
            "result" : {
              "type" : "array",
              "minItems" : 1,
              "items" : {"type" : "string"}
            },
            "action" : {
              "type" : "string",
              "description": "passthrough, abort, dynamic"
            },
            "upstream_inject_headers" : {
              "type" : "array",
              "uniqueItems" : true,
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
            "use_rpc_response" : {
              "type" : "boolean"
            },
            "response_code" : { "type": "integer" },
            "response_headers" : {
              "type": "array",
              "items": {
                "type": "object",
                "additionalProperties": false,
                "properties" : {
                  "key" : { "type": "string" },
                  "value" : { "type": "string" }
                }
              }
            },
            "response_body" : { "type": "string" }
          }
        }
      }
    }
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


  // CLEANUP - see envoy/source/common/router/config_utility.h
  json_config.validateSchema(INJECT_SCHEMA);

  std::vector<Router::ConfigUtility::HeaderData> trigger_headers;
  std::vector<std::string> trigger_cookie_names;
  if (json_config.hasObject("trigger_headers") ) {
    std::vector<Json::ObjectSharedPtr> thdrs = json_config.getObjectArray("trigger_headers");
    trigger_headers.reserve(thdrs.size());
    for (Json::ObjectSharedPtr element : thdrs) {
      Router::ConfigUtility::HeaderData hd(*element);
      if (hd.name_.get().find("cookie.") == 0) {
        trigger_cookie_names.push_back(element->getString("name").substr(7));
        continue;
      }
      trigger_headers.push_back(hd);
    }
  }

  std::vector<Router::ConfigUtility::HeaderData> antitrigger_headers;
  if (json_config.hasObject("antitrigger_headers") ) {
    std::vector<Json::ObjectSharedPtr> antithdrs = json_config.getObjectArray("antitrigger_headers");
    antitrigger_headers.reserve(antithdrs.size());
    for (Json::ObjectSharedPtr element : antithdrs) {
      Router::ConfigUtility::HeaderData hd(*element);
      antitrigger_headers.push_back(hd);
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

  std::map<std::string,std::string> params;
  if (json_config.hasObject("params") ) {
    Json::ObjectSharedPtr p = json_config.getObject("params");
    p->iterate([&params](const std::string& name, const Json::Object& value) {
        params.insert(std::pair<std::string,std::string>(name, value.asString()));
        return true;
      });
  }

  Http::InjectActionMatcher* action_matcher;
  if (json_config.hasObject("actions") ) {
    std::vector<Json::ObjectSharedPtr> actions = json_config.getObjectArray("actions");
    action_matcher = new Http::InjectActionMatcher(actions.size());
    for (Json::ObjectSharedPtr action: actions) {

      std::vector<Http::LowerCaseString> upstream_inject_headers_lc;
      if (action->hasObject("upstream_inject_headers") ) {
        std::vector<std::string> upstream_inject_headers = action->getStringArray("upstream_inject_headers");
        upstream_inject_headers_lc.reserve(upstream_inject_headers.size());
        for (std::string element : upstream_inject_headers) {
          Http::LowerCaseString lcstr(element);
          upstream_inject_headers_lc.push_back(lcstr);
        }
      }

      std::vector<Http::LowerCaseString> upstream_remove_headers_lc;
      std::vector<std::string> upstream_remove_cookie_names;
      if (action->hasObject("upstream_remove_headers") ) {
        std::vector<std::string> upstream_remove_headers = action->getStringArray("upstream_remove_headers");
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
      if (action->hasObject("downstream_inject_headers") ) {
        std::vector<std::string> downstream_inject_headers = action->getStringArray("downstream_inject_headers");
        downstream_inject_headers_lc.reserve(downstream_inject_headers.size());
        for (std::string element : downstream_inject_headers) {
          Http::LowerCaseString lcstr(element);
          downstream_inject_headers_lc.push_back(lcstr);
        }
      }

      std::vector<Http::LowerCaseString> downstream_remove_headers_lc;
      if (action->hasObject("downstream_remove_headers") ) {
        std::vector<std::string> downstream_remove_headers = action->getStringArray("downstream_remove_headers");
        downstream_remove_headers_lc.reserve(downstream_remove_headers.size());
        for (std::string element : downstream_remove_headers) {
          Http::LowerCaseString lcstr(element);
          downstream_remove_headers_lc.push_back(std::move(lcstr));
        }
      }

      const bool upstream_inject_any  = action->getBoolean("upstream_inject_any", false);
      const bool downstream_inject_any  = action->getBoolean("downstream_inject_any", false);
      std::map<std::string,std::string> response_headers; // fixme

      /*
      std::vector<std::string> result;
      if (action->hasObject("result") ) {
        action->getStringArray("result")
        for (std::string element : downstream_remove_headers) {
        }*/

      action_matcher->add(
        Http::InjectAction(action->getStringArray("result",true), action->getString("action","passthrough"),
                           upstream_inject_headers_lc, upstream_inject_any,
                           upstream_remove_headers_lc, upstream_remove_cookie_names,
                           downstream_inject_headers_lc, downstream_inject_any,
                           downstream_remove_headers_lc, action->getBoolean("use_rpc_response",false),
                           action->getInteger("response_code",500), response_headers, action->getString("response_body","")));

    }
  }

  const std::string& cluster_name = json_config.getString("cluster_name");
  const int64_t timeout_ms = json_config.getInteger("timeout_ms", 120);
  const bool always_triggered = json_config.getBoolean("always_triggered", false);
  const bool include_all_headers = json_config.getBoolean("include_all_headers", false);

  bool always_triggered_not_specified  = json_config.getBoolean("always_triggered", true) && !json_config.getBoolean("always_triggered", false);
  bool disabled = !always_triggered_not_specified && !always_triggered  && (trigger_headers.size() == 0) && (trigger_cookie_names.size() == 0);

  if ((trigger_headers.size() == 0) && (trigger_cookie_names.size() == 0) && !disabled) {
    throw EnvoyException("Inject filter requires a non-empty trigger_headers list or always_triggered to be explicitly set.");
  }

  // no need to verify that any header injection could happen - inject could just be mirroring requests for review

  // verify that target cluster exists
  if (!fac_ctx.clusterManager().get(cluster_name)) {
    throw EnvoyException("Inject filter requires 'cluster_name' cluster for gRPC inject request to be configured statically in the config file. No such cluster: " + cluster_name);
  }
  // nice to have: ensure no dups in trig vs include hdrs
  Http::InjectFilterConfigSharedPtr config(new Http::InjectFilterConfig(trigger_headers, trigger_cookie_names, antitrigger_headers,
                                                                        always_triggered, inc_hdrs_lc, include_all_headers, params,
                                                                        fac_ctx.clusterManager(), cluster_name, timeout_ms, *action_matcher));
  return config;
}





} // Configuration
} // Server
} // Envoy
