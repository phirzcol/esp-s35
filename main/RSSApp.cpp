#include "RSSApp.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include "SettingsStore.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>

RSSApp RSS;

static const char* DEFAULT_FEEDS[3] = {
  "http://feeds.bbci.co.uk/news/world/rss.xml",
  "https://hnrss.org/frontpage",
  "https://www.nasa.gov/rss/dyn/breaking_news.rss",
};

// ---- tiny XML helpers ----
static const char* findTag(const char* p, const char* end, const char* tag) {
  size_t tl = strlen(tag);
  while (p && p < end) {
    p = (const char*)memchr(p, '<', end - p);
    if (!p) return nullptr;
    if ((size_t)(end - p) > tl + 1 && !strncasecmp(p + 1, tag, tl) &&
        (p[1 + tl] == '>' || p[1 + tl] == ' '))
      return (const char*)memchr(p, '>', end - p) + 1;
    p++;
  }
  return nullptr;
}

static void extractText(const char* start, const char* end, const char* closeTag,
                        char* out, size_t outSz) {
  out[0] = 0;
  if (!start) return;
  char close[48];
  snprintf(close, sizeof(close), "</%s", closeTag);
  const char* e = start;
  while (e && e < end) {
    e = (const char*)memchr(e, '<', end - e);
    if (!e) return;
    if (!strncasecmp(e, close, strlen(close))) break;
    e++;
  }
  if (!e || e >= end) return;

  // copy, unwrapping CDATA, stripping tags, decoding common entities
  size_t o = 0;
  const char* p = start;
  if (!strncmp(p, "<![CDATA[", 9)) { p += 9; }
  while (p < e && o < outSz - 1) {
    if (!strncmp(p, "]]>", 3)) break;
    if (*p == '<') {                       // skip embedded HTML tags
      const char* c = (const char*)memchr(p, '>', e - p);
      if (!c) break;
      p = c + 1;
      continue;
    }
    if (*p == '&') {
      if (!strncmp(p, "&amp;", 5)) { out[o++] = '&'; p += 5; continue; }
      if (!strncmp(p, "&lt;", 4)) { out[o++] = '<'; p += 4; continue; }
      if (!strncmp(p, "&gt;", 4)) { out[o++] = '>'; p += 4; continue; }
      if (!strncmp(p, "&quot;", 6)) { out[o++] = '"'; p += 6; continue; }
      if (!strncmp(p, "&#39;", 5) || !strncmp(p, "&apos;", 6)) {
        out[o++] = '\'';
        p += (p[1] == '#') ? 5 : 6;
        continue;
      }
      const char* semi = (const char*)memchr(p, ';', min((long)(e - p), 8L));
      p = semi ? semi + 1 : p + 1;
      continue;
    }
    char c = *p++;
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (c == ' ' && o && out[o - 1] == ' ') continue;  // collapse whitespace
    out[o++] = c;
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
}

void RSSApp::parseFeed(const char* xml, size_t len) {
  _itemCount = 0;
  const char* end = xml + len;
  const char* p = xml;
  bool atom = (strstr(xml, "<feed") && !strstr(xml, "<rss"));
  const char* itemTag = atom ? "entry" : "item";

  while (_itemCount < MAX_ITEMS) {
    p = findTag(p, end, itemTag);
    if (!p) break;
    // Bound this item by the next item start (or end)
    const char* next = findTag(p, end, itemTag);
    const char* ib = next ? next : end;

    Item& it = _items[_itemCount];
    extractText(findTag(p, ib, "title"), ib, "title", it.title, sizeof(it.title));
    extractText(findTag(p, ib, atom ? "updated" : "pubDate"), ib,
                atom ? "updated" : "pubDate", it.date, sizeof(it.date));
    extractText(findTag(p, ib, atom ? "summary" : "description"), ib,
                atom ? "summary" : "description", it.desc, sizeof(it.desc));
    // article link: RSS = <link>url</link>; Atom = <link href="url"/>
    it.link[0] = 0;
    if (atom) {
      const char* l = p;
      while (l && l < ib) {
        l = (const char*)memchr(l, '<', ib - l);
        if (!l) break;
        if (!strncasecmp(l, "<link", 5)) {
          const char* href = strstr(l, "href=\"");
          const char* tagEnd = (const char*)memchr(l, '>', ib - l);
          if (href && tagEnd && href < tagEnd) {
            href += 6;
            const char* q = (const char*)memchr(href, '"', ib - href);
            if (q) {
              size_t n = min((size_t)(q - href), sizeof(it.link) - 1);
              memcpy(it.link, href, n);
              it.link[n] = 0;
              break;
            }
          }
        }
        l++;
      }
    } else {
      extractText(findTag(p, ib, "link"), ib, "link", it.link, sizeof(it.link));
    }
    if (it.title[0]) _itemCount++;
    p = ib;
    if (!next) break;
  }
}

void RSSApp::fetchTask(void* self) {
  RSSApp* a = (RSSApp*)self;
  char* body = nullptr;
  int got = -1;

  {
    HTTPClient http;
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    bool https = !strncmp(a->_fetchUrl, "https", 5);
    NetworkClientSecure secure;
    NetworkClient plain;
    if (https) secure.setInsecure();
    if (http.begin(https ? (NetworkClient&)secure : plain, a->_fetchUrl)) {
      int code = http.GET();
      if (code == 200) {
        String payload = http.getString();   // feeds are typically < 200KB
        got = payload.length();
        body = (char*)ps_malloc(got + 1);
        if (body) memcpy(body, payload.c_str(), got + 1);
      } else {
        snprintf(a->_status, sizeof(a->_status), "HTTP %d", code);
      }
      http.end();
    } else {
      strlcpy(a->_status, "connect failed", sizeof(a->_status));
    }
  }

  if (body && got > 0) {
    a->parseFeed(body, got);
    free(body);
    a->_fetchState = a->_itemCount ? 2 : 3;
    if (!a->_itemCount) strlcpy(a->_status, "no items parsed", sizeof(a->_status));
  } else {
    if (body) free(body);
    a->_fetchState = 3;
  }
  vTaskDelete(nullptr);
}

// Full-story: fetch the article page and extract readable <p> paragraph text
static const size_t FULL_MAX = 8192;

void RSSApp::fullFetchTask(void* self) {
  RSSApp* a = (RSSApp*)self;
  Item& it = a->_items[a->_sel];
  bool ok = false;

  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (compatible; ES3C35P)");
  bool https = !strncmp(it.link, "https", 5);
  NetworkClientSecure secure;
  NetworkClient plain;
  if (https) secure.setInsecure();
  if (http.begin(https ? (NetworkClient&)secure : plain, it.link)) {
    if (http.GET() == 200) {
      String page = http.getString();
      const char* html = page.c_str();
      size_t len = page.length();
      char* out = a->_fullText;
      size_t o = 0;
      const char* p = html;
      const char* end = html + len;
      while (p < end && o < FULL_MAX - 4) {
        // skip script/style blocks entirely
        if (!strncasecmp(p, "<script", 7)) { const char* e = strstr(p, "</script"); if (!e) break; p = e + 8; continue; }
        if (!strncasecmp(p, "<style", 6)) { const char* e = strstr(p, "</style"); if (!e) break; p = e + 7; continue; }
        if (!strncasecmp(p, "<p", 2) && (p[2] == '>' || p[2] == ' ')) {
          const char* tagEnd = (const char*)memchr(p, '>', end - p);
          if (!tagEnd) break;
          const char* close = strstr(tagEnd, "</p");
          if (!close) break;
          // strip inner tags/entities into out
          char para[1024];
          extractText(tagEnd + 1, close + 4, "p", para, sizeof(para));
          size_t pl = strlen(para);
          if (pl > 40 && o + pl + 2 < FULL_MAX) {   // skip nav/caption crumbs
            memcpy(out + o, para, pl);
            o += pl;
            out[o++] = '\n';
            out[o++] = '\n';
          }
          p = close + 4;
          continue;
        }
        p++;
      }
      out[o] = 0;
      ok = (o > 200);
    }
    http.end();
  }
  a->_fullState = ok ? 2 : 3;
  vTaskDelete(nullptr);
}

void RSSApp::startFullFetch() {
  if (_fullState == 1 || !_items[_sel].link[0]) return;
  if (!_fullText) _fullText = (char*)ps_malloc(FULL_MAX);
  if (!_fullText) return;
  _fullText[0] = 0;
  _fullState = 1;
  xTaskCreatePinnedToCore(fullFetchTask, "rssFull", 14336, this, 1, nullptr, 0);
}

void RSSApp::startFetch(const char* url) {
  if (_fetchState == 1) return;
  if (!Sys.wifiConnected) {
    Notify.post("WiFi not connected");
    return;
  }
  strlcpy(_fetchUrl, url, sizeof(_fetchUrl));
  _itemCount = 0;
  _fetchState = 1;
  _mode = LOADING;
  xTaskCreatePinnedToCore(fetchTask, "rssFetch", 12288, this, 1, nullptr, 0);
}

void RSSApp::loadFeeds() {
  Preferences p;
  p.begin("rss", true);
  _feedCount = p.getInt("n", 0);
  if (_feedCount == 0) {
    for (int i = 0; i < 3; i++) strlcpy(_feeds[i], DEFAULT_FEEDS[i], sizeof(_feeds[i]));
    _feedCount = 3;
  } else {
    if (_feedCount > MAX_FEEDS) _feedCount = MAX_FEEDS;
    for (int i = 0; i < _feedCount; i++) {
      char k[8];
      snprintf(k, sizeof(k), "f%d", i);
      String u = p.getString(k, "");
      strlcpy(_feeds[i], u.c_str(), sizeof(_feeds[i]));
    }
  }
  p.end();
}

void RSSApp::saveFeeds() {
  Preferences p;
  p.begin("rss", false);
  p.putInt("n", _feedCount);
  for (int i = 0; i < _feedCount; i++) {
    char k[8];
    snprintf(k, sizeof(k), "f%d", i);
    p.putString(k, _feeds[i]);
  }
  p.end();
}

void RSSApp::onEnter() {
  if (!_items) _items = (Item*)ps_malloc(sizeof(Item) * MAX_ITEMS);
  loadFeeds();
  _mode = FEEDS;
  _scroll = 0;
}

bool RSSApp::onBack() {
  if (_mode == ARTICLE) { _mode = HEADLINES; return true; }
  if (_mode == HEADLINES || _mode == LOADING) { _mode = FEEDS; return true; }
  return false;
}

static const char* feedLabel(const char* url) {
  const char* p = strstr(url, "//");
  return p ? p + 2 : url;
}

void RSSApp::drawFeeds(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  UIRect ab = {c.x + c.w - 100, c.y + 2, 94, 30};
  UIDraw::button(g, ab, "+ Add feed", false, Theme::ACCENT);
  g.setTextDatum(lgfx::middle_left);
  g.setTextColor(Sys.wifiConnected ? Theme::GOOD : Theme::BAD);
  g.drawString(Sys.wifiConnected ? "WiFi OK" : "WiFi offline", c.x + 8, c.y + 17);

  int top = c.y + 38;
  for (int i = 0; i < _feedCount; i++) {
    UIRect r = {c.x + 8, top + i * 48, c.w - 16, 42};
    g.fillRoundRect(r.x, r.y, r.w, r.h, 6, Theme::PANEL);
    g.setTextDatum(lgfx::middle_left);
    g.setTextColor(Theme::TEXT);
    char lbl[46];
    strlcpy(lbl, feedLabel(_feeds[i]), sizeof(lbl));
    g.drawString(lbl, r.x + 10, r.y + r.h / 2);
  }
}

void RSSApp::drawHeadlines(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_left);
  g.setTextColor(Theme::ACCENT);
  char hdr[64];
  snprintf(hdr, sizeof(hdr), "%d headlines", _itemCount);
  g.drawString(hdr, c.x + 6, c.y + 4);

  int top = c.y + 22;
  int listH = c.y + c.h - top;
  g.setClipRect(0, top, c.w, listH);
  for (int i = 0; i < _itemCount; i++) {
    int y = top + i * ROW_H - _scroll;
    if (y + ROW_H < top || y > top + listH) continue;
    g.setTextDatum(lgfx::top_left);
    g.setTextColor(Theme::TEXT);
    // two-line clipped title
    char t1[46], t2[46];
    strlcpy(t1, _items[i].title, sizeof(t1));
    t2[0] = 0;
    if (strlen(_items[i].title) >= sizeof(t1))
      strlcpy(t2, _items[i].title + sizeof(t1) - 1, sizeof(t2));
    g.drawString(t1, 10, y + 4);
    if (t2[0]) g.drawString(t2, 10, y + 20);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString(_items[i].date, 10, y + ROW_H - 15);
    g.drawFastHLine(0, y + ROW_H - 1, c.w, Theme::PANEL);
  }
  int totalH = _itemCount * ROW_H;
  if (totalH > listH) {
    int barH = max(16, listH * listH / totalH);
    int barY = top + (listH - barH) * _scroll / (totalH - listH);
    g.fillRoundRect(c.w - 4, barY, 3, barH, 1, Theme::PANEL_HI);
  }
  g.clearClipRect();
}

