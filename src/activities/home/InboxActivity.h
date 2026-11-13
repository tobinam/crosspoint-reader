#pragma once

#include "activities/UiListActivity.h"
#include "inbox/InboxStore.h"

// Inbox tile: saved pages a computer pushed over USB (src/inbox). List newest first,
// open a page to read it (marks it read), hold Confirm on a page to delete it.
class InboxActivity final : public UiListActivity {
  enum class View { List, Page, ConfirmDelete };
  static constexpr size_t MAX_PAGE_STARTS = 32;

  inbox::Entry entries[inbox::CAPACITY];
  size_t entryCount = 0;
  uint32_t loadedGeneration = 0;
  View view = View::List;
  inbox::Page page;
  bool pageLoaded = false;
  size_t openIndex = 0;
  // Body paging: starts of the pages seen so far, filled while rendering.
  uint16_t pageStarts[MAX_PAGE_STARTS]{};
  size_t pageIndex = 0;
  size_t pageEnd = 0;
  bool lastPage = true;

  void reload();
  void openPage(size_t index);
  void backToList();
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  bool handleCustomInput() override;

 public:
  InboxActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
};
