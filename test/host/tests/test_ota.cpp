#include <config.h>

#include <string>

#include "../../../src/network.h"
#include "../../../src/ota.h"
#include "api_helpers.h"

const char* g_test_pubkey = "";
static std::string pubKey;
static std::string wrongKey;
static std::string manifest, sig, firmware;

static void prepare(bool connect = true) {
  pubKey = slurp(g_fixtureDir + "/pub.pem");
  wrongKey = slurp(g_fixtureDir + "/pub_other.pem");
  manifest = slurp(g_fixtureDir + "/manifest.txt");
  sig = slurp(g_fixtureDir + "/manifest.sig");
  firmware = slurp(g_fixtureDir + "/firmware.bin");
  g_test_pubkey = pubKey.c_str();
  g_files["manifest.txt"] = manifest;
  g_files["manifest.sig"] = sig;
  g_files["firmware.bin"] = firmware;
  if (connect) {
    g_nvs["wifi_ssid"] = "home";
    g_nvs["wifi_pass"] = "secret";
    network::begin();
  }
  ota::begin();
}

static void check() {
  CHECK(ota::requestCheck());
  runTasks();
}

static bool contains(const String& text, const char* part) { return text.s.find(part) != std::string::npos; }

TEST(ota_check_offers_newer_signed_release) {
  prepare();
  check();
  ota::Status s = ota::status();
  CHECK(s.state == ota::State::Available);
  CHECK(s.latest == "1.0.1");
  CHECK(s.configured);
  CHECK(s.lastCheckSecondsAgo >= 0);
}

TEST(ota_check_rejects_tampered_manifest) {
  prepare();
  g_files["manifest.txt"][0] ^= 1;
  check();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "INVALID"));
}

TEST(ota_check_rejects_tampered_signature) {
  prepare();
  g_files["manifest.sig"][10] ^= 1;
  check();
  CHECK(ota::status().state == ota::State::Error);
}

TEST(ota_check_rejects_signature_from_another_key) {
  prepare();
  g_test_pubkey = wrongKey.c_str();
  check();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "INVALID"));
}

TEST(ota_check_same_version_is_up_to_date) {
  prepare();
  g_files["manifest.txt"] = slurp(g_fixtureDir + "/manifest_same.txt");
  g_files["manifest.sig"] = slurp(g_fixtureDir + "/manifest_same.sig");
  check();
  CHECK(ota::status().state == ota::State::UpToDate);
}

TEST(ota_check_ignores_an_older_signed_manifest) {
  prepare();
  g_files["manifest.txt"] = slurp(g_fixtureDir + "/manifest_old.txt");
  g_files["manifest.sig"] = slurp(g_fixtureDir + "/manifest_old.sig");
  check();
  CHECK(ota::status().state == ota::State::UpToDate);
}

TEST(ota_check_missing_release_reports_error) {
  prepare();
  g_files.erase("manifest.txt");
  check();
  CHECK(ota::status().state == ota::State::Error);
}

TEST(ota_check_requires_wifi) {
  prepare(false);
  check();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "WiFi"));
}

TEST(ota_unconfigured_firmware_refuses_to_check) {
  prepare();
  g_test_pubkey = "-----BEGIN PUBLIC KEY-----\nchange-me\n-----END PUBLIC KEY-----\n";
  CHECK(!ota::status().configured);
  check();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "not configured"));
}

TEST(ota_busy_state_rejects_a_second_check) {
  prepare();
  CHECK(ota::requestCheck());
  CHECK(!ota::requestCheck());
}

TEST(ota_background_check_waits_then_runs) {
  prepare();
  ota::loop();
  CHECK(ota::status().state == ota::State::Idle);
  delay(cfg::kOtaFirstCheckDelayMs + 1);
  ota::loop();
  runTasks();
  CHECK(ota::status().state == ota::State::Available);
}

TEST(ota_background_check_failure_is_silent) {
  prepare();
  g_files.erase("manifest.txt");
  delay(cfg::kOtaFirstCheckDelayMs + 1);
  ota::loop();
  runTasks();
  CHECK(ota::status().state == ota::State::Idle);
  CHECK(ota::status().message.length() == 0);
}

TEST(ota_apply_needs_an_available_update) {
  prepare();
  CHECK(!ota::requestApply());
  check();
  CHECK(ota::requestApply());
}

TEST(ota_install_flashes_verified_image_and_arms_trial) {
  prepare();
  check();
  CHECK(ota::requestApply());
  runTasks();
  CHECK(ota::status().state == ota::State::Rebooting);
  CHECK(g_updateEnded && !g_updateAborted);
  CHECK(g_updateWritten == firmware);
  CHECK(g_nvs["ota_pend"] == "1" && g_nvs["ota_boots"] == "0");
  CHECK(g_restarts == 1);
}