void RSSApp::drawArticle(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  Item& it = _items[_sel];
  g.setClipRect(0, c.y, c.w, c.h);
  g.setTextDatum(lgfx::top_left);

  // wrap title + body (summary, or the fetched full story)
  int lineH = g.fontHeight() + 3;
  int y = c.y + 6 - _artScroll;
  int maxW = c.w - 20;

  const char* body = (_fullState == 2 && _fullText && _fullText[0]) ? _fullText : it.desc;
  const char* blocks[2] = {it.title, body};
  for (int b = 0; b < 2; b++) {
    g.setTextColor(b == 0 ? Theme::ACCENT : Theme::TEXT);
    const char* p = blocks[b];
    while (*p) {
      char line[64];
      int o = 0, w = 0;
      int lastSpace = -1;
      while (*p && o < (int)sizeof(line) - 1) {
        char s[2] = {*p, 0};
        w += g.textWidth(s);
        if (w > maxW) break;
        if (*p == ' ') lastSpace = o;
        line[o++] = *p++;
      }
      if (*p && lastSpace > 0 && o - lastSpace < 14) {   // word wrap
        p -= (o - lastSpace - 1);
        o = lastSpace;
      }
      line[o] = 0;
      if (y > c.y - lineH && y < c.y + c.h) g.drawString(line, c.x + 10, y);
      y += lineH;
    }
    y += lineH / 2;
    if (b == 0) {
      g.setTextColor(Theme::TEXT_DIM);
      if (y > c.y - lineH && y < c.y + c.h) g.drawString(it.date, c.x + 10, y);
      y += lineH + lineH / 2;
    }
  }
  g.clearClipRect();

  // full-story affordance pinned at the bottom
  if (it.link[0]) {
    g.setTextDatum(lgfx::bottom_center);
    if (_fullState == 1) {
      g.setTextColor(Theme::WARN);
      g.drawString("Loading full story...", c.w / 2, c.y + c.h - 2);
    } else if (_fullState == 3) {
      g.setTextColor(Theme::BAD);
      g.drawString("Full story unavailable", c.w / 2, c.y + c.h - 2);
    } else if (_fullState != 2) {
      g.setTextColor(Theme::ACCENT);
      g.drawString("Double-tap for full story", c.w / 2, c.y + c.h - 2);
    }
  }
}

