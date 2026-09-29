#include <gtest/gtest.h>

#include <fstream>

#include "common/paths.h"
#include "framework/property_manager.h"
#include "framework/system_properties.h"
#include "init/config.h"
#include "init/property_service.h"
#include "test_util.h"

using namespace mini;

// ---- PropertyStore ----------------------------------------------------------

TEST(PropertyStoreTest, SetGetAndList) {
  PropertyStore store;
  ASSERT_EQ(store.set("sys.b", "2"), Status::Ok);
  ASSERT_EQ(store.set("sys.a", "1"), Status::Ok);
  ASSERT_EQ(store.set("sys.a", "3"), Status::Ok);  // normal keys can change
  EXPECT_EQ(store.get("sys.a").value(), "3");
  EXPECT_EQ(store.get("nope").status(), Status::NotFound);
  auto all = store.list();
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].first, "sys.a");  // sorted by key
}

TEST(PropertyStoreTest, RoPropertiesAreWriteOnce) {
  PropertyStore store;
  ASSERT_EQ(store.set("ro.build.type", "user"), Status::Ok);
  EXPECT_EQ(store.set("ro.build.type", "eng"), Status::ReadOnly);
  EXPECT_EQ(store.get("ro.build.type").value(), "user");
}

TEST(PropertyStoreTest, RejectsBadKeysAndLongValues) {
  PropertyStore store;
  EXPECT_EQ(store.set("", "x"), Status::InvalidArgument);
  EXPECT_EQ(store.set("has space", "x"), Status::InvalidArgument);
  EXPECT_EQ(store.set(".leading", "x"), Status::InvalidArgument);
  EXPECT_EQ(store.set("k", std::string(PropertyStore::kMaxValueLength + 1, 'v')),
            Status::InvalidArgument);
  EXPECT_EQ(store.set("k", std::string(PropertyStore::kMaxValueLength, 'v')), Status::Ok);
}

TEST(PropertyServiceTest, ProxyAndStubRoundTrip) {
  PropertyStore store;
  PropertyServiceStub stub(store);
  PropertyServiceProxy proxy(std::make_unique<test::LoopbackRemote>(stub));
  ASSERT_EQ(proxy.set("sys.x", "hello"), Status::Ok);
  EXPECT_EQ(proxy.get("sys.x").value(), "hello");
  EXPECT_EQ(proxy.get("sys.missing").status(), Status::NotFound);
  EXPECT_EQ(proxy.set("bad key", "v"), Status::InvalidArgument);
  auto all = proxy.list();
  ASSERT_TRUE(all.ok());
  EXPECT_EQ(all.value().size(), 1u);
}

// ---- config parser ----------------------------------------------------------

TEST(ConfigTest, ParsesPropertiesServicesAndOptions) {
  std::string error;
  auto cfg = parseInitConfig(
      "# comment\n"
      "setprop ro.a hello world   # trailing comment\n"
      "service one /bin/sleep 30\n"
      "    oneshot\n"
      "    restart_delay 50\n"
      "service two two\n",
      error);
  ASSERT_TRUE(cfg.ok()) << error;
  ASSERT_EQ(cfg.value().properties.size(), 1u);
  EXPECT_EQ(cfg.value().properties[0].second, "hello world");
  ASSERT_EQ(cfg.value().services.size(), 2u);
  const ServiceDef& one = cfg.value().services[0];
  EXPECT_EQ(one.name, "one");
  EXPECT_EQ(one.args, std::vector<std::string>{"30"});
  EXPECT_TRUE(one.oneshot);
  EXPECT_EQ(one.restartDelay.count(), 50);
  EXPECT_FALSE(cfg.value().services[1].oneshot);
}

TEST(ConfigTest, ReportsErrorsWithLineNumbers) {
  std::string error;
  EXPECT_EQ(parseInitConfig("setprop only_key\n", error).status(), Status::InvalidArgument);
  EXPECT_EQ(error, "line 1: setprop needs a key and a value");
  EXPECT_FALSE(parseInitConfig("\n\nfrobnicate now\n", error).ok());
  EXPECT_EQ(error, "line 3: unknown directive 'frobnicate'");
  EXPECT_FALSE(parseInitConfig("oneshot\n", error).ok());  // option before any service
  EXPECT_FALSE(parseInitConfig("service a /bin/true\nservice a /bin/true\n", error).ok());
  EXPECT_FALSE(parseInitConfig("service a /bin/true\nrestart_delay soon\n", error).ok());
}

// ---- init, for real ---------------------------------------------------------

namespace {
std::string writeConfig(const test::TempRunDir& run, const std::string& text) {
  std::string path = run.path() + "/test-init.conf";
  std::ofstream(path) << text;
  return path;
}

std::string prop(const std::string& key) { return SystemProperties::get(key, ""); }

bool processAlive(pid_t pid) { return ::kill(pid, 0) == 0; }
}  // namespace

