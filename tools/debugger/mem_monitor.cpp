/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Feb. 23, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "mem_monitor.h"
#include "camel/core/mm/profiler.h"

#include <httplib.h>
#include <sstream>
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace debugger {

namespace {

// Web UI HTML (inline, single file)
const char *const kWebUIHtml = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Camel GC Memory Visualization</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', system-ui, sans-serif; background: #1a1d23; color: #e4e6eb; padding: 20px; }
    h1 { margin-bottom: 16px; font-size: 1.5rem; color: #58a6ff; }
    .summary { display: flex; gap: 16px; flex-wrap: wrap; margin-bottom: 20px; }
    .card { background: #21262d; border-radius: 8px; padding: 16px; min-width: 180px; }
    .card h3 { font-size: 0.75rem; color: #8b949e; text-transform: uppercase; margin-bottom: 8px; }
    .card .val { font-size: 1.25rem; font-weight: 600; color: #58a6ff; }
    .regions { display: grid; gap: 12px; }
    .region { background: #21262d; border-radius: 8px; overflow: hidden; }
    .region-header { padding: 12px 16px; display: flex; justify-content: space-between; align-items: center; background: #2d333b; cursor: pointer; }
    .region-header:hover { background: #373e47; }
    .region-name { font-weight: 600; }
    .region-stats { font-size: 0.875rem; color: #8b949e; }
    .region-viz { height: 24px; background: #161b22; margin: 8px 16px; border-radius: 4px; overflow: hidden; display: flex; }
    .region-viz .used { background: linear-gradient(90deg, #238636, #2ea043); }
    .region-viz .free { background: #30363d; }
    .region-body { padding: 0 16px 12px; }
    .obj-list { font-size: 0.75rem; font-family: 'Consolas', monospace; max-height: 120px; overflow-y: auto; }
    .obj-row { padding: 4px 0; border-bottom: 1px solid #21262d; }
    .obj-row:last-child { border-bottom: none; }
    .addr { color: #8b949e; }
    .size { color: #58a6ff; }
    .age { color: #d29922; }
    .refresh { margin-bottom: 16px; color: #8b949e; font-size: 0.875rem; }
    .error { color: #f85149; padding: 16px; }
    .toolbar { display: flex; gap: 12px; align-items: center; margin-bottom: 16px; }
    .btn { padding: 8px 16px; border-radius: 6px; border: none; font-weight: 600; cursor: pointer; font-size: 0.875rem; }
    .btn-continue { background: #238636; color: white; }
    .btn-continue:hover { background: #2ea043; }
    .btn-continue:disabled { background: #30363d; color: #8b949e; cursor: not-allowed; }
    .step-status { color: #d29922; font-size: 0.875rem; }
    .last-alloc { font-size: 0.8rem; color: #8b949e; font-family: Consolas, monospace; }
  </style>
</head>
<body>
  <h1>Camel GC Memory Layout</h1>
  <div class="toolbar">
    <div class="refresh">Auto-refresh 500ms | <a href="/api/snapshot" style="color:#58a6ff">Raw JSON</a></div>
    <button id="btn-continue" class="btn btn-continue">Continue</button>
    <span id="step-status" class="step-status"></span>
    <span id="last-alloc" class="last-alloc"></span>
  </div>
  <div id="content"></div>
  <script>
    function fmtBytes(n) {
      if (n < 1024) return n + ' B';
      if (n < 1024*1024) return (n/1024).toFixed(2) + ' KB';
      return (n/(1024*1024)).toFixed(2) + ' MB';
    }
    function render(data) {
      const div = document.getElementById('content');
      if (!data.regions) { div.innerHTML = '<div class="error">No data</div>'; return; }
      let totalUsed = 0, totalCap = 0;
      data.regions.forEach(r => {
        totalUsed += r.used || 0;
        totalCap += r.capacity || 0;
      });
      let html = '<div class="summary">';
      html += '<div class="card"><h3>Regions</h3><div class="val">' + data.regions.length + '</div></div>';
      html += '<div class="card"><h3>Used</h3><div class="val">' + fmtBytes(totalUsed) + '</div></div>';
      html += '<div class="card"><h3>Total</h3><div class="val">' + fmtBytes(totalCap) + '</div></div>';
      html += '</div><div class="regions">';
      data.regions.forEach(r => {
        const cap = r.capacity || 1;
        const used = r.used || 0;
        const pct = Math.min(100, (used/cap)*100);
        html += '<div class="region">';
        html += '<div class="region-header">';
        html += '<span class="region-name">' + r.name + '</span>';
        html += '<span class="region-stats">' + fmtBytes(used) + ' / ' + fmtBytes(cap) + ' (' + r.objectCount + ' objects)</span>';
        html += '</div>';
        if (cap > 0) {
          html += '<div class="region-viz">';
          html += '<div class="used" style="width:' + pct + '%"></div>';
          html += '<div class="free" style="width:' + (100-pct) + '%"></div>';
          html += '</div>';
        }
        if (r.objects && r.objects.length > 0) {
          html += '<div class="region-body"><div class="obj-list">';
          r.objects.slice(0, 20).forEach(o => {
            html += '<div class="obj-row"><span class="addr">0x' + o.addr.toString(16) + '</span> size=<span class="size">' + o.size + '</span> age=<span class="age">' + o.age + '</span></div>';
          });
          if (r.objects.length > 20) html += '<div class="obj-row">... total ' + r.objects.length + ' objects</div>';
          html += '</div></div>';
        }
        html += '</div>';
      });
      html += '</div>';
      div.innerHTML = html;
    }
    function fetchData() {
      fetch('/api/snapshot').then(r=>r.json()).then(d=>{ render(d); return d; }).catch(e=>{
        document.getElementById('content').innerHTML = '<div class="error">Failed to fetch data: ' + e + '</div>';
      });
    }
    document.getElementById('btn-continue').onclick = function() {
      this.disabled = true;
      document.getElementById('step-status').textContent = '';
      fetch('/api/continue', { method: 'POST' }).then(()=>{}).catch(()=>{});
    };
    function pollStepStatus() {
      fetch('/api/step-paused').then(r=>r.json()).then(d=>{
        const btn = document.getElementById('btn-continue');
        const st = document.getElementById('step-status');
        if (d.paused) { btn.disabled = false; st.textContent = 'Paused after alloc – click Continue'; }
        else { btn.disabled = true; st.textContent = ''; }
      }).catch(()=>{});
      fetch('/api/last-alloc').then(r=>r.json()).then(d=>{
        const la = document.getElementById('last-alloc');
        if (d.ptr !== undefined) la.textContent = 'Last: ' + (d.space||'?') + ' 0x' + d.ptr.toString(16) + ' ' + fmtBytes(d.size||0);
        else la.textContent = '';
      }).catch(()=>{});
    }
    fetchData();
    setInterval(fetchData, 500);
    setInterval(pollStepStatus, 200);
  </script>
</body>
</html>
)HTML";

} // namespace

MemMonitor::MemMonitor() = default;

MemMonitor::~MemMonitor() { stop(); }

void MemMonitor::start(int port) {
    if (running_) return;
    port_   = port;
    running_ = true;
    scanStop_ = false;
    httpStop_ = false;

    scanThread_ = std::thread(&MemMonitor::scanThreadLoop, this);
    httpThread_ = std::thread(&MemMonitor::httpServerLoop, this);

    std::cout << "[MemMonitor] Memory monitor started at http://127.0.0.1:" << port_
              << " (open in browser to view GC memory layout)" << std::endl;
}

void MemMonitor::pauseAndWaitForContinue(const void *ptr, size_t size, const char *space) {
    if (!allocStepEnabled_ || !running_)
        return;
    try {
        std::ostringstream oss;
        oss << "{\"ptr\":" << reinterpret_cast<uintptr_t>(ptr) << ",\"size\":" << size
            << ",\"space\":\"" << (space ? space : "") << "\"}";
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            lastAllocJson_ = oss.str();
        }
        std::string json = mm::profiler::snapshotToJson();
        {
            std::lock_guard<std::mutex> lock(jsonMutex_);
            latestJson_ = std::move(json);
        }
    } catch (const std::exception &e) {
        std::cerr << "[MemMonitor] Snapshot exception: " << e.what() << std::endl;
    }
    paused_.store(true);
    continueRequested_.store(false);
    {
        std::unique_lock<std::mutex> lock(continueMutex_);
        continueCond_.wait(lock, [this] {
            return continueRequested_.load() || !running_ || !allocStepEnabled_;
        });
    }
    paused_.store(false);
}

void MemMonitor::stop() {
    if (!running_) return;
    running_  = false;
    scanStop_ = true;
    httpStop_ = true;
    allocStepEnabled_   = false;
    continueRequested_  = true;
    continueCond_.notify_all();

    // Signal HTTP server to stop (listen is blocking)
    auto *svr = reinterpret_cast<httplib::Server *>(serverPtr_.load());
    if (svr)
        svr->stop();

    if (scanThread_.joinable())
        scanThread_.join();
    if (httpThread_.joinable())
        httpThread_.join();
}

void MemMonitor::scanThreadLoop() {
    while (!scanStop_) {
        try {
            std::string json = mm::profiler::snapshotToJson();
            {
                std::lock_guard<std::mutex> lock(jsonMutex_);
                latestJson_ = std::move(json);
            }
        } catch (const std::exception &e) {
            std::cerr << "[MemMonitor] Scan exception: " << e.what() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void MemMonitor::httpServerLoop() {
    httplib::Server svr;
    serverPtr_.store(&svr);

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        res.set_content(kWebUIHtml, "text/html; charset=utf-8");
    });

    svr.Get("/api/snapshot", [this](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(jsonMutex_);
        if (latestJson_.empty()) {
            res.set_content("{}", "application/json");
        } else {
            res.set_content(latestJson_, "application/json");
        }
    });

    svr.Get("/api/step-paused", [this](const httplib::Request &, httplib::Response &res) {
        res.set_content(paused_.load() ? "{\"paused\":true}" : "{\"paused\":false}",
                       "application/json");
    });

    svr.Get("/api/last-alloc", [this](const httplib::Request &, httplib::Response &res) {
        std::lock_guard<std::mutex> lock(jsonMutex_);
        res.set_content(lastAllocJson_.empty() ? "{}" : lastAllocJson_, "application/json");
    });

    svr.Post("/api/continue", [this](const httplib::Request &, httplib::Response &res) {
        continueRequested_.store(true);
        continueCond_.notify_one();
        res.set_content("{\"ok\":true}", "application/json");
    });

    svr.set_payload_max_length(1024);
    svr.set_keep_alive_timeout(1);

    if (!svr.listen("127.0.0.1", port_)) {
        std::cerr << "[MemMonitor] HTTP server failed to start, port " << port_ << " may be in use" << std::endl;
    }
    serverPtr_.store(nullptr);
}

} // namespace debugger
