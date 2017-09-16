#include <string>

#include "envoy/server/filter_config.h"
#include "common/json/json_loader.h"
#include "inject.h"

namespace Envoy {
namespace Server {
namespace Configuration {
/**
 * Config registration for the header injection filter
 */
class InjectFilterConfig : public NamedHttpFilterConfigFactory {
public:
  std::string name() override { return "inject"; }

  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;

  static Http::InjectFilterConfigSharedPtr createConfig(const Json::Object& json_config,
                                                        const std::string& stat_prefix,
                                                        FactoryContext& context);
};


} // Configuration
} // Server
} // Envoy
