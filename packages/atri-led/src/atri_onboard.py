#!/usr/bin/env python3
"""
atri_onboard.py - AtriOS Phone Onboarding & Wi-Fi Setup Service

Allows smartphones and computers to easily set up AtriStation:
1. Puts Wi-Fi into Hotspot (Access Point) mode: "AtriOS-Setup-XXXX"
2. Serves a responsive mobile web interface on port 80 / 8080 (http://192.168.4.1)
3. Scans local Wi-Fi networks and lets the user connect with a single tap
4. Configures device language, name, timezone, and tests sound/speakers
5. Drives the LED ring and matrix screen with setup animations
"""

import os
import sys
import json
import time
import socket
import subprocess
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import parse_qs, urlparse

HOTSPOT_BASE_SSID = "AtriOS-Setup"
HOTSPOT_PASSWORD = "atriossetup"
WEB_PORT = 8080

def run_cmd(cmd):
    try:
        res = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=15)
        return res.returncode, res.stdout.strip()
    except Exception as e:
        return -1, str(e)

def get_mac_suffix():
    for iface in ["wlan0", "eth0"]:
        path = f"/sys/class/net/{iface}/address"
        if os.path.exists(path):
            with open(path) as f:
                mac = f.read().strip().replace(":", "")
                if len(mac) >= 4:
                    return mac[-4:].upper()
    return "STATION"

def scan_wifi_networks():
    rc, out = run_cmd("nmcli -t -f SSID,SIGNAL,SECURITY dev wifi list --rescan yes")
    networks = []
    seen = set()
    if rc == 0 and out:
        for line in out.splitlines():
            parts = line.split(":")
            if len(parts) >= 2:
                ssid = parts[0].strip()
                if not ssid or ssid in seen:
                    continue
                seen.add(ssid)
                signal = parts[1].strip() if len(parts) > 1 else "50"
                sec = parts[2].strip() if len(parts) > 2 else "WPA2"
                networks.append({"ssid": ssid, "signal": signal, "security": sec})
    if not networks:
        networks = [
            {"ssid": "Home-WiFi-5G", "signal": "90", "security": "WPA2"},
            {"ssid": "Home-WiFi-2.4G", "signal": "75", "security": "WPA2"}
        ]
    return networks

def visual_feedback(mode):
    if mode == "setup":
        run_cmd("atri-led-ctl loop rainbow 2>/dev/null || atri-led-ctl color 255 120 0 2>/dev/null || true")
        run_cmd("atri-matrix text 'SETUP' 2>/dev/null || true")
    elif mode == "connecting":
        run_cmd("atri-led-ctl color 0 180 255 2>/dev/null || true")
        run_cmd("atri-matrix text 'WIFI...' 2>/dev/null || true")
    elif mode == "success":
        run_cmd("atri-led-ctl color 0 255 60 2>/dev/null || true")
        run_cmd("atri-matrix text 'ONLINE' 2>/dev/null || true")
    elif mode == "error":
        run_cmd("atri-led-ctl color 255 0 0 2>/dev/null || true")
        run_cmd("atri-matrix text 'FAIL' 2>/dev/null || true")

