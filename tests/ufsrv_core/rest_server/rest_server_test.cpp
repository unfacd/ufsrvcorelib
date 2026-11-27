#include "gtest/gtest.h"

extern "C" {
#include <ufsrvcorelib/ufsrv_core/rest_server/rest_server.h>
#include <ufsrvcorelib/ufsrv_core/rest_server/rest_session_type.h>
}

namespace {

UfsrvRestServerSpecs
UsableSpecs(Protocol *protocol, RestSession *sessions)
{
  UfsrvRestServerSpecs specs = {};
  specs.worker_count = 2;
  specs.bind_host = "127.0.0.1";
  specs.port = 0;
  specs.protocol = protocol;
  specs.sessions = sessions;
  return specs;
}

}  // namespace

TEST(RestServer, RejectsAnUnusableSpec)
{
  EXPECT_EQ(UfsrvRestServerOnboard(nullptr), nullptr);

  Protocol protocol = {};
  RestSession sessions[2] = {};
  UfsrvRestServerSpecs specs = UsableSpecs(&protocol, sessions);

  specs.worker_count = 0;
  EXPECT_EQ(UfsrvRestServerOnboard(&specs), nullptr);

  specs = UsableSpecs(&protocol, sessions);
  specs.protocol = nullptr;
  EXPECT_EQ(UfsrvRestServerOnboard(&specs), nullptr);

  specs = UsableSpecs(&protocol, sessions);
  specs.sessions = nullptr;
  EXPECT_EQ(UfsrvRestServerOnboard(&specs), nullptr);
}

TEST(RestServer, TwoWorkersShareOnePortAndStop)
{
  Protocol protocol = {};
  RestSession sessions[2] = {};
  UfsrvRestServerSpecs specs = UsableSpecs(&protocol, &sessions[0]);

  UfsrvRestServer *server = UfsrvRestServerOnboard(&specs);
  ASSERT_NE(server, nullptr);
  EXPECT_NE(UfsrvRestServerPort(server), 0u);
  EXPECT_TRUE(UfsrvRestServerStart(server));
  UfsrvRestServerStop(server);
  EXPECT_EQ(UfsrvRestServerPort(nullptr), 0u);
}
