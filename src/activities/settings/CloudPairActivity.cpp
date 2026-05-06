#include "CloudPairActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CloudClient.h"

std::string CloudPairActivity::lastEnteredCode;

namespace {
// Display labels for paired/connected status badges.
std::string statusValueText() {
  auto& cc = CloudClient::getInstance();
  if (!cc.isPaired()) return std::string(tr(STR_CLOUD_NOT_PAIRED));
  return cc.isConnected() ? std::string(tr(STR_CLOUD_CONNECTED)) : std::string(tr(STR_CLOUD_DISCONNECTED));
}

std::string truncatedMid(const std::string& s, size_t maxLen) {
  if (s.size() <= maxLen) return s;
  if (maxLen <= 3) return std::string(maxLen, '.');
  size_t keep = (maxLen - 1) / 2;
  return s.substr(0, keep) + "…" + s.substr(s.size() - (maxLen - 1 - keep));
}
}  // namespace

void CloudPairActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  statusBanner.clear();
  requestUpdate();
}

void CloudPairActivity::onExit() { Activity::onExit(); }

int CloudPairActivity::menuItemCount() const { return CloudClient::getInstance().isPaired() ? 3 : 2; }

void CloudPairActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, menuItemCount());
    statusBanner.clear();
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, menuItemCount());
    statusBanner.clear();
    requestUpdate();
  });
}

void CloudPairActivity::handleSelection() {
  const bool paired = CloudClient::getInstance().isPaired();
  switch (selectedIndex) {
    case 0:
      editServerUrl();
      break;
    case 1:
      // When paired, this row is a status indicator only — pressing it must
      // not re-launch the pairing-code keyboard. Re-pairing requires Forget
      // first, which avoids accidentally invalidating a working token.
      if (!paired) enterPairingCode();
      break;
    case 2:
      doForget();
      break;
    default:
      break;
  }
}

void CloudPairActivity::editServerUrl() {
  const auto& current = CloudClient::getInstance().getServerUrl();
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_ENTER_URL), current, 127, false),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto& kb = std::get<KeyboardResult>(result.data);
        if (kb.text.empty()) return;
        if (CloudClient::getInstance().setServerUrl(kb.text)) {
          statusBanner = std::string(tr(STR_CLOUD_SERVER_URL)) + ": " + kb.text;
        } else {
          statusBanner = tr(STR_CLOUD_PAIRING_FAILED);
        }
        requestUpdate();
      });
}

void CloudPairActivity::enterPairingCode() {
  if (CloudClient::getInstance().getServerUrl().empty()) {
    statusBanner = tr(STR_CLOUD_URL_NEEDED);
    requestUpdate();
    return;
  }
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CLOUD_ENTER_CODE),
                                                                 lastEnteredCode, 6, false),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto& kb = std::get<KeyboardResult>(result.data);
                           if (kb.text.empty()) return;
                           // Server expects the code uppercase; KeyboardEntry returns whatever
                           // the user typed.
                           std::string code = kb.text;
                           for (auto& c : code) {
                             if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
                           }
                           // Remember it so a failed retry can pre-fill the keyboard.
                           lastEnteredCode = code;

                           // If WiFi is down, route to WifiSelectionActivity then resume pair.
                           // Stash the code so the user doesn't have to retype it after the
                           // detour through WiFi setup.
                           if (WiFi.status() != WL_CONNECTED) {
                             pendingPairCode = code;
                             statusBanner = tr(STR_CLOUD_WIFI_NEEDED);
                             requestUpdate();
                             startActivityForResult(
                                 std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true),
                                 [this](const ActivityResult&) {
                                   // Whether or not WifiSelectionActivity reports a result, the
                                   // device's WiFi state is what matters. Try the saved code if
                                   // we successfully landed online.
                                   std::string saved = pendingPairCode;
                                   pendingPairCode.clear();
                                   if (saved.empty()) return;
                                   if (WiFi.status() != WL_CONNECTED) {
                                     statusBanner = tr(STR_CLOUD_WIFI_NEEDED);
                                     requestUpdate();
                                     return;
                                   }
                                   executePairing(saved);
                                 });
                             return;
                           }

                           executePairing(code);
                         });
}

void CloudPairActivity::executePairing(const std::string& code) {
  // Show "pairing..." immediately so user knows we're working. Render happens
  // before the blocking HTTP call lands.
  statusBanner = tr(STR_CLOUD_PAIRING_IN_PROGRESS);
  requestUpdate();

  // Device name: short identifier the cloud dashboard can display. Use the
  // last 4 hex chars of the MAC for uniqueness across owners who might pair
  // multiple readers.
  const auto& hwId = CloudClient::getInstance().getHardwareId();
  std::string suffix = hwId.size() >= 5 ? hwId.substr(hwId.size() - 5) : hwId;
  std::string clean;
  for (char c : suffix) {
    if (c != ':') clean.push_back(c);
  }
  std::string deviceName = std::string("CrossPoint-") + clean;

  bool ok = CloudClient::getInstance().pairWithCode(code, deviceName);
  statusBanner = ok ? tr(STR_CLOUD_PAIRING_SUCCESS) : tr(STR_CLOUD_PAIRING_FAILED);
  if (ok) selectedIndex = 0;  // re-anchor since menu length changed
  requestUpdate();
}

void CloudPairActivity::doForget() {
  if (CloudClient::getInstance().forgetDevice()) {
    statusBanner = tr(STR_CLOUD_NOT_PAIRED);
    selectedIndex = 0;
    requestUpdate();
  }
}

void CloudPairActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLOUD_PAIRING));

  // Sub-header doubles as a status line. Show the transient banner if set,
  // otherwise the static hint.
  const std::string subHeader = statusBanner.empty() ? std::string(tr(STR_CLOUD_HINT)) : statusBanner;
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    subHeader.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  const int items = menuItemCount();
  const bool paired = CloudClient::getInstance().isPaired();
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, items, selectedIndex,
      [paired](int index) -> std::string {
        switch (index) {
          case 0:
            return std::string(I18N.get(StrId::STR_CLOUD_SERVER_URL));
          case 1:
            // Once paired, this row is purely a connection-status indicator;
            // the actionable "Pair this device" label only makes sense before
            // a token exists.
            return std::string(I18N.get(paired ? StrId::STR_CLOUD_STATUS : StrId::STR_CLOUD_PAIR_DEVICE));
          case 2:
            return std::string(I18N.get(StrId::STR_CLOUD_FORGET_DEVICE));
          default:
            return std::string{};
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        auto& cc = CloudClient::getInstance();
        switch (index) {
          case 0: {
            const auto& url = cc.getServerUrl();
            return url.empty() ? std::string(I18N.get(StrId::STR_NOT_SET)) : truncatedMid(url, 32);
          }
          case 1:
            return statusValueText();
          case 2:
            // Show device id when paired so the user knows what they're forgetting.
            return cc.getDeviceId().empty() ? std::string{} : truncatedMid(cc.getDeviceId(), 24);
          default:
            return std::string{};
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