void RSSApp::draw(lgfx::LGFX_Sprite& g) {
  if (_mode == ADDING && Kbd.done()) {
    if (!Kbd.cancelled() && strlen(Kbd.text()) > 8 && _feedCount < MAX_FEEDS) {
      strlcpy(_feeds[_feedCount], Kbd.text(), sizeof(_feeds[0]));
      _feedCount++;
      saveFeeds();
    }
    Kbd.close();
    _mode = FEEDS;
  }
  if (_mode == LOADING) {
    if (_fetchState == 2) { _fetchState = 0; _mode = HEADLINES; _scroll = 0; }
    else if (_fetchState == 3) {
      _fetchState = 0;
      Notify.post(_status[0] ? _status : "Fetch failed");
      _mode = FEEDS;
    } else {
      UIRect c = contentArea();
      g.setTextDatum(lgfx::middle_center);
      g.setTextColor(Theme::TEXT_DIM);
      g.drawString("Fetching feed...", c.w / 2, c.y + c.h / 2);
      int t = (millis() / 120) % 12;
      for (int i = 0; i < 12; i++) {
        float a = i * PI / 6;
        uint16_t col = (i == t) ? Theme::ACCENT : Theme::PANEL_HI;
        g.fillCircle(c.w / 2 + (int)(28 * cosf(a)), c.y + c.h / 2 + 40 + (int)(28 * sinf(a)), 3, col);
      }
      return;
    }
  }
  switch (_mode) {
    case FEEDS: case ADDING: drawFeeds(g); break;
    case HEADLINES: drawHeadlines(g); break;
    case ARTICLE: drawArticle(g); break;
    default: break;
  }
}

