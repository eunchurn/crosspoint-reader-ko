#include "SettingsActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OtaUpdateActivity.h"
#include "fontIds.h"

// Define the static settings list
namespace {
constexpr int settingsCount = 12;
const SettingInfo settingsList[settingsCount] = {
    // Should match with SLEEP_SCREEN_MODE
    // {"Sleep Screen", SettingType::ENUM, &CrossPointSettings::sleepScreen, {"Dark", "Light", "Custom", "Cover"}},
    {"절전 화면 이미지", SettingType::ENUM, &CrossPointSettings::sleepScreen, {"다크", "라이트", "사용자 정의", "커버"}},
    // {"Status Bar", SettingType::ENUM, &CrossPointSettings::statusBar, {"None", "No Progress", "Full"}},
    {"상태 표시줄", SettingType::ENUM, &CrossPointSettings::statusBar, {"없음", "진행 없음", "전체"}},
    // {"Extra Paragraph Spacing", SettingType::TOGGLE, &CrossPointSettings::extraParagraphSpacing, {}},
    {"문단 간격 추가", SettingType::TOGGLE, &CrossPointSettings::extraParagraphSpacing, {}},
    // {"Short Power Button Click", SettingType::TOGGLE, &CrossPointSettings::shortPwrBtn, {}},
    {"전원 버튼 짧게 눌러 끄기", SettingType::TOGGLE, &CrossPointSettings::shortPwrBtn, {}},
    // {"Reading Orientation",
    //  SettingType::ENUM,
    //  &CrossPointSettings::orientation,
    //  {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"}},
    {"읽기 방향",
     SettingType::ENUM,
     &CrossPointSettings::orientation,
     {"세로", "가로 시계방향", "반전", "가로 반시계방향"}},
    // {"Front Button Layout",
    //  SettingType::ENUM,
    //  &CrossPointSettings::frontButtonLayout,
    //  {"Bck, Cnfrm, Lft, Rght", "Lft, Rght, Bck, Cnfrm", "Lft, Bck, Cnfrm, Rght"}},
    {"앞면 버튼 레이아웃",
     SettingType::ENUM,
     &CrossPointSettings::frontButtonLayout,
     {"뒤로, 확인, 왼쪽, 오른쪽", "왼쪽, 오른쪽, 뒤로, 확인", "왼쪽, 뒤로, 확인, 오른쪽"}},
      // {"Side Button Layout (reader)",
      //  SettingType::ENUM,
      //  &CrossPointSettings::sideButtonLayout,
      //  {"Prev, Next", "Next, Prev"}},
    {"측면 버튼 레이아웃 (리더기)",
     SettingType::ENUM,
     &CrossPointSettings::sideButtonLayout,
     {"이전, 다음", "다음, 이전"}},
    {"줄 간격", SettingType::ENUM, &CrossPointSettings::lineSpacing, {"좁게", "보통", "넓게"}},
    {"문단 정렬",
     SettingType::ENUM,
     &CrossPointSettings::paragraphAlignment,
     {"양쪽 정렬", "왼쪽", "가운데", "오른쪽"}},
    {"절전 시간",
     SettingType::ENUM,
     &CrossPointSettings::sleepTimeout,
     {"1분", "5분", "10분", "15분", "30분"}},
    {"새로고침 주기",
     SettingType::ENUM,
     &CrossPointSettings::refreshFrequency,
     {"1 페이지", "5 페이지", "10 페이지", "15 페이지", "30 페이지"}},
    {"업데이트 확인", SettingType::ACTION, nullptr, {}},
};
}  // namespace

void SettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<SettingsActivity*>(param);
  self->displayTaskLoop();
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Reset selection to first item
  selectedSettingIndex = 0;

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&SettingsActivity::taskTrampoline, "SettingsActivityTask",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void SettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void SettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    updateRequired = true;
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  // Handle navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Move selection up (with wrap-around)
    selectedSettingIndex = (selectedSettingIndex > 0) ? (selectedSettingIndex - 1) : (settingsCount - 1);
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Move selection down
    if (selectedSettingIndex < settingsCount - 1) {
      selectedSettingIndex++;
      updateRequired = true;
    }
  }
}

void SettingsActivity::toggleCurrentSetting() {
  // Validate index
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = settingsList[selectedSettingIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ACTION) {
    if (std::string(setting.name) == "업데이트 확인") {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exitActivity();
        updateRequired = true;
      }));
      xSemaphoreGive(renderingMutex);
    }
  } else {
    // Only toggle if it's a toggle type and has a value pointer
    return;
  }

  // Save settings when they change
  SETTINGS.saveToFile();
}

void SettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void SettingsActivity::render() const {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Draw header
  // renderer.drawCenteredText(UI_12_FONT_ID, 15, "Settings", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "설정", true, EpdFontFamily::BOLD);

  // Draw selection
  renderer.fillRect(0, 60 + selectedSettingIndex * 30 - 2, pageWidth - 1, 30);

  // Draw all settings
  for (int i = 0; i < settingsCount; i++) {
    const int settingY = 60 + i * 30;  // 30 pixels between settings

    // Draw setting name
    renderer.drawText(UI_10_FONT_ID, 20, settingY, settingsList[i].name, i != selectedSettingIndex);

    // Draw value based on setting type
    std::string valueText = "";
    if (settingsList[i].type == SettingType::TOGGLE && settingsList[i].valuePtr != nullptr) {
      const bool value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = value ? "ON" : "OFF";
    } else if (settingsList[i].type == SettingType::ENUM && settingsList[i].valuePtr != nullptr) {
      const uint8_t value = SETTINGS.*(settingsList[i].valuePtr);
      valueText = settingsList[i].enumValues[value];
    }
    const auto width = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, settingY, valueText.c_str(), i != selectedSettingIndex);
  }

  // Draw version text above button hints
  renderer.drawText(SMALL_FONT_ID, pageWidth - 20 - renderer.getTextWidth(SMALL_FONT_ID, CROSSPOINT_VERSION),
                    pageHeight - 60, CROSSPOINT_VERSION);

  // Draw help text
  // const auto labels = mappedInput.mapLabels("« Save", "Toggle", "", "");
  const auto labels = mappedInput.mapLabels("« 저장", "토글", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