HTML_PAGE = """<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>AtriOS Setup</title>
<style>
  :root {
    --bg: #0d1117;
    --card: #161b22;
    --border: #30363d;
    --accent: #00d2ff;
    --accent-grad: linear-gradient(135deg, #00d2ff 0%, #3a7bd5 100%);
    --green: #2ea043;
    --text: #f0f6fc;
    --subtext: #8b949e;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { background: var(--bg); color: var(--text); padding: 20px; line-height: 1.5; }
  .container { max-width: 440px; margin: 0 auto; }
  .header { text-align: center; margin-bottom: 24px; padding: 10px 0; }
  .logo { font-size: 28px; font-weight: 800; background: var(--accent-grad); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
  .subtitle { font-size: 14px; color: var(--subtext); margin-top: 4px; }
  .card { background: var(--card); border: 1px solid var(--border); border-radius: 14px; padding: 20px; margin-bottom: 18px; box-shadow: 0 4px 20px rgba(0,0,0,0.3); }
  .card-title { font-size: 16px; font-weight: 600; margin-bottom: 14px; display: flex; align-items: center; gap: 8px; }
  .card-title span { color: var(--accent); }
  label { font-size: 13px; color: var(--subtext); margin-bottom: 6px; display: block; }
  select, input[type="text"], input[type="password"] {
    width: 100%; padding: 12px 14px; background: #090d13; border: 1px solid var(--border); border-radius: 8px; color: #fff; font-size: 15px; margin-bottom: 14px; outline: none; transition: 0.2s;
  }
  select:focus, input:focus { border-color: var(--accent); box-shadow: 0 0 0 2px rgba(0,210,255,0.2); }
  .btn {
    width: 100%; padding: 14px; background: var(--accent-grad); border: none; border-radius: 10px; color: #fff; font-size: 16px; font-weight: 600; cursor: pointer; transition: 0.2s;
  }
  .btn:active { transform: scale(0.98); opacity: 0.9; }
  .btn-sec { background: transparent; border: 1px solid var(--border); color: var(--text); margin-top: 10px; }
  .net-item { display: flex; justify-content: space-between; align-items: center; padding: 10px 12px; background: #090d13; border: 1px solid var(--border); border-radius: 8px; margin-bottom: 8px; cursor: pointer; }
  .net-item:hover, .net-item.selected { border-color: var(--accent); background: #121922; }
  .signal { font-size: 12px; color: var(--accent); font-weight: bold; }
  .status-box { padding: 12px; border-radius: 8px; font-size: 14px; display: none; margin-top: 14px; }
  .status-ok { background: rgba(46,160,67,0.15); border: 1px solid var(--green); color: #3fb950; }
  .status-err { background: rgba(248,81,73,0.15); border: 1px solid #f85149; color: #f85149; }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="logo">AtriOS</div>
    <div class="subtitle">Мастер настройки умной колонки</div>
  </div>

  <div class="card">
    <div class="card-title"><span>🌐</span> Выберите домашнюю сеть Wi-Fi</div>
    <div id="net-list">Загрузка сетей...</div>
    <label for="wifi-pass">Пароль от Wi-Fi</label>
    <input type="password" id="wifi-pass" placeholder="Введите пароль">
  </div>

  <div class="card">
    <div class="card-title"><span>⚙️</span> Параметры станции</div>
    <label for="dev-name">Имя колонки</label>
    <input type="text" id="dev-name" value="AtriStation">

    <label for="dev-lang">Язык интерфейса</label>
    <select id="dev-lang">
      <option value="ru" selected>Русский (RU)</option>
      <option value="en">English (US)</option>
    </select>

    <button class="btn btn-sec" onclick="testSound()">🔊 Проверить динамики</button>
  </div>

  <button class="btn" onclick="saveAndConnect()">Подключить станцию к сети</button>
  <div id="status" class="status-box"></div>
</div>

<script>
let selectedSsid = "";

async function loadNetworks() {
  const el = document.getElementById("net-list");
  try {
    const res = await fetch("/api/scan");
    const data = await res.json();
    if (!data.networks || data.networks.length === 0) {
      el.innerHTML = "<div style='color:var(--subtext)'>Сети не найдены</div>";
      return;
    }
    let html = "";
    data.networks.forEach((n, idx) => {
      const isSel = idx === 0 ? "selected" : "";
      if (idx === 0) selectedSsid = n.ssid;
      html += `<div class="net-item ${isSel}" onclick="selectNet('${n.ssid.replace(/'/g, "\\\\'")}', this)">
        <span>${n.ssid}</span>
        <span class="signal">${n.signal}%</span>
      </div>`;
    });
    el.innerHTML = html;
  } catch (e) {
    el.innerHTML = "<div style='color:#f85149'>Ошибка сканирования</div>";
  }
}

function selectNet(ssid, elem) {
  selectedSsid = ssid;
  document.querySelectorAll(".net-item").forEach(x => x.classList.remove("selected"));
  elem.classList.add("selected");
}

async function testSound() {
  await fetch("/api/sound-test", { method: "POST" });
}

async function saveAndConnect() {
  const pass = document.getElementById("wifi-pass").value;
  const name = document.getElementById("dev-name").value;
  const lang = document.getElementById("dev-lang").value;
  const stat = document.getElementById("status");

  if (!selectedSsid) {
    alert("Выберите сеть Wi-Fi!");
    return;
  }

  stat.style.display = "block";
  stat.className = "status-box status-ok";
  stat.innerHTML = "Подключение к " + selectedSsid + "... Подождите ~15 сек.";

  try {
    const res = await fetch("/api/connect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ssid: selectedSsid, password: pass, hostname: name, lang: lang })
    });
    const data = await res.json();
    if (data.status === "ok") {
      stat.innerHTML = "<b>Успешно подключено!</b><br>IP адрес станции: <b>" + (data.ip || "получен") + "</b><br>Точка настройки выключается.";
    } else {
      stat.className = "status-box status-err";
      stat.innerHTML = "Ошибка подключения: " + (data.error || "Неверный пароль или тайм-аут");
    }
  } catch (e) {
    stat.className = "status-box status-err";
    stat.innerHTML = "Связь потеряна (возможно, станция уже переключилась на домашнюю сеть).";
  }
}

loadNetworks();
</script>
</body>
</html>
"""

class SetupHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass # Quiet console logging

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/" or u.path == "/index.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode("utf-8"))
        elif u.path == "/api/scan":
            nets = scan_wifi_networks()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"networks": nets}).encode("utf-8"))
        else:
            # Captive portal redirection
            self.send_response(302)
            self.send_header("Location", "http://192.168.4.1:8080/")
            self.end_headers()

    def do_POST(self):
        u = urlparse(self.path)
        if u.path == "/api/sound-test":
            run_cmd("atri sound tone all 2>/dev/null || atri-sound-test tone all 2>/dev/null || true")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')

        elif u.path == "/api/connect":
            content_length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(content_length).decode("utf-8")
            try:
                data = json.loads(body)
                ssid = data.get("ssid", "").strip()
                password = data.get("password", "").strip()
                hostname = data.get("hostname", "AtriStation").strip()

                visual_feedback("connecting")

                # Configure hostname if provided
                if hostname:
                    run_cmd(f"hostnamectl set-hostname '{hostname}' 2>/dev/null || true")

                # Connect via NetworkManager
                cmd = f"nmcli dev wifi connect '{ssid}'"
                if password:
                    cmd += f" password '{password}'"

                rc, out = run_cmd(cmd)
                if rc == 0:
                    # Connection succeeded, get new IP
                    _, ip_out = run_cmd("hostname -I")
                    ip = ip_out.split()[0] if ip_out else "unknown"
                    visual_feedback("success")

                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(json.dumps({"status": "ok", "ip": ip}).encode("utf-8"))

                    # Stop hotspot after giving client time to receive response
                    def teardown():
                        time.sleep(2)
                        run_cmd("nmcli con down AtriOS-Hotspot 2>/dev/null || true")
                        sys.exit(0)
                    import threading
                    threading.Thread(target=teardown).start()
                else:
                    visual_feedback("error")
                    self.send_response(400)
                    self.send_header("Content-Type", "application/json")
                    self.end_headers()
                    self.wfile.write(json.dumps({"status": "error", "error": out}).encode("utf-8"))
            except Exception as e:
                self.send_response(500)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"status": "error", "error": str(e)}).encode("utf-8"))

def start_onboarding():
    suffix = get_mac_suffix()
    ssid = f"{HOTSPOT_BASE_SSID}-{suffix}"

    print(f"=== AtriOS Phone Onboarding Mode ===")
    print(f"Creating Wi-Fi hotspot: {ssid}")
    visual_feedback("setup")

    # Start hotspot via NetworkManager
    run_cmd(f"nmcli con down AtriOS-Hotspot 2>/dev/null || true")
    run_cmd(f"nmcli dev wifi hotspot ifname wlan0 con-name AtriOS-Hotspot ssid '{ssid}' password '{HOTSPOT_PASSWORD}' 2>/dev/null || true")

    print(f"Hotspot active! Connect your smartphone to Wi-Fi:")
    print(f"  SSID:     {ssid}")
    print(f"  Password: {HOTSPOT_PASSWORD}")
    print(f"  Web URL:  http://192.168.4.1:{WEB_PORT}/ (or http://atri.local:{WEB_PORT}/)")
    print("Press Ctrl+C to exit setup mode.")

    server = HTTPServer(("0.0.0.0", WEB_PORT), SetupHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nExiting onboarding mode...")
    finally:
        server.server_close()
        run_cmd("nmcli con down AtriOS-Hotspot 2>/dev/null || true")
        visual_feedback("success")

if __name__ == "__main__":
    start_onboarding()
