#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/eunchurn/crosspoint-reader-ko/releases/latest";
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  const std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;

  Serial.printf("[%lu] [OTA] Fetching: %s\n", millis(), latestReleaseUrl);

  http.begin(*client, latestReleaseUrl);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [OTA] HTTP error: %d\n", millis(), httpCode);
    http.end();
    return HTTP_ERROR;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  filter["assets"][0]["size"] = true;
  const DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("[%lu] [OTA] JSON parse failed: %s\n", millis(), error.c_str());
    return JSON_PARSE_ERROR;
  }

  if (!doc["tag_name"].is<std::string>()) {
    Serial.printf("[%lu] [OTA] No tag_name found\n", millis());
    return JSON_PARSE_ERROR;
  }
  if (!doc["assets"].is<JsonArray>()) {
    Serial.printf("[%lu] [OTA] No assets found\n", millis());
    return JSON_PARSE_ERROR;
  }

  latestVersion = doc["tag_name"].as<std::string>();

  for (int i = 0; i < doc["assets"].size(); i++) {
    if (doc["assets"][i]["name"] == "firmware.bin") {
      otaUrl = doc["assets"][i]["browser_download_url"].as<std::string>();
      otaSize = doc["assets"][i]["size"].as<size_t>();
      totalSize = otaSize;
      updateAvailable = true;
      break;
    }
  }

  if (!updateAvailable) {
    Serial.printf("[%lu] [OTA] No firmware.bin asset found\n", millis());
    return NO_UPDATE;
  }

  Serial.printf("[%lu] [OTA] Found update: %s\n", millis(), latestVersion.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  // semantic version check (only match on 3 segments)
  const auto updateMajor = stoi(latestVersion.substr(0, latestVersion.find('.')));
  const auto updateMinor = stoi(
      latestVersion.substr(latestVersion.find('.') + 1, latestVersion.find_last_of('.') - latestVersion.find('.') - 1));
  const auto updatePatch = stoi(latestVersion.substr(latestVersion.find_last_of('.') + 1));

  std::string currentVersion = CROSSPOINT_VERSION;
  const auto currentMajor = stoi(currentVersion.substr(0, currentVersion.find('.')));
  const auto currentMinor = stoi(currentVersion.substr(
      currentVersion.find('.') + 1, currentVersion.find_last_of('.') - currentVersion.find('.') - 1));
  const auto currentPatch = stoi(currentVersion.substr(currentVersion.find_last_of('.') + 1));

  if (updateMajor > currentMajor) {
    return true;
  }
  if (updateMajor < currentMajor) {
    return false;
  }

  if (updateMinor > currentMinor) {
    return true;
  }
  if (updateMinor < currentMinor) {
    return false;
  }

  if (updatePatch > currentPatch) {
    return true;
  }
  return false;
}

const std::string& OtaUpdater::getLatestVersion() { return latestVersion; }

bool OtaUpdater::isUpdateNewerKO() {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  // Parse version: major.minor.patch-ko.koVersion
  auto parseVersion = [](const std::string& version, int& major, int& minor, int& patch, int& ko) {
    major = minor = patch = ko = 0;

    // Find -ko. suffix
    size_t koPos = version.find("-ko.");
    std::string baseVersion = (koPos != std::string::npos) ? version.substr(0, koPos) : version;

    // Parse ko version if present
    if (koPos != std::string::npos) {
      ko = stoi(version.substr(koPos + 4));
    }

    // Parse major.minor.patch
    size_t firstDot = baseVersion.find('.');
    size_t lastDot = baseVersion.find_last_of('.');

    if (firstDot != std::string::npos) {
      major = stoi(baseVersion.substr(0, firstDot));
      if (lastDot != firstDot) {
        minor = stoi(baseVersion.substr(firstDot + 1, lastDot - firstDot - 1));
        patch = stoi(baseVersion.substr(lastDot + 1));
      } else {
        minor = stoi(baseVersion.substr(firstDot + 1));
      }
    }
  };

  int updateMajor, updateMinor, updatePatch, updateKo;
  int currentMajor, currentMinor, currentPatch, currentKo;

  parseVersion(latestVersion, updateMajor, updateMinor, updatePatch, updateKo);
  parseVersion(CROSSPOINT_VERSION, currentMajor, currentMinor, currentPatch, currentKo);

  if (updateMajor != currentMajor) return updateMajor > currentMajor;
  if (updateMinor != currentMinor) return updateMinor > currentMinor;
  if (updatePatch != currentPatch) return updatePatch > currentPatch;
  return updateKo > currentKo;
}

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(const std::function<void(size_t, size_t)>& onProgress) {
  if (!isUpdateNewerKO()) {
    return UPDATE_OLDER_ERROR;
  }

  const std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);
  client->setInsecure();
  HTTPClient http;

  Serial.printf("[%lu] [OTA] Fetching: %s\n", millis(), otaUrl.c_str());

  http.begin(*client, otaUrl.c_str());
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  const int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[%lu] [OTA] Download failed: %d\n", millis(), httpCode);
    http.end();
    return HTTP_ERROR;
  }

  // 2. Get length and stream
  const size_t contentLength = http.getSize();

  if (contentLength != otaSize) {
    Serial.printf("[%lu] [OTA] Invalid content length\n", millis());
    http.end();
    return HTTP_ERROR;
  }

  // 3. Begin the ESP-IDF Update process
  if (!Update.begin(otaSize)) {
    Serial.printf("[%lu] [OTA] Not enough space. Error: %s\n", millis(), Update.errorString());
    http.end();
    return INTERNAL_UPDATE_ERROR;
  }

  this->totalSize = otaSize;
  Serial.printf("[%lu] [OTA] Update started\n", millis());
  Update.onProgress([this, onProgress](const size_t progress, const size_t total) {
    this->processedSize = progress;
    this->totalSize = total;
    onProgress(progress, total);
  });
  const size_t written = Update.writeStream(*client);
  http.end();

  if (written == otaSize) {
    Serial.printf("[%lu] [OTA] Successfully written %u bytes\n", millis(), written);
  } else {
    Serial.printf("[%lu] [OTA] Written only %u/%u bytes. Error: %s\n", millis(), written, otaSize,
                  Update.errorString());
    return INTERNAL_UPDATE_ERROR;
  }

  if (Update.end() && Update.isFinished()) {
    Serial.printf("[%lu] [OTA] Update complete\n", millis());
    return OK;
  } else {
    Serial.printf("[%lu] [OTA] Error Occurred: %s\n", millis(), Update.errorString());
    return INTERNAL_UPDATE_ERROR;
  }
}
