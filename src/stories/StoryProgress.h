#pragma once

#include <cstddef>
#include <cstdint>

class InkStorySession;

// Per-story progress under /stories/.save/: <name>.sav (resume) and <name>.plays
// (finished count). Main-loop only; holds no files open between calls.
namespace storyProgress {
// Saves the VM at its current choice point, the passage text and the page offset.
// Written to a temp file and renamed, so a failed save keeps the previous one.
bool save(const char* storyName, uint32_t storyCrc, const InkStorySession& session, uint32_t pageOffset);
// Restores into session when a save exists for these exact story bytes.
// Returns false (session cleared) when there is no usable save.
bool restore(const char* storyName, uint32_t storyCrc, const uint8_t* bytes, size_t length, InkStorySession& session,
             uint32_t& pageOffset);
void discard(const char* storyName);
uint16_t plays(const char* storyName);
void addPlay(const char* storyName);
// Wake marker: the story open when the device went to sleep.
void markWakeResume(const char* storyName);
bool wakeResumePending();
// Reads and deletes the marker (one attempt only, so a crash cannot loop).
bool takeWakeResume(char* storyName, size_t size);
void clearWakeResume();
}  // namespace storyProgress
