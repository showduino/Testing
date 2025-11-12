const state = {
  socket: null,
  reconnectTimer: null,
};

function connectSocket() {
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = `${protocol}//${location.host}/ws`;
  const ws = new WebSocket(url);

  ws.addEventListener('open', () => {
    console.log('WebSocket connected');
    if (state.reconnectTimer) {
      clearTimeout(state.reconnectTimer);
      state.reconnectTimer = null;
    }
  });

  ws.addEventListener('close', () => {
    console.log('WebSocket closed');
    state.reconnectTimer = setTimeout(connectSocket, 2500);
  });

  ws.addEventListener('message', evt => {
    try {
      const payload = JSON.parse(evt.data);
      document.getElementById('fps').textContent = payload.fps?.toFixed?.(1) ?? payload.fps;
      document.getElementById('packets').textContent = payload.packets;
      document.getElementById('manual').textContent = payload.manual ? 'true' : 'false';
      document.getElementById('uptime').textContent = `${Math.round(payload.uptime / 1000)}s`;
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
    document.getElementById('brightness').value = cfg?.pixels?.brightness ?? 200;
    document.getElementById('pixelCount').value = cfg?.pixels?.count ?? 300;
  } catch (err) {
    console.error('Failed to load config', err);
  }
}

function bindActions() {
  document.getElementById('downloadLogs').addEventListener('click', () => {
    window.location.href = '/logs/run_latest.txt';
  });

  document.getElementById('saveLighting').addEventListener('click', async () => {
    const pixels = parseInt(document.getElementById('pixelCount').value, 10);
    const brightness = parseInt(document.getElementById('brightness').value, 10);
    const body = JSON.stringify({ pixels: { count: pixels, brightness } });
    try {
      await fetch('/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    } catch (err) {
      console.error('Save failed', err);
    }
  });
}

function init() {
  document.getElementById('version').textContent = `v${document.body.dataset.version ?? ''}`;
  document.getElementById('ip').textContent = document.body.dataset.ip ?? '0.0.0.0';
  connectSocket();
  fetchConfig();
  bindActions();
}

document.addEventListener('DOMContentLoaded', init);
