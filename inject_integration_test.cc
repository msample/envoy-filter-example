#include "test/integration/integration.h"
#include "test/integration/utility.h"

namespace Envoy {
class InjectIntegrationTest : public BaseIntegrationTest,
                             public testing::TestWithParam<Network::Address::IpVersion> {
public:
  InjectIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual integration test.
   */
  void SetUp() override {
    createTestServer("inject_server.json", {"inject"});
  }

  /**
   * Destructor for an individual integration test.
   */
  void TearDown() override {
    test_server_.reset();
  }
};

INSTANTIATE_TEST_CASE_P(IpVersions, InjectIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(InjectIntegrationTest, Inject) {
}
} // Envoy


