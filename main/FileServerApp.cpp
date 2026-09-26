#include "FileServerApp.h"
#include "SDCardHAL.h"
#include "SystemTask.h"
#include "DisplayHAL.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <dirent.h>

FileServerApp FileServer;

static WebServer* s_srv = nullptr;
static File s_upFile;
static char* s_actionOut = nullptr;
static uint32_t* s_hitsOut = nullptr;

static void note(const char* fmt, const char* arg) {
  if (s_actionOut) snprintf(s_actionOut, 64, fmt, arg);
  if (s_hitsOut) (*s_hitsOut)++;
}

static String htmlEscape(const String& s) {
  String o = s;
  o.replace("&", "&amp;");
  o.replace("<", "&lt;");
  o.replace(">", "&gt;");
  return o;
}

static void handleRoot() {
  String dir = s_srv->hasArg("d") ? s_srv->arg("d") : "/";
  if (dir.indexOf("..") >= 0) dir = "/";

  String page =
    "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width'>"
    "<title>ES3C35P Files</title><style>body{font-family:sans-serif;background:#111;color:#eee;margin:20px}"
    "a{color:#4cf}td{padding:4px 10px}h2{color:#4cf}.del{color:#f66}</style></head><body>"
    "<h2>SD card - " + htmlEscape(dir) + "</h2>"
    "<form method=POST action='/upload?d=" + dir + "' enctype='multipart/form-data'>"
    "<input type=file name=f> <input type=submit value=Upload></form><hr><table>";

  if (dir != "/") {
    String up = dir.substring(0, dir.lastIndexOf('/'));
    if (up.length() == 0) up = "/";
    page += "<tr><td><a href='/?d=" + up + "'>[..]</a></td><td></td><td></td></tr>";
  }

  char vfs[192];
  snprintf(vfs, sizeof(vfs), "/sdcard%s", dir == "/" ? "" : dir.c_str());
  DIR* d = opendir(vfs);
  if (d) {
    struct dirent* de;
    while ((de = readdir(d))) {
      if (de->d_name[0] == '.') continue;
      String full = (dir == "/" ? "" : dir) + "/" + de->d_name;
      if (de->d_type == DT_DIR) {
        page += "<tr><td><a href='/?d=" + full + "'>[" + htmlEscape(de->d_name) + "]</a></td><td></td><td></td></tr>";
      } else {
        page += "<tr><td><a href='/dl?f=" + full + "'>" + htmlEscape(de->d_name) +
                "</a></td><td></td><td><a class=del href='/rm?f=" + full +
                "&d=" + dir + "' onclick='return confirm(\"Delete?\")'>delete</a></td></tr>";
      }
    }
    closedir(d);
  }
  page += "</table></body></html>";
  s_srv->send(200, "text/html", page);
  note("page view %s", dir.c_str());
}

static void handleDownload() {
  String f = s_srv->arg("f");
  if (f.indexOf("..") >= 0 || !f.startsWith("/")) { s_srv->send(400, "text/plain", "bad path"); return; }
  File file = SD_MMC.open(f);
  if (!file || file.isDirectory()) { s_srv->send(404, "text/plain", "not found"); return; }
  s_srv->sendHeader("Content-Disposition", "attachment");
  s_srv->streamFile(file, "application/octet-stream");
  file.close();
  note("sent %s", f.c_str());
}

static void handleDelete() {
  String f = s_srv->arg("f");
  String dir = s_srv->hasArg("d") ? s_srv->arg("d") : "/";
  if (f.indexOf("..") >= 0 || !f.startsWith("/")) { s_srv->send(400, "text/plain", "bad path"); return; }
  SD_MMC.remove(f);
  note("deleted %s", f.c_str());
  s_srv->sendHeader("Location", "/?d=" + dir);
  s_srv->send(303);
}

static void handleUpload() {
  HTTPUpload& up = s_srv->upload();
  String dir = s_srv->hasArg("d") ? s_srv->arg("d") : "/";
  if (up.status == UPLOAD_FILE_START) {
    String path = (dir == "/" ? "" : dir) + "/" + up.filename;
    s_upFile = SD_MMC.open(path, FILE_WRITE);
  } else if (up.status == UPLOAD_FILE_WRITE && s_upFile) {
    s_upFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END && s_upFile) {
    s_upFile.close();
    note("received %s", up.filename.c_str());
  }
}

void FileServerApp::onEnter() {
  _lastAction[0] = 0;
  _hits = 0;
  if (!Sys.wifiConnected) { Notify.post("WiFi not connected"); return; }
  if (!SDCard.mount()) { Notify.post("No SD card"); return; }
  s_actionOut = _lastAction;
  s_hitsOut = &_hits;
  s_srv = new WebServer(80);
  s_srv->on("/", handleRoot);
  s_srv->on("/dl", handleDownload);
  s_srv->on("/rm", handleDelete);
  s_srv->on("/upload", HTTP_POST, []() {
    String dir = s_srv->hasArg("d") ? s_srv->arg("d") : "/";
    s_srv->sendHeader("Location", "/?d=" + dir);
    s_srv->send(303);
  }, handleUpload);
  s_srv->begin();
  _running = true;
}

void FileServerApp::onExit() {
  // Closing the app closes the server: no background transfers possible
  if (s_srv) {
    s_srv->stop();
    delete s_srv;
    s_srv = nullptr;
  }
  if (s_upFile) s_upFile.close();
  s_actionOut = nullptr;
  s_hitsOut = nullptr;
  _running = false;
}

void FileServerApp::update(uint32_t) {
  if (s_srv) {
    // several polls per frame keeps transfers moving at UI frame rate
    for (int i = 0; i < 8; i++) s_srv->handleClient();
  }
}

void FileServerApp::draw(lgfx::LGFX_Sprite& g) {
  UIRect c = contentArea();
  g.setTextDatum(lgfx::top_center);

  if (!_running) {
    g.setTextColor(Theme::BAD);
    g.drawString("Server not running", c.w / 2, c.y + c.h / 2 - 20);
    g.setTextColor(Theme::TEXT_DIM);
    g.drawString(Sys.wifiConnected ? "SD card missing" : "Connect WiFi first",
                 c.w / 2, c.y + c.h / 2 + 6);
    return;
  }

  g.setTextColor(Theme::GOOD);
  g.drawString("File server running", c.w / 2, c.y + 24);
  g.setTextSize(2);
  g.setTextColor(Theme::ACCENT);
  char url[48];
  snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
  g.drawString(url, c.w / 2, c.y + 60);
  g.setTextSize(1);
  g.setTextColor(Theme::TEXT_DIM);
  g.drawString("Open in a browser on this network", c.w / 2, c.y + 100);
  g.drawString("Upload / download / delete SD files", c.w / 2, c.y + 120);

  char st[64];
  snprintf(st, sizeof(st), "requests: %lu", (unsigned long)_hits);
  g.setTextColor(Theme::TEXT);
  g.drawString(st, c.w / 2, c.y + 170);
  if (_lastAction[0]) {
    g.setTextColor(Theme::WARN);
    g.drawString(_lastAction, c.w / 2, c.y + 192);
  }

  g.setTextColor(Theme::PANEL_HI);
  g.drawString("Server stops when you leave this app", c.w / 2, c.y + c.h - 24);
}
