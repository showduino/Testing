const state = {
  socket: null,
  reconnectTimer: null,
  meta: {
    version: '',
    ip: '',
  },
};

const els = {};

function $(id) {
  if (!els[id]) {
    els[id] = document.getElementById(id);
  }
  return els[id];
}

function updateConnectionStatus(connected) {
  const statusEl = $('connection');
  if (!statusEl) return;
  statusEl.textContent = connected ? 'Connected' : 'Disconnected';
  statusEl.classList.toggle('connected', connected);
  statusEl.classList.toggle('disconnected', !connected);
}

function renderTelemetry(payload) {
  const fps = typeof payload.fps === 'number' ? payload.fps.toFixed(1) : '-';
  const packets = payload.packets ?? 0;
  const manual = payload.manual ? 'Enabled' : 'Disabled';
  const uptimeSeconds = typeof payload.uptime === 'number'
    ? Math.round(payload.uptime / 1000)
    : 0;

  $('fps').textContent = fps;
  $('packets').textContent = packets;
  $('manual').textContent = manual;
  $('uptime').textContent = `${uptimeSeconds}s`;
}

function connectSocket() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${location.host}/ws`;
  const ws = new WebSocket(url);

  ws.addEventListener('open', () => {
    updateConnectionStatus(true);
    if (state.reconnectTimer) {
      clearTimeout(state.reconnectTimer);
      state.reconnectTimer = null;
    }
  });

  ws.addEventListener('close', () => {
    updateConnectionStatus(false);
    state.reconnectTimer = setTimeout(connectSocket, 2500);
  });

  ws.addEventListener('error', () => {
    updateConnectionStatus(false);
  });

  ws.addEventListener('message', evt => {
    try {
      const payload = JSON.parse(evt.data);
      renderTelemetry(payload);
    } catch (err) {
      console.warn('Bad payload', err);
    }
  });

  state.socket = ws;
}

async function fetchConfig() {
  try {
    const res = await fetch('/config');
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const cfg = await res.json();
    $('brightness').value = cfg?.pixels?.brightness ?? 200;
    $('pixelCount').value = cfg?.pixels?.count ?? 300;
  } catch (err) {
    console.error('Failed to load config', err);
  }
}

function bindActions() {
  $('downloadLogs').addEventListener('click', () => {
    window.location.href = '/logs/run_latest.txt';
  });

  $('saveLighting').addEventListener('click', async () => {
    let pixels = parseInt($('pixelCount').value, 10);
    let brightness = parseInt($('brightness').value, 10);

    pixels = Number.isFinite(pixels) ? Math.min(Math.max(pixels, 1), 1024) : 300;
    brightness = Number.isFinite(brightness) ? Math.min(Math.max(brightness, 0), 255) : 200;

    $('pixelCount').value = pixels;
    $('brightness').value = brightness;

    const body = JSON.stringify({ pixels: { count: pixels, brightness } });
    try {
      await fetch('/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body,
      });
    } catch (err) {
      console.error('Save failed', err);
    }
  });
}

function initMeta() {
  state.meta.version = document.body.dataset.version || '';
  state.meta.ip = document.body.dataset.ip || '0.0.0.0';

  const versionEl = $('version');
  const ipEl = $('ip');

  if (versionEl) versionEl.textContent = state.meta.version ? `v${state.meta.version}` : '';
  if (ipEl) ipEl.textContent = state.meta.ip;
}

function init() {
  initMeta();
  updateConnectionStatus(false);
  connectSocket();
  fetchConfig();
  bindActions();
}

document.addEventListener('DOMContentLoaded', init);