TEST(ota_install_discards_a_corrupted_image) {
  prepare();
  check();
  g_files["firmware.bin"][g_files["firmware.bin"].size() / 2] ^= 0x55;
  CHECK(ota::requestApply());
  runTasks();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "hash"));
  CHECK(g_updateAborted && !g_updateEnded);
  CHECK(g_nvs["ota_pend"] != "1");
  CHECK(g_restarts == 0);
}

TEST(ota_install_refuses_a_size_that_differs_from_the_manifest) {
  prepare();
  check();
  g_files["firmware.bin"] += "extra";
  CHECK(ota::requestApply());
  runTasks();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(!g_updateEnded);
}

TEST(ota_install_waits_for_previous_update_to_confirm) {
  prepare();
  g_nvs["ota_pend"] = "1";
  ota::begin();
  check();
  CHECK(ota::requestApply());
  runTasks();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "previous update"));
}

TEST(ota_does_not_reoffer_a_version_that_was_rolled_back) {
  prepare();
  g_nvs["ota_bad"] = "1.0.1";
  check();
  CHECK(ota::status().state == ota::State::UpToDate);
  CHECK(contains(ota::status().message, "rolled back"));
}

TEST(ota_trial_boots_are_counted_then_rolled_back) {
  g_nvs["ota_pend"] = "1";
  g_nvs["ota_boots"] = "2";
  ota::begin();
  CHECK(g_nvs["ota_boots"] == "3");
  CHECK(!g_bootPartitionSet);
  CHECK(ota::status().pendingConfirm);
  ota::begin();
  CHECK(g_bootPartitionSet);
  CHECK(g_nvs["ota_pend"] == "0");
  CHECK(g_nvs["ota_bad"] == "1.0.0");
  CHECK(g_restarts == 1);
}

TEST(ota_new_image_confirms_itself_after_staying_up) {
  g_nvs["ota_pend"] = "1";
  ota::begin();
  ota::loop();
  CHECK(g_nvs["ota_pend"] == "1");
  delay(cfg::kOtaConfirmAfterMs + 1);
  ota::loop();
  CHECK(g_nvs["ota_pend"] == "0" && g_nvs["ota_boots"] == "0");
  CHECK(!ota::status().pendingConfirm);
}

TEST(ota_task_start_failure_is_reported_and_recoverable) {
  prepare();
  g_taskStartFails = true;
  CHECK(ota::requestCheck());
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "update task"));
  g_taskStartFails = false;
  check();
  CHECK(ota::status().state == ota::State::Available);
}

TEST(ota_check_failure_names_the_http_status_and_free_memory) {
  prepare();
  g_files.erase("manifest.txt");
  check();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "HTTP 404"));
  CHECK(contains(ota::status().message, "largest free block 60000 bytes"));
}

TEST(ota_check_failure_names_connection_errors) {
  prepare();
  g_httpForceError = -1;
  check();
  CHECK(contains(ota::status().message, "connection refused"));
  g_httpForceError = -11;
  check();
  CHECK(contains(ota::status().message, "read Timeout"));
}

TEST(ota_check_accepts_responses_without_a_content_length) {
  prepare();
  g_chunked = true;
  check();
  CHECK(ota::status().state == ota::State::Available);
  CHECK(ota::status().latest == "1.0.1");
}

TEST(ota_apply_is_refused_while_a_check_is_running) {
  prepare();
  check();
  CHECK(ota::status().state == ota::State::Available);
  CHECK(ota::requestCheck());
  CHECK(!ota::requestApply());
  runTasks();
  CHECK(ota::requestApply());
}

TEST(ota_install_failure_names_the_http_status) {
  prepare();
  check();
  g_files.erase("firmware.bin");
  CHECK(ota::requestApply());
  runTasks();
  CHECK(ota::status().state == ota::State::Error);
  CHECK(contains(ota::status().message, "HTTP 404"));
  CHECK(!g_updateEnded);
}

TEST(ota_install_progress_reaches_one_hundred_before_reboot) {
  prepare();
  check();
  CHECK(ota::requestApply());
  CHECK(ota::status().progress == 0);
  runTasks();
  CHECK(ota::status().progress == 100);
}

TEST(ota_a_wifi_change_is_refused_during_a_download) {
  prepare();
  g_nvs["api_token"] = kToken;
  relays::begin();
  scheduler::begin();
  api::begin();
  check();
  CHECK(ota::requestApply());
  CHECK(call(HTTP_POST, "/api/wifi/connect", args("ssid", "Other")).code == 409);
  runTasks();
}
