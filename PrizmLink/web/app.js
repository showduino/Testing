/* ──────────────────────────────────────────────
   PRIZMLINK CONTROL APP.JS
   Handles status updates, lighting control,
   and WebSocket/HTTP communication
   ────────────────────────────────────────────── */

console.log('[PrizmLink] WebUI started');

// DOM elements (cached lookups)
const fpsEl = document.getElementById('fps');
const packetsEl = document.getElementById('packets');
const manualEl = document.getElementById('manual');
const uptimeEl = document.getElementById('uptime');
const connectionEl = document.getElementById('connection');
const brightnessEl = document.getElementById('brightness');
const pixelCountEl = document.getElementById('pixelCount');
const versionEl = document.getElementById('version');
const ipEl = document.getElementById('ip');
const downloadLogsBtn = document.getElementById('downloadLogs');
const saveLightingBtn = document.getElementById('saveLighting');

// Populate meta information from body data attributes
const metaVersion = document.body.dataset.version || '';
const metaIp = document.body.dataset.ip || location.hostname || '0.0.0.0';

if (versionEl) versionEl.textContent = metaVersion ? `v${metaVersion}` : 'v0.0.0';
if (ipEl) ipEl.textContent = metaIp;

// WebSocket connection (falls back to polling if necessary)
let ws;
let pollTimer = null;
const WS_PROTOCOL = location.protocol === 'https:' ? 'wss' : 'ws';
const wsURL = `${WS_PROTOCOL}://${metaIp}/ws`;

function setConnectionState(connected) {
  if (!connectionEl) return;
  connectionEl.textContent = connected ? 'Connected' : 'Disconnected';
  connectionEl.classList.toggle('connected', connected);
  connectionEl.classList.toggle('disconnected', !connected);
}

function renderTelemetry(data) {
  if (!data) return;
  const fpsValue = typeof data.fps === 'number' ? data.fps.toFixed(1) : '0.0';
  const packetsValue = data.packets ?? 0;
  const manualValue = data.manual ? 'Enabled' : 'Disabled';
  const uptimeSeconds = typeof data.uptime === 'number'
    ? Math.round(data.uptime / 1000)
    : 0;

  if (fpsEl) fpsEl.textContent = fpsValue;
  if (packetsEl) packetsEl.textContent = packetsValue;
  if (manualEl) manualEl.textContent = manualValue;
  if (uptimeEl) uptimeEl.textContent = `${uptimeSeconds}s`;
}

function handleMessage(message) {
  try {
    const payload = typeof message === 'string' ? JSON.parse(message) : message;
    renderTelemetry(payload);
  } catch (err) {
    console.warn('[PrizmLink] Failed to parse telemetry payload:', err);
  }
}

function startPollingFallback() {
  if (pollTimer) return;
  console.log('[PrizmLink] Starting HTTP polling fallback');
  pollTimer = setInterval(async () => {
    try {
      const res = await fetch('/status');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      handleMessage(data);
      setConnectionState(true);
    } catch (err) {
      setConnectionState(false);
    }
  }, 3000);
}

function stopPollingFallback() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

function connectWebSocket() {
  try {
    ws = new WebSocket(wsURL);
  } catch (err) {
    console.error('[PrizmLink] WebSocket connect exception:', err);
    startPollingFallback();
    return;
  }

  ws.onopen = () => {
    console.log('[PrizmLink] Connected to WebSocket');
    setConnectionState(true);
    stopPollingFallback();
  };

  ws.onmessage = (event) => handleMessage(event.data);

  ws.onclose = () => {
    console.warn('[PrizmLink] WebSocket closed, retrying...');
    setConnectionState(false);
    startPollingFallback();
    setTimeout(connectWebSocket, 3000);
  };

  ws.onerror = (event) => {
    console.error('[PrizmLink] WebSocket error:', event);
    ws.close();
  };
}

// Lighting config helpers
function clamp(value, min, max, fallback) {
  if (!Number.isFinite(value)) return fallback;
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

async function loadLightingConfig() {
  try {
    const res = await fetch('/config');
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const cfg = await res.json();
    if (brightnessEl) brightnessEl.value = cfg?.pixels?.brightness ?? 200;
    if (pixelCountEl) pixelCountEl.value = cfg?.pixels?.count ?? 300;
  } catch (err) {
    console.error('[PrizmLink] Failed to load config:', err);
  }
}

async function saveLightingConfig() {
  let pixels = clamp(parseInt(pixelCountEl.value, 10), 1, 1024, 300);
  let brightness = clamp(parseInt(brightnessEl.value, 10), 0, 255, 200);

  pixelCountEl.value = pixels;
  brightnessEl.value = brightness;

  const body = JSON.stringify({ pixels: { count: pixels, brightness } });
  try {
    const res = await fetch('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    console.log('[PrizmLink] Lighting config saved');
  } catch (err) {
    console.error('[PrizmLink] Save failed:', err);
  }
}

function bindEvents() {
  if (downloadLogsBtn) {
    downloadLogsBtn.addEventListener('click', () => {
      window.location.href = '/logs/run_latest.txt';
    });
  }

  if (saveLightingBtn) {
    saveLightingBtn.addEventListener('click', saveLightingConfig);
  }
}

async function init() {
  setConnectionState(false);
  bindEvents();
  await loadLightingConfig();
  connectWebSocket();
}

document.addEventListener('DOMContentLoaded', init);
