#pragma once
#include "AppBase.h"

// RSS/Atom reader: feed list -> headlines -> article. Fetch+parse on core 0.
class RSSApp : public AppBase {
public:
  const char* name() const override { return "RSS"; }
  void onEnter() override;
  bool onBack() override;
  void draw(lgfx::LGFX_Sprite& g) override;
  void handleTouch(int x, int y, bool pressed) override;

private:
  enum Mode { FEEDS, LOADING, HEADLINES, ARTICLE, ADDING };
  struct Item {
    char title[112];
    char date[40];
    char desc[512];
    char link[192];
  };
  static const int MAX_FEEDS = 6;
  static const int MAX_ITEMS = 30;
  static const int ROW_H = 52;

  Mode _mode = FEEDS;
  char _feeds[MAX_FEEDS][160];
  int _feedCount = 0;
  Item* _items = nullptr;        // PSRAM
  int _itemCount = 0;
  int _sel = 0;
  int _scroll = 0, _artScroll = 0;
  char _status[48] = "";

  // fetch task handshake
  volatile int _fetchState = 0;  // 0 idle, 1 running, 2 done, 3 failed
  char _fetchUrl[192];
  // full-story fetch
  char* _fullText = nullptr;     // PSRAM
  volatile int _fullState = 0;   // 0 none, 1 running, 2 done, 3 failed

  bool _wasPressed = false;
  bool _dragging = false;
  int _pressX = 0, _pressY = 0, _scrollStart = 0;

  void loadFeeds();
  void saveFeeds();
  void startFetch(const char* url);
  static void fetchTask(void* self);
  void startFullFetch();
  static void fullFetchTask(void* self);
  void parseFeed(const char* xml, size_t len);
  void drawFeeds(lgfx::LGFX_Sprite& g);
  void drawHeadlines(lgfx::LGFX_Sprite& g);
  void drawArticle(lgfx::LGFX_Sprite& g);
};

extern RSSApp RSS;
