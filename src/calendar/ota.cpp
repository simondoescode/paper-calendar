#if defined(EINK_CALENDAR_APP) && !defined(CALENDAR_HOST)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <calendar/ota.h>
#include <config.h>
#include <ctype.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

namespace calendar {
  namespace {

#ifndef CALENDAR_OTA_MANIFEST_URL
#define CALENDAR_OTA_MANIFEST_URL                                                                                      \
  "https://github.com/simondoescode/paper-calendar/releases/latest/download/manifest.json"
#endif

    constexpr uint32_t kOtaCheckIntervalSeconds = 24U * 60U * 60U;
    constexpr uint32_t kOtaTimeoutMs = 30000;
    constexpr size_t kMaximumManifestBytes = 2048;
    constexpr size_t kOtaBufferBytes = 4096;
    constexpr char kOtaPreferencesNamespace[] = "cal-ota";
    constexpr char kLastCheckKey[] = "last_ok";
    constexpr char kFirmwareUrlPrefix[] = "https://github.com/simondoescode/paper-calendar/releases/download/";

    struct Manifest {
      int major = -1;
      int minor = -1;
      int patch = -1;
      char version[24] = {};
      char url[320] = {};
      char sha256[65] = {};
      size_t size = 0;
    };

    bool isHexSha256(const char *value) {
      if (value == nullptr || strlen(value) != 64) return false;
      for (size_t i = 0; i < 64; ++i) {
        if (!isxdigit(static_cast<unsigned char>(value[i]))) return false;
      }
      return true;
    }

    bool parseVersion(const char *value, int &major, int &minor, int &patch) {
      if (value == nullptr) return false;
      if (*value == 'v' || *value == 'V') ++value;
      char tail = '\0';
      return sscanf(value, "%d.%d.%d%c", &major, &minor, &patch, &tail) == 3 && major >= 0 && minor >= 0 && patch >= 0;
    }

    bool isNewer(const Manifest &manifest) {
      if (manifest.major != FW_MAJOR_VERSION) return manifest.major > FW_MAJOR_VERSION;
      if (manifest.minor != FW_MINOR_VERSION) return manifest.minor > FW_MINOR_VERSION;
      return manifest.patch > FW_PATCH_VERSION;
    }

    bool shouldCheck(bool force, uint32_t nowEpoch) {
      if (force) return true;
      if (nowEpoch == 0) return false;

      Preferences prefs;
      if (!prefs.begin(kOtaPreferencesNamespace, true)) return true;
      const uint32_t lastCheck = prefs.getUInt(kLastCheckKey, 0);
      prefs.end();

      return lastCheck == 0 || nowEpoch < lastCheck || (nowEpoch - lastCheck) >= kOtaCheckIntervalSeconds;
    }

    void rememberSuccessfulCheck(uint32_t nowEpoch) {
      if (nowEpoch == 0) return;
      Preferences prefs;
      if (!prefs.begin(kOtaPreferencesNamespace, false)) return;
      prefs.putUInt(kLastCheckKey, nowEpoch);
      prefs.end();
    }

    void configureTls(WiFiClientSecure &tls) {
      tls.setCACertBundle(rootca_crt_bundle_start);
      tls.setHandshakeTimeout(kOtaTimeoutMs / 1000);
    }

    bool fetchManifest(Manifest &manifest) {
      WiFiClientSecure tls;
      configureTls(tls);

      HTTPClient http;
      http.setConnectTimeout(kOtaTimeoutMs);
      http.setTimeout(kOtaTimeoutMs);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      if (!http.begin(tls, CALENDAR_OTA_MANIFEST_URL)) {
        Serial.println("[ota] Manifest HTTPS initialization failed");
        return false;
      }

      const int status = http.GET();
      if (status != HTTP_CODE_OK) {
        Serial.printf("[ota] Manifest HTTP %d\n", status);
        http.end();
        return false;
      }

      const int contentLength = http.getSize();
      if (contentLength <= 0 || contentLength > static_cast<int>(kMaximumManifestBytes)) {
        Serial.printf("[ota] Invalid manifest size: %d\n", contentLength);
        http.end();
        return false;
      }

      const String body = http.getString();
      http.end();
      if (body.length() != static_cast<size_t>(contentLength)) {
        Serial.println("[ota] Incomplete manifest response");
        return false;
      }

      JsonDocument doc;
      const DeserializationError error = deserializeJson(doc, body);
      if (error) {
        Serial.printf("[ota] Invalid manifest JSON: %s\n", error.c_str());
        return false;
      }

      const char *version = doc["version"] | "";
      const char *url = doc["url"] | "";
      const char *sha256 = doc["sha256"] | "";
      const size_t size = doc["size"] | 0U;

      if (!parseVersion(version, manifest.major, manifest.minor, manifest.patch) ||
          strlen(version) >= sizeof(manifest.version) ||
          strncmp(url, kFirmwareUrlPrefix, strlen(kFirmwareUrlPrefix)) != 0 || strlen(url) >= sizeof(manifest.url) ||
          !isHexSha256(sha256) || size == 0) {
        Serial.println("[ota] Manifest fields failed validation");
        return false;
      }

      strlcpy(manifest.version, version, sizeof(manifest.version));
      strlcpy(manifest.url, url, sizeof(manifest.url));
      strlcpy(manifest.sha256, sha256, sizeof(manifest.sha256));
      manifest.size = size;
      return true;
    }

