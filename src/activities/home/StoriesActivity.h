#pragma once

#include <HalMemory.h>

#include "activities/UiListActivity.h"
#include "components/themes/BaseTheme.h"
#include "stories/InkStorySession.h"

class StoriesActivity final : public UiListActivity {
  static constexpr size_t MAX_FILES = 32;
  struct FileName {
    char text[96]{};
  };
  enum class View { Library, Reading, Choices, Error };

  FileName files[MAX_FILES];
  uint16_t plays[MAX_FILES]{};  // Finished playthroughs per file.
  size_t fileCount = 0;
  bool initialized = false;
  View view = View::Library;
  HalMemory::PsramBuffer bytes;
  InkStorySession session;  // Must die before its backing story bytes.
  size_t textOffset = 0;  // Current page is session.text()[textOffset, nextOffset).
  size_t nextOffset = 0;
  size_t pageLines = 0;
  size_t pageMaxLines = 1;
  int markerSpace = 0;  // Width kept free at the end of the page's last line.
  size_t openIndex = 0;  // Library row of the open story.
  uint32_t storyCrc = 0;  // Ties a save to the exact story bytes.
  bool ended = false;     // Ending reached: counted once, nothing to resume.
  bool saved = false;     // Progress already saved for the current page.
  bool sleeping = false;  // prepareForSleep() ran; keep the wake marker.
  static constexpr uint32_t FLASH_MS = 1500;
  bool flashChoice = false;  // Briefly showing lastChoice instead of the page.
  uint32_t flashUntil = 0;
  char lastChoice[160]{};  // Text of the choice that started this passage.
  bool lastPage = false;
  bool inlineChoices = false;  // Choices fit below the last narration page.
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  char path[112]{};

  bool installExample();
  void scan();
  void openStory(size_t index, bool startOver = false);
  void saveProgress();
  void nextPage();
  void previousPage();
  size_t passageStart() const;
  size_t previousPageStart() const;
  void rememberChoice(int index);
  void showPage();
  void resetSelection();
  const char* markerLabel() const;
  int markerWidth(const char* label) const;
  bool storyScreen() const { return view == View::Reading || view == View::Choices; }
  Rect contentArea() const;
  void returnToLibrary();

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawChrome() override;
  void drawFooter() override;
  bool handleCustomInput() override;

 public:
  StoriesActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
};