void RSSApp::handleTouch(int x, int y, bool pressed) {
  bool tap = false;
  if (pressed && !_wasPressed) {
    _pressX = x; _pressY = y;
    _scrollStart = (_mode == ARTICLE) ? _artScroll : _scroll;
    _dragging = false;
  } else if (pressed && _wasPressed) {
    if (abs(y - _pressY) > 8) _dragging = true;
    if (_dragging) {
      int v = _scrollStart + (_pressY - y);
      if (v < 0) v = 0;
      if (_mode == ARTICLE) {
        if (v > 12000) v = 12000;
        _artScroll = v;
      } else if (_mode == HEADLINES) {
        UIRect c = contentArea();
        int listH = c.h - 22;
        int maxS = _itemCount * ROW_H > listH ? _itemCount * ROW_H - listH : 0;
        if (v > maxS) v = maxS;
        _scroll = v;
      }
    }
  } else if (!pressed && _wasPressed) {
    if (!_dragging) tap = true;
  }
  _wasPressed = pressed;
  if (!tap) return;
  x = _pressX; y = _pressY;

  UIRect c = contentArea();
  switch (_mode) {
    case FEEDS: {
      UIRect ab = {c.x + c.w - 100, c.y + 2, 94, 30};
      if (ab.contains(x, y)) {
        Kbd.open("Feed URL (http...)", "https://");
        _mode = ADDING;
        return;
      }
      int top = c.y + 38;
      int idx = (y - top) / 48;
      if (idx >= 0 && idx < _feedCount && y >= top) startFetch(_feeds[idx]);
      break;
    }
    case HEADLINES: {
      int top = c.y + 22;
      if (y < top) break;
      int idx = (y - top + _scroll) / ROW_H;
      if (idx >= 0 && idx < _itemCount) {
        _sel = idx;
        _artScroll = 0;
        _fullState = 0;   // fresh article: summary first
        _mode = ARTICLE;
      }
      break;
    }
    case ARTICLE: {
      // double-tap loads the full story from the article link
      static uint32_t lastTap = 0;
      if (millis() - lastTap < 450 && _fullState == 0) startFullFetch();
      lastTap = millis();
      break;
    }
    default: break;
  }
}
