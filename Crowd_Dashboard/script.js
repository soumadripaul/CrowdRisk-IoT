/* =====================================================================
   CROWD RISK MONITOR — dashboard client
   Subscribes to the Ground Node's MQTT topics over WebSockets and
   drives every panel, lamp, and the split-flap occupancy counters.
   Requires a broker with a WebSocket listener (e.g. Mosquitto with a
   `listener 9001` / `protocol websockets` block) — plain MQTT on
   port 1883 cannot be reached directly from a browser.
   ===================================================================== */

(() => {
  "use strict";

  // Must match SENSOR_TIMEOUT on the Ground Node (ms) — used here only
  // to flag the dashboard's own link to the broker as stale, separate
  // from the ceilingOnline field (Ceiling → Ground link).
  const STALE_TIMEOUT_MS = 5000;
  const MAX_LOG_LINES = 80;

  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const $ = (id) => document.getElementById(id);

  const banner = $("banner");
  const zoneGridEl = $("zoneGrid");
  const flapInDigits = $("flapIn").querySelector(".flapboard__digits");
  const flapOutDigits = $("flapOut").querySelector(".flapboard__digits");
  const flapPeopleDigits = $("flapPeople").querySelector(".flapboard__digits");

  let client = null;
  let lastPacketTime = null;

  // ---------------------------------------------------------------
  // Small formatting helpers
  // ---------------------------------------------------------------
  const pad = (n) => n.toString().padStart(2, "0");
  const fmt1 = (n) => (Number.isFinite(n) ? n.toFixed(1) : "–");
  const fmt0 = (n) => (Number.isFinite(n) ? Math.round(n).toString() : "–");
  const clampNum = (n, lo, hi) => Math.min(hi, Math.max(lo, Number.isFinite(n) ? n : lo));

  function riskClass(str) {
    if (!str) return "unknown";
    const s = String(str).toLowerCase();
    return s === "safe" || s === "moderate" || s === "danger" ? s : "unknown";
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"']/g, (c) => ({
      "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
    }[c]));
  }

  // ---------------------------------------------------------------
  // Clock
  // ---------------------------------------------------------------
  function updateClock() {
    const d = new Date();
    $("clock").textContent = `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
  }
  updateClock();
  setInterval(updateClock, 1000);

  // ---------------------------------------------------------------
  // Event log
  // ---------------------------------------------------------------
  function appendLog(message, level = "system") {
    const body = $("logBody");
    const line = document.createElement("div");
    line.className = `log__line level-${level}`;
    const t = new Date();
    line.innerHTML = `<span class="t">[${pad(t.getHours())}:${pad(t.getMinutes())}:${pad(t.getSeconds())}]</span> ${escapeHtml(message)}`;
    body.appendChild(line);
    while (body.children.length > MAX_LOG_LINES) body.removeChild(body.firstChild);
    body.scrollTop = body.scrollHeight;
  }

  $("clearLogBtn").addEventListener("click", () => {
    $("logBody").innerHTML = "";
  });

  // ---------------------------------------------------------------
  // Split-flap digit board
  // ---------------------------------------------------------------
  function renderFlap(digitsEl, valueStr) {
    const isFirstRender = digitsEl.children.length === 0;

    while (digitsEl.children.length < valueStr.length) {
      const flap = document.createElement("div");
      flap.className = "flap";
      flap.textContent = "0";
      digitsEl.appendChild(flap);
    }
    while (digitsEl.children.length > valueStr.length) {
      digitsEl.removeChild(digitsEl.lastChild);
    }

    for (let i = 0; i < valueStr.length; i++) {
      const flapEl = digitsEl.children[i];
      const newDigit = valueStr[i];
      if (flapEl.textContent === newDigit) continue;

      if (isFirstRender || reduceMotion) {
        flapEl.textContent = newDigit;
        continue;
      }

      flapEl.classList.add("flipping");
      setTimeout(() => {
        flapEl.textContent = newDigit;
        flapEl.classList.remove("flipping");
      }, 160);
    }
  }

  // ---------------------------------------------------------------
  // VL53L5CX 8x8 zone grid
  // ---------------------------------------------------------------
  function initZoneGrid() {
    for (let i = 0; i < 64; i++) {
      const cell = document.createElement("span");
      zoneGridEl.appendChild(cell);
    }
  }
  initZoneGrid();

  function renderZoneGrid(occupiedCount) {
    const n = clampNum(occupiedCount, 0, 64);
    const cells = zoneGridEl.children;
    for (let i = 0; i < cells.length; i++) {
      cells[i].classList.toggle("on", i < n);
    }
  }

  // ---------------------------------------------------------------
  // Panel risk state
  // ---------------------------------------------------------------
  function setPanelRisk(panelName, riskStr) {
    const panel = document.querySelector(`.panel[data-panel="${panelName}"]`);
    if (panel) panel.dataset.risk = riskClass(riskStr);
  }

  // ---------------------------------------------------------------
  // Apply an incoming data-topic payload to every panel
  // ---------------------------------------------------------------
  function updateFromPayload(payload) {
    lastPacketTime = Date.now();

    // Occupancy
    renderFlap(flapInDigits, String(payload.entry ?? 0).padStart(3, "0"));
    renderFlap(flapOutDigits, String(payload.exit ?? 0).padStart(3, "0"));
    renderFlap(flapPeopleDigits, String(payload.people ?? 0).padStart(2, "0"));
    setPanelRisk("occupancy", payload.occupiedRisk);
    $("occupiedRiskValue").textContent = (payload.occupiedRisk || "–").toString().toUpperCase();

    // Density
    $("densityValue").textContent = fmt1(payload.densityPercent);
    $("zoneCount").textContent = payload.zonesOccupied ?? 0;
    renderZoneGrid(payload.zonesOccupied ?? 0);
    setPanelRisk("density", payload.densityRisk);
    $("densityRiskValue").textContent = (payload.densityRisk || "–").toString().toUpperCase();

    // Environment
    $("tempValue").textContent = fmt1(payload.temperature);
    $("humidityValue").textContent = fmt0(payload.humidity);
    setPanelRisk("environment", payload.envRisk);
    $("envRiskValue").textContent = (payload.envRisk || "–").toString().toUpperCase();

    // Noise
    $("noiseValue").textContent = fmt0(payload.noisePercent);
    $("noiseDbValue").textContent = fmt1(payload.noiseDb);
    $("noiseMeterFill").style.width = `${clampNum(payload.noisePercent, 0, 100)}%`;
    setPanelRisk("noise", payload.noiseRisk);
    $("noiseRiskValue").textContent = (payload.noiseRisk || "–").toString().toUpperCase();

    // Node health
    const online = !!payload.ceilingOnline;
    $("ceilingDot").classList.toggle("online", online);
    $("ceilingStatus").textContent = online ? "ONLINE" : "OFFLINE";
    $("packetNumber").textContent = payload.packetNumber ?? "–";
    setPanelRisk("node", payload.overallRisk);
    $("nodeOverallValue").textContent = (payload.overallRisk || "–").toString().toUpperCase();

    // Overall banner
    banner.dataset.risk = riskClass(payload.overallRisk);
    $("bannerValue").textContent = (payload.overallRisk || "UNKNOWN").toString().toUpperCase();

    appendLog(
      `packet #${payload.packetNumber ?? "?"} — overall ${(payload.overallRisk || "?").toString().toUpperCase()}, ` +
      `people ${payload.people ?? "?"}, ceiling ${online ? "online" : "offline"}`,
      riskClass(payload.overallRisk)
    );
  }

  // ---------------------------------------------------------------
  // Packet freshness ticker (dashboard ⇄ broker link, not the
  // ESP-NOW link between the two nodes)
  // ---------------------------------------------------------------
  setInterval(() => {
    const packetAgeEl = $("packetAge");
    if (!lastPacketTime) {
      packetAgeEl.textContent = "no packets yet";
      return;
    }
    const ageMs = Date.now() - lastPacketTime;
    const ageSec = Math.round(ageMs / 1000);
    packetAgeEl.textContent = `last packet ${ageSec}s ago`;
    packetAgeEl.parentElement.classList.toggle("stale", ageMs > STALE_TIMEOUT_MS);
  }, 1000);

  // ---------------------------------------------------------------
  // Connection status pill
  // ---------------------------------------------------------------
  function setConnState(state) {
    const dot = $("connDot");
    const label = $("connLabel");
    dot.dataset.state = state;
    const labels = {
      disconnected: "Disconnected",
      connecting: "Connecting…",
      connected: "Connected",
      error: "Connection error",
    };
    label.textContent = labels[state] || state;
  }

  // ---------------------------------------------------------------
  // MQTT (Paho over WebSockets)
  // ---------------------------------------------------------------
  function buildClientId() {
    return "dashboard-" + Math.random().toString(16).slice(2, 10);
  }

  let activeTopics = { data: "crowd/risk/data", status: "crowd/risk/status" };

  function connect(cfg) {
    if (typeof Paho === "undefined") {
      appendLog("Paho MQTT library did not load — check your network / CDN access.", "danger");
      setConnState("error");
      return;
    }

    if (client) {
      try { client.disconnect(); } catch (e) { /* already disconnected */ }
    }

    activeTopics = { data: cfg.dataTopic, status: cfg.statusTopic };
    setConnState("connecting");
    appendLog(`Connecting to ${cfg.host}:${cfg.port}${cfg.path} …`, "system");

   client = new Paho.Client(cfg.host, Number(cfg.port), cfg.path || "/", buildClientId());

    client.onConnectionLost = (res) => {
      setConnState("error");
      appendLog(`Connection lost${res.errorMessage ? ": " + res.errorMessage : ""}`, "danger");
    };

    client.onMessageArrived = (message) => {
      let payload;
      try {
        payload = JSON.parse(message.payloadString);
      } catch (e) {
        appendLog(`Received non-JSON message on ${message.destinationName}`, "system");
        return;
      }
      if (message.destinationName === activeTopics.data) {
        updateFromPayload(payload);
      } else if (message.destinationName === activeTopics.status) {
        appendLog(`Status: ${message.payloadString}`, "system");
      }
    };

    const connectOptions = {
      useSSL: !!cfg.tls,
      timeout: 8,
      onSuccess: () => {
        setConnState("connected");
        appendLog("Connected to broker", "system");
        client.subscribe(activeTopics.data);
        client.subscribe(activeTopics.status);
      },
      onFailure: (err) => {
        setConnState("error");
        appendLog(`Connect failed: ${err.errorMessage || "unknown error"}`, "danger");
      },
    };
    if (cfg.user) connectOptions.userName = cfg.user;
    if (cfg.pass) connectOptions.password = cfg.pass;

    client.connect(connectOptions);
  }

  function disconnect() {
    if (client) {
      try { client.disconnect(); } catch (e) { /* already disconnected */ }
    }
    setConnState("disconnected");
    appendLog("Disconnected", "system");
  }

  // ---------------------------------------------------------------
  // Settings panel wiring
  // ---------------------------------------------------------------
  const connBtn = $("connBtn");
  const settingsPanel = $("settingsPanel");

  connBtn.addEventListener("click", () => {
    const isHidden = settingsPanel.hasAttribute("hidden");
    if (isHidden) settingsPanel.removeAttribute("hidden");
    else settingsPanel.setAttribute("hidden", "");
    connBtn.setAttribute("aria-expanded", String(isHidden));
  });

  // Defaults matching the Ground Node's publishToMQTT() topics
  $("cfgPort").value = 9001;
  $("cfgPath").value = "/mqtt";
  $("cfgDataTopic").value = "crowd/risk/data";
  $("cfgStatusTopic").value = "crowd/risk/status";

  $("settingsForm").addEventListener("submit", (e) => {
    e.preventDefault();
    const cfg = {
      host: $("cfgHost").value.trim(),
      port: $("cfgPort").value.trim(),
      path: $("cfgPath").value.trim() || "/",
      tls: $("cfgTls").checked,
      dataTopic: $("cfgDataTopic").value.trim() || "crowd/risk/data",
      statusTopic: $("cfgStatusTopic").value.trim() || "crowd/risk/status",
      user: $("cfgUser").value.trim(),
      pass: $("cfgPass").value,
    };
    if (!cfg.host || !cfg.port) return;
    connect(cfg);
  });

  $("disconnectBtn").addEventListener("click", disconnect);

  // ---------------------------------------------------------------
  // Boot
  // ---------------------------------------------------------------
  setConnState("disconnected");
  appendLog("Dashboard loaded. Open the connection panel to set your broker and connect.", "system");
})();