TEST(InitTest, StartsServicesAndRestartsACrashedOne) {
  test::TempRunDir run;
  std::string conf = writeConfig(run,
                                 "setprop ro.test.flag yes\n"
                                 "service sleeper /bin/sleep 300\n"
                                 "    restart_delay 50\n");
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {conf});
  ASSERT_TRUE(test::waitFor([&] { return prop("init.svc.sleeper") == "running"; }));
  EXPECT_EQ(prop("ro.test.flag"), "yes");
  EXPECT_EQ(prop("sys.boot_completed"), "1");

  pid_t first = std::stoi(prop("init.svc_debug_pid.sleeper"));
  ASSERT_TRUE(processAlive(first));
  ::kill(first, SIGKILL);  // crash it

  ASSERT_TRUE(test::waitFor([&] {
    std::string pid = prop("init.svc_debug_pid.sleeper");
    return prop("init.svc.sleeper") == "running" && !pid.empty() && std::stoi(pid) != first;
  }));
  pid_t second = std::stoi(prop("init.svc_debug_pid.sleeper"));
  EXPECT_TRUE(processAlive(second));

  // Orderly shutdown: SIGTERM to init stops the child too.
  init.stop(SIGTERM);
  EXPECT_TRUE(test::waitFor([&] { return !processAlive(second); }));
}

TEST(InitTest, GivesUpOnAServiceThatKeepsCrashing) {
  test::TempRunDir run;
  std::string conf = writeConfig(run,
                                 "service crasher /bin/false\n"
                                 "    restart_delay 10\n"
                                 "service oneoff /bin/true\n"
                                 "    oneshot\n");
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {conf});
  ASSERT_TRUE(test::waitFor([&] { return prop("init.svc.crasher") == "failed"; }));
  EXPECT_EQ(prop("init.svc.oneoff"), "stopped");  // oneshot: ran once, not restarted
}

TEST(InitTest, MissingBinaryEndsUpFailed) {
  test::TempRunDir run;
  std::string conf = writeConfig(run,
                                 "service ghost /no/such/binary\n"
                                 "    restart_delay 10\n");
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {conf});
  ASSERT_TRUE(test::waitFor([&] { return prop("init.svc.ghost") == "failed"; }));
}

TEST(InitTest, SetpropThroughTheServiceAndReadOnlyEnforced) {
  test::TempRunDir run;
  std::string conf = writeConfig(run, "setprop ro.fixed 1\n");
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {conf});
  ASSERT_TRUE(test::waitFor([&] { return prop("sys.boot_completed") == "1"; }));
  EXPECT_EQ(SystemProperties::set("sys.custom", "abc"), Status::Ok);
  EXPECT_EQ(prop("sys.custom"), "abc");
  EXPECT_EQ(SystemProperties::set("ro.fixed", "2"), Status::ReadOnly);
}

TEST(InitTest, BadConfigMakesInitExitWithError) {
  test::TempRunDir run;
  std::string conf = writeConfig(run, "bogus directive\n");
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {conf});
  int status = 0;
  ASSERT_TRUE(test::waitFor([&] {
    pid_t r = ::waitpid(init.pid(), &status, WNOHANG);
    return r == init.pid();
  }));
  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 1);
  init.release();  // already reaped
}

// The shipped init.conf boots the whole system, and init revives a crashed service.
TEST(InitTest, BootsTheWholeSystemAndRecoversFromAServiceCrash) {
  test::TempRunDir run;
  test::ChildProcess init(std::string(BIN_DIR) + "/init", {std::string(ETC_DIR) + "/init.conf"});

  std::unique_ptr<PropertyManager> pm;
  ASSERT_TRUE(test::waitFor([&] {  // init starts servicemanager and vehicle_service in any order
    auto r = PropertyManager::connect();
    if (r.ok()) pm = r.take();
    return r.ok();
  }));
  ASSERT_EQ(pm->set(kGear, int32_t{2}), Status::Ok);
  EXPECT_EQ(prop("ro.build.type"), "learning");

  pid_t oldPid = std::stoi(prop("init.svc_debug_pid.vehicle_service"));
  ::kill(oldPid, SIGKILL);

  // A fresh client finds the restarted service (state was lost with the process: gear is back to 0).
  ASSERT_TRUE(test::waitFor([&] {
    auto r = PropertyManager::connect();
    if (!r.ok()) return false;
    auto gear = r.value()->get(kGear);
    return gear.ok() && gear.value() == 0;
  }));
  EXPECT_NE(std::stoi(prop("init.svc_debug_pid.vehicle_service")), oldPid);
}