    void hexDigest(const unsigned char digest[32], char output[65]) {
      static const char kHex[] = "0123456789abcdef";
      for (size_t i = 0; i < 32; ++i) {
        output[i * 2] = kHex[digest[i] >> 4];
        output[i * 2 + 1] = kHex[digest[i] & 0x0f];
      }
      output[64] = '\0';
    }

    bool installFirmware(const Manifest &manifest) {
      WiFiClientSecure tls;
      configureTls(tls);

      HTTPClient http;
      http.setConnectTimeout(kOtaTimeoutMs);
      http.setTimeout(kOtaTimeoutMs);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      if (!http.begin(tls, manifest.url)) {
        Serial.println("[ota] Firmware HTTPS initialization failed");
        return false;
      }

      const int status = http.GET();
      if (status != HTTP_CODE_OK) {
        Serial.printf("[ota] Firmware HTTP %d\n", status);
        http.end();
        return false;
      }

      const int contentLength = http.getSize();
      if (contentLength <= 0 || static_cast<size_t>(contentLength) != manifest.size) {
        Serial.printf("[ota] Firmware size mismatch: expected %u, got %d\n", static_cast<unsigned>(manifest.size),
                      contentLength);
        http.end();
        return false;
      }

      if (!Update.begin(manifest.size, U_FLASH)) {
        Serial.printf("[ota] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
      }

      mbedtls_sha256_context sha;
      mbedtls_sha256_init(&sha);
      if (mbedtls_sha256_starts_ret(&sha, 0) != 0) {
        Update.abort();
        http.end();
        mbedtls_sha256_free(&sha);
        Serial.println("[ota] SHA-256 initialization failed");
        return false;
      }

      WiFiClient *stream = http.getStreamPtr();
      uint8_t buffer[kOtaBufferBytes];
      size_t written = 0;
      uint32_t lastDataAt = millis();
      bool failed = false;

      while (written < manifest.size) {
        const int available = stream->available();
        if (available <= 0) {
          if (!http.connected() || millis() - lastDataAt >= kOtaTimeoutMs) {
            failed = true;
            break;
          }
          delay(1);
          continue;
        }

        const size_t remaining = manifest.size - written;
        size_t request = static_cast<size_t>(available);
        if (request > sizeof(buffer)) request = sizeof(buffer);
        if (request > remaining) request = remaining;

        const int received = stream->readBytes(reinterpret_cast<char *>(buffer), request);
        if (received <= 0) {
          failed = true;
          break;
        }
        lastDataAt = millis();

        if (mbedtls_sha256_update_ret(&sha, buffer, static_cast<size_t>(received)) != 0 ||
            Update.write(buffer, static_cast<size_t>(received)) != static_cast<size_t>(received)) {
          failed = true;
          break;
        }
        written += static_cast<size_t>(received);
      }

      unsigned char digest[32] = {};
      const bool hashFinished = mbedtls_sha256_finish_ret(&sha, digest) == 0;
      mbedtls_sha256_free(&sha);
      http.end();

      char actualSha[65] = {};
      if (hashFinished) hexDigest(digest, actualSha);

      if (failed || written != manifest.size || !hashFinished || strcasecmp(actualSha, manifest.sha256) != 0) {
        Serial.printf("[ota] Verification failed after %u/%u bytes\n", static_cast<unsigned>(written),
                      static_cast<unsigned>(manifest.size));
        if (hashFinished) {
          Serial.printf("[ota] SHA-256 expected %s, got %s\n", manifest.sha256, actualSha);
        }
        Update.abort();
        return false;
      }

      if (!Update.end(true)) {
        Serial.printf("[ota] Update.end failed: %s\n", Update.errorString());
        return false;
      }

      Serial.printf("[ota] Firmware %s installed and ready to boot\n", manifest.version);
      return true;
    }

  } // namespace

  OtaCheckResult checkForFirmwareUpdate(bool force, uint32_t nowEpoch) {
    if (!shouldCheck(force, nowEpoch)) {
      Serial.println("[ota] Daily update check not due");
      return OtaCheckResult::Skipped;
    }

    Serial.printf("[ota] Checking GitHub Releases%s\n", force ? " (forced)" : "");
    Manifest manifest;
    if (!fetchManifest(manifest)) return OtaCheckResult::Failed;

    rememberSuccessfulCheck(nowEpoch);
    Serial.printf("[ota] Latest %s; running %s\n", manifest.version, FW_VERSION_STRING);
    if (!isNewer(manifest)) return OtaCheckResult::UpToDate;

    Serial.printf("[ota] Installing %s (%u bytes)\n", manifest.version, static_cast<unsigned>(manifest.size));
    return installFirmware(manifest) ? OtaCheckResult::UpdateInstalled : OtaCheckResult::Failed;
  }

} // namespace calendar

#endif
