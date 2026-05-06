#pragma once
#include <GfxRenderer.h>

#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Settings → CrossPoint Cloud. Lets the user view pairing status, edit the
// server URL, run the pairing exchange with a 6-char code, and unpair.
class CloudPairActivity final : public Activity {
 public:
  explicit CloudPairActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CloudPair", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  // Transient banner text shown beneath the sub-header — set by pair/forget
  // handlers, cleared on the next interaction.
  std::string statusBanner;

  // Stashed across the WiFi-setup detour: if the user enters a code while
  // disconnected, we send them to WifiSelectionActivity and resume pairing
  // with this code on return so they don't have to re-type it.
  std::string pendingPairCode;

  // Persisted across activity entries (but not across reboots — pairing codes
  // expire in 10 minutes anyway, so RAM-lifetime is enough). Lets a failed
  // pair attempt be retried without re-typing the 6-char code.
  static std::string lastEnteredCode;

  void handleSelection();
  void editServerUrl();
  void enterPairingCode();
  void doForget();
  void executePairing(const std::string& code);
  int menuItemCount() const;  // 2 when un-paired, 3 when paired
};
