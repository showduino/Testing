const ROWS = 10;
const COLS = 21;
const LED_COUNT = ROWS * COLS;

const Commands = Object.freeze({
  FRAME: 0x01,
  SETTINGS: 0x02,
  EFFECT: 0x03,
  ANIMATION_META: 0x04,
});

const state = {
  frames: [createBlankFrame()],
  currentFrame: 0,
  tool: 'pen',
  brushSize: 1,
  color: '#ff004d',
  onion: false,
  zoom: 18,
  fps: 24,
  playing: false,
  loop: true,
  brightness: 128,
  speed: 50,
  mode: 'static',
  ws: null,
  wsConnected: false,
  reconnectTimer: null,
  undoStack: [],
  redoStack: [],
  editingStroke: false,
  shapeStart: null,
  packetsSent: 0,
  previewsSent: 0,
  previewTimer: null,
  animationTimer: null,
  selectedEffect: 'rainbow',
  effectParams: {},
  autoSend: false,
};

const dom = {
  matrixGrid: document.getElementById('matrixGrid'),
  connectionStatus: document.getElementById('connectionStatus'),
  fpsDisplay: document.getElementById('fpsDisplay'),
  packetDisplay: document.getElementById('packetDisplay'),
  toolButtons: Array.from(document.querySelectorAll('.tool')),
  colorPicker: document.getElementById('colorPicker'),
  brushSize: document.getElementById('brushSize'),
  undoBtn: document.getElementById('undoBtn'),
  redoBtn: document.getElementById('redoBtn'),
  zoomSlider: document.getElementById('zoomSlider'),
  onionToggle: document.getElementById('onionToggle'),
  clearFrame: document.getElementById('clearFrame'),
  sendFrameBtn: document.getElementById('sendFrameBtn'),
  sendAnimationBtn: document.getElementById('sendAnimationBtn'),
  playPauseBtn: document.getElementById('playPauseBtn'),
  loopToggle: document.getElementById('loopToggle'),
  fpsSlider: document.getElementById('fpsSlider'),
  fpsValue: document.getElementById('fpsValue'),
  addFrame: document.getElementById('addFrame'),
  duplicateFrame: document.getElementById('duplicateFrame'),
  deleteFrame: document.getElementById('deleteFrame'),
  frameStrip: document.getElementById('frameStrip'),
  frameTemplate: document.getElementById('frameThumbTemplate'),
  brightnessSlider: document.getElementById('brightnessSlider'),
  brightnessValue: document.getElementById('brightnessValue'),
  speedSlider: document.getElementById('speedSlider'),
  speedValue: document.getElementById('speedValue'),
  modeSelect: document.getElementById('modeSelect'),
  effectSelect: document.getElementById('effectSelect'),
  effectOptions: document.getElementById('effectOptions'),
  applyEffect: document.getElementById('applyEffect'),
  saveAnimationBtn: document.getElementById('saveAnimationBtn'),
  loadAnimationBtn: document.getElementById('loadAnimationBtn'),
  exportJsonBtn: document.getElementById('exportJsonBtn'),
  importJsonInput: document.getElementById('importJsonInput'),
  espFileList: document.getElementById('espFileList'),
};

const effectDefinitions = {
  rainbow: {
    params: {
      shift: { label: 'Hue Shift', min: 0, max: 360, value: 0 },
      saturation: { label: 'Saturation', min: 40, max: 100, value: 90 },
      brightness: { label: 'Brightness', min: 30, max: 100, value: 80 },
    },
  },
  fire: {
    params: {
      intensity: { label: 'Intensity', min: 20, max: 100, value: 70 },
      flicker: { label: 'Flicker', min: 1, max: 10, value: 4 },
    },
  },
  twinkle: {
    params: {
      density: { label: 'Density', min: 1, max: 50, value: 12 },
      hue: { label: 'Hue', min: 0, max: 360, value: 210 },
    },
  },
  snow: {
    params: {
      count: { label: 'Flakes', min: 5, max: 60, value: 24 },
    },
  },
  glitch: {
    params: {
      blocks: { label: 'Blocks', min: 1, max: 12, value: 4 },
      chaos: { label: 'Chaos', min: 1, max: 10, value: 6 },
    },
  },
  meteor: {
    params: {
      length: { label: 'Length', min: 3, max: 15, value: 8 },
      hue: { label: 'Hue', min: 0, max: 360, value: 180 },
      trails: { label: 'Trails', min: 1, max: 5, value: 2 },
    },
  },
};

const pointer = {
  active: false,
  id: null,
  lastRow: null,
  lastCol: null,
};

init();

function init() {
  buildMatrixGrid();
  attachToolHandlers();
  attachTimelineHandlers();
  attachDeviceHandlers();
  attachFileHandlers();
  renderEffectOptions();
  updateFrameStrip();
  updateMatrix();
  connectWebSocket();
  fetchAnimationList();
  window.addEventListener('beforeunload', () => state.ws?.close());
}

function buildMatrixGrid() {
  const frag = document.createDocumentFragment();
  for (let row = 0; row < ROWS; row++) {
    for (let col = 0; col < COLS; col++) {
      const pixel = document.createElement('div');
      pixel.className = 'pixel';
      pixel.dataset.row = row;
      pixel.dataset.col = col;
      frag.appendChild(pixel);
    }
  }
  dom.matrixGrid.appendChild(frag);
  dom.matrixGrid.addEventListener('pointerdown', handlePointerDown);
  dom.matrixGrid.addEventListener('pointermove', handlePointerMove);
  window.addEventListener('pointerup', handlePointerUp);
}

function attachToolHandlers() {
  dom.toolButtons.forEach((btn) =>
    btn.addEventListener('click', () => setTool(btn.dataset.tool))
  );
  dom.colorPicker.addEventListener('input', (e) => {
    state.color = e.target.value;
  });
  dom.brushSize.addEventListener('input', (e) => {
    state.brushSize = Number(e.target.value);
  });
  dom.undoBtn.addEventListener('click', undo);
  dom.redoBtn.addEventListener('click', redo);
  dom.zoomSlider.addEventListener('input', (e) => {
    state.zoom = Number(e.target.value);
    dom.matrixGrid.style.setProperty('--pixel-size', `${state.zoom}px`);
  });
  dom.onionToggle.addEventListener('change', (e) => {
    state.onion = e.target.checked;
    updateMatrix();
  });
  dom.clearFrame.addEventListener('click', () => {
    pushUndo();
    state.frames[state.currentFrame] = createBlankFrame();
    updateMatrix();
    updateFrameStrip();
  });
  dom.sendFrameBtn.addEventListener('click', () => sendFrame(currentFrame()));
  dom.sendAnimationBtn.addEventListener('click', sendAnimation);
}

function attachTimelineHandlers() {
  dom.playPauseBtn.addEventListener('click', togglePlayback);
  dom.loopToggle.addEventListener('change', (e) => {
    state.loop = e.target.checked;
  });
  dom.fpsSlider.addEventListener('input', (e) => {
    state.fps = Number(e.target.value);
    dom.fpsValue.textContent = state.fps;
    if (state.playing) {
      stopPlayback();
      startPlayback();
    }
  });
  dom.addFrame.addEventListener('click', () => {
    state.frames.push(createBlankFrame());
    state.currentFrame = state.frames.length - 1;
    updateMatrix();
    updateFrameStrip();
  });
  dom.duplicateFrame.addEventListener('click', () => {
    state.frames.splice(
      state.currentFrame + 1,
      0,
      cloneFrame(currentFrame())
    );
    state.currentFrame++;
    updateFrameStrip();
  });
  dom.deleteFrame.addEventListener('click', () => {
    if (state.frames.length === 1) return;
    state.frames.splice(state.currentFrame, 1);
    state.currentFrame = Math.max(0, state.currentFrame - 1);
    updateMatrix();
    updateFrameStrip();
  });
  dom.effectSelect.addEventListener('change', () => {
    state.selectedEffect = dom.effectSelect.value;
    renderEffectOptions();
    if (state.mode === 'effect') {
      sendEffectConfig();
    }
  });
  dom.applyEffect.addEventListener('click', () => {
    pushUndo();
    const generated = generateEffectFrame(state.selectedEffect, {
      ...state.effectParams[state.selectedEffect],
    });
    state.frames[state.currentFrame] = generated;
    updateMatrix();
    updateFrameStrip();
    if (state.mode === 'effect') {
      sendEffectConfig();
    }
  });
}

function attachDeviceHandlers() {
  dom.brightnessSlider.addEventListener('input', (e) => {
    state.brightness = Number(e.target.value);
    dom.brightnessValue.textContent = state.brightness;
    queueDeviceUpdate();
  });
  dom.speedSlider.addEventListener('input', (e) => {
    state.speed = Number(e.target.value);
    dom.speedValue.textContent = state.speed;
    queueDeviceUpdate();
  });
  dom.modeSelect.addEventListener('change', (e) => {
    state.mode = e.target.value;
    queueDeviceUpdate();
    if (state.mode === 'effect') {
      sendEffectConfig();
    }
  });
}

function attachFileHandlers() {
  dom.saveAnimationBtn.addEventListener('click', async () => {
    const name = prompt('Save animation as:');
    if (!name) return;
    await saveAnimationToEsp(name);
    await fetchAnimationList();
  });
  dom.loadAnimationBtn.addEventListener('click', async () => {
    const selected = dom.espFileList.value;
    if (!selected) return alert('Select a file first.');
    await loadAnimationFromEsp(selected);
  });
  dom.exportJsonBtn.addEventListener('click', exportAnimationJson);
  dom.importJsonInput.addEventListener('change', importAnimationJson);
}

function setTool(tool) {
  state.tool = tool;
  dom.toolButtons.forEach((btn) =>
    btn.classList.toggle('active', btn.dataset.tool === tool)
  );
}

function handlePointerDown(e) {
  if (!(e.target instanceof HTMLElement) || !e.target.classList.contains('pixel')) {
    return;
  }
  e.preventDefault();
  pointer.active = true;
  pointer.id = e.pointerId;
  dom.matrixGrid.setPointerCapture(e.pointerId);
  const row = Number(e.target.dataset.row);
  const col = Number(e.target.dataset.col);
  pointer.lastRow = row;
  pointer.lastCol = col;
  pushUndo();
  state.editingStroke = true;
  state.shapeStart = { row, col };
  applyToolAction(row, col, false);
}

function handlePointerMove(e) {
  if (!pointer.active) return;
  if (!(e.target instanceof HTMLElement)) return;
  if (!e.target.classList.contains('pixel')) return;
  const row = Number(e.target.dataset.row);
  const col = Number(e.target.dataset.col);
  if (row === pointer.lastRow && col === pointer.lastCol) return;
  pointer.lastRow = row;
  pointer.lastCol = col;
  applyToolAction(row, col, true);
}

function handlePointerUp(e) {
  if (!pointer.active) return;
  pointer.active = false;
  state.editingStroke = false;
  state.shapeStart = null;
  if (pointer.id !== null && dom.matrixGrid.hasPointerCapture(pointer.id)) {
    dom.matrixGrid.releasePointerCapture(pointer.id);
  }
  pointer.id = null;
  updateFrameStrip();
}

function applyToolAction(row, col, isDragging) {
  switch (state.tool) {
    case 'pen':
      drawBrush(row, col, state.color);
      break;
    case 'eraser':
      drawBrush(row, col, '#000000');
      break;
    case 'fill':
      if (isDragging) return;
      floodFill(row, col, state.color);
      break;
    case 'line':
    case 'rectangle':
    case 'circle':
      if (!state.shapeStart) return;
      state.frames[state.currentFrame] = cloneFrame(
        state.undoStack[state.undoStack.length - 1]
      );
      drawShape(state.shapeStart, { row, col }, state.tool);
      break;
    default:
      break;
  }
  updateMatrix();
}

function drawBrush(row, col, color) {
  const radius = Math.floor(state.brushSize / 2);
  for (let r = row - radius; r <= row + radius; r++) {
    for (let c = col - radius; c <= col + radius; c++) {
      if (r < 0 || r >= ROWS || c < 0 || c >= COLS) continue;
      setFramePixel(state.frames[state.currentFrame], r, c, color);
    }
  }
}

function drawShape(start, end, mode) {
  if (mode === 'line') {
    drawLine(start.row, start.col, end.row, end.col, state.color);
  } else if (mode === 'rectangle') {
    drawRectangle(start, end, state.color);
  } else if (mode === 'circle') {
    const radius = Math.round(
      Math.hypot(end.row - start.row, end.col - start.col)
    );
    drawCircle(start.row, start.col, radius, state.color);
  }
}

function drawLine(r0, c0, r1, c1, color) {
  const dr = Math.abs(r1 - r0);
  const dc = Math.abs(c1 - c0);
  const sr = r0 < r1 ? 1 : -1;
  const sc = c0 < c1 ? 1 : -1;
  let err = dr - dc;
  while (true) {
    setFramePixel(state.frames[state.currentFrame], r0, c0, color);
    if (r0 === r1 && c0 === c1) break;
    const e2 = 2 * err;
    if (e2 > -dc) {
      err -= dc;
      r0 += sr;
    }
    if (e2 < dr) {
      err += dr;
      c0 += sc;
    }
  }
}

function drawRectangle(start, end, color) {
  const top = Math.min(start.row, end.row);
  const bottom = Math.max(start.row, end.row);
  const left = Math.min(start.col, end.col);
  const right = Math.max(start.col, end.col);
  for (let r = top; r <= bottom; r++) {
    for (let c = left; c <= right; c++) {
      if (
        r === top ||
        r === bottom ||
        c === left ||
        c === right ||
        state.brushSize > 1
      ) {
        setFramePixel(state.frames[state.currentFrame], r, c, color);
      }
    }
  }
}

function drawCircle(row, col, radius, color) {
  let x = radius;
  let y = 0;
  let decisionOver2 = 1 - x;
  while (y <= x) {
    plotCirclePoints(row, col, x, y, color);
    y++;
    if (decisionOver2 <= 0) {
      decisionOver2 += 2 * y + 1;
    } else {
      x--;
      decisionOver2 += 2 * (y - x) + 1;
    }
  }
}

function plotCirclePoints(cx, cy, x, y, color) {
  const points = [
    [cx + y, cy + x],
    [cx + x, cy + y],
    [cx - y, cy + x],
    [cx - x, cy + y],
    [cx - y, cy - x],
    [cx - x, cy - y],
    [cx + y, cy - x],
    [cx + x, cy - y],
  ];
  for (const [r, c] of points) {
    if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
      setFramePixel(state.frames[state.currentFrame], r, c, color);
    }
  }
}

function floodFill(row, col, newColor) {
  const frame = currentFrame();
  const targetColor = getFramePixel(frame, row, col);
  if (targetColor === newColor) return;
  const queue = [[row, col]];
  while (queue.length) {
    const [r, c] = queue.shift();
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) continue;
    if (getFramePixel(frame, r, c) !== targetColor) continue;
    setFramePixel(frame, r, c, newColor);
    queue.push([r + 1, c], [r - 1, c], [r, c + 1], [r, c - 1]);
  }
}

function updateMatrix() {
  const frame = currentFrame();
  const prev =
    state.onion && state.currentFrame > 0
      ? state.frames[state.currentFrame - 1]
      : null;
  Array.from(dom.matrixGrid.children).forEach((pixel, index) => {
    const color = frame[index];
    let display = color;
    if (prev && isOff(color)) {
      const prevColor = prev[index];
      if (!isOff(prevColor)) {
        const rgba = hexToRgba(prevColor, 0.35);
        display = rgba;
      }
    }
    pixel.style.background = display;
    pixel.classList.toggle('active', !isOff(color));
  });
}

function updateFrameStrip() {
  dom.frameStrip.innerHTML = '';
  state.frames.forEach((frame, index) => {
    const thumb = dom.frameTemplate.content.firstElementChild.cloneNode(true);
    const canvas = thumb.querySelector('canvas');
    renderThumbnail(frame, canvas);
    thumb.querySelector('.caption').textContent = `Frame ${index + 1}`;
    thumb.classList.toggle('active', index === state.currentFrame);
    thumb.addEventListener('click', () => {
      state.currentFrame = index;
      updateMatrix();
      updateFrameStrip();
    });
    dom.frameStrip.appendChild(thumb);
  });
}

function renderThumbnail(frame, canvas) {
  const ctx = canvas.getContext('2d');
  const scaleX = canvas.width / COLS;
  const scaleY = canvas.height / ROWS;
  for (let row = 0; row < ROWS; row++) {
    for (let col = 0; col < COLS; col++) {
      ctx.fillStyle = frame[xyToIndex(row, col)];
      ctx.fillRect(col * scaleX, row * scaleY, scaleX, scaleY);
    }
  }
}

function togglePlayback() {
  if (state.playing) {
    stopPlayback();
  } else {
    startPlayback();
  }
}

function startPlayback() {
  if (state.playing) return;
  state.playing = true;
  dom.playPauseBtn.textContent = 'Pause';
  const interval = 1000 / state.fps;
  state.animationTimer = setInterval(() => {
    state.currentFrame++;
    if (state.currentFrame >= state.frames.length) {
      if (state.loop) {
        state.currentFrame = 0;
      } else {
        state.currentFrame = state.frames.length - 1;
        stopPlayback();
      }
    }
    updateMatrix();
    updateFrameStrip();
    if (state.wsConnected) {
      sendFrame(currentFrame());
    }
  }, interval);
}

function stopPlayback() {
  state.playing = false;
  dom.playPauseBtn.textContent = 'Play';
  clearInterval(state.animationTimer);
  state.animationTimer = null;
}

function sendFrame(frame) {
  if (!state.wsConnected || !state.ws) return;
  const payload = buildFramePayload(frame);
  state.ws.send(payload);
  state.packetsSent++;
  dom.packetDisplay.textContent = `${state.packetsSent} pkts`;
}

function buildFramePayload(frame) {
  const buffer = new Uint8Array(2 + LED_COUNT * 3);
  buffer[0] = Commands.FRAME;
  buffer[1] = state.brightness;
  for (let i = 0; i < LED_COUNT; i++) {
    const color = frame[i];
    const { r, g, b } = hexToRgb(color);
    const offset = 2 + i * 3;
    buffer[offset] = r;
    buffer[offset + 1] = g;
    buffer[offset + 2] = b;
  }
  return buffer;
}

function sendAnimation() {
  if (!state.wsConnected || !state.ws) return;
  const payload = {
    type: 'animation',
    fps: state.fps,
    loop: state.loop,
    frames: state.frames,
  };
  state.ws.send(JSON.stringify(payload));
}

function queueDeviceUpdate() {
  clearTimeout(queueDeviceUpdate.timer);
  queueDeviceUpdate.timer = setTimeout(sendDeviceSettings, 200);
}

function sendDeviceSettings() {
  if (!state.wsConnected || !state.ws) return;
  const payload = new Uint8Array([Commands.SETTINGS, state.brightness, state.speed, state.mode === 'static' ? 0 : state.mode === 'animation' ? 1 : 2]);
  state.ws.send(payload);
}

function sendEffectConfig(effect = state.selectedEffect) {
  if (!state.wsConnected || !state.ws) return;
  const message = {
    type: 'effect',
    effect,
    params: state.effectParams[effect] || {},
  };
  state.ws.send(JSON.stringify(message));
}

function connectWebSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  state.ws = new WebSocket(`${protocol}://${window.location.host}/ws`);
  state.ws.binaryType = 'arraybuffer';
  state.ws.addEventListener('open', () => {
    state.wsConnected = true;
    updateConnectionStatus(true);
    if (state.reconnectTimer) {
      clearTimeout(state.reconnectTimer);
      state.reconnectTimer = null;
    }
  });
  state.ws.addEventListener('message', handleSocketMessage);
  state.ws.addEventListener('close', () => {
    state.wsConnected = false;
    updateConnectionStatus(false);
    state.reconnectTimer = setTimeout(connectWebSocket, 1500);
  });
  state.ws.addEventListener('error', () => {
    state.ws.close();
  });
}

function handleSocketMessage(event) {
  if (typeof event.data === 'string') {
    try {
      const data = JSON.parse(event.data);
      if (data.fps) {
        dom.fpsDisplay.textContent = `${data.fps.toFixed(1)} FPS`;
      }
      if (data.packets) {
        dom.packetDisplay.textContent = `${data.packets} pkts`;
      }
    } catch {
      // ignore
    }
  }
}

function updateConnectionStatus(connected) {
  dom.connectionStatus.classList.toggle('connected', connected);
  dom.connectionStatus.querySelector('.label').textContent = connected
    ? 'Connected'
    : 'Disconnected';
}

async function fetchAnimationList() {
  try {
    const res = await fetch('/api/animations');
    if (!res.ok) throw new Error('Failed to list animations');
    const files = await res.json();
    dom.espFileList.innerHTML = '';
    files.forEach((name) => {
      const option = document.createElement('option');
      option.value = name;
      option.textContent = name;
      dom.espFileList.appendChild(option);
    });
  } catch (err) {
    console.warn(err);
  }
}

async function saveAnimationToEsp(name) {
  const payload = {
    name,
    fps: state.fps,
    loop: state.loop,
    frames: state.frames,
  };
  await fetch('/api/animations', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}

async function loadAnimationFromEsp(name) {
  const res = await fetch(`/api/animations/${encodeURIComponent(name)}`);
  if (!res.ok) return alert('Failed to load animation');
  const data = await res.json();
  if (!Array.isArray(data.frames)) return;
  state.frames = data.frames.map((frame) => frame.slice());
  state.fps = data.fps || state.fps;
  state.loop = data.loop ?? state.loop;
  state.currentFrame = 0;
  dom.fpsSlider.value = state.fps;
  dom.fpsValue.textContent = state.fps;
  dom.loopToggle.checked = state.loop;
  updateMatrix();
  updateFrameStrip();
}

function exportAnimationJson() {
  const payload = {
    name: 'matrix-animation',
    version: 1,
    rows: ROWS,
    cols: COLS,
    fps: state.fps,
    loop: state.loop,
    frames: state.frames,
  };
  const blob = new Blob([JSON.stringify(payload, null, 2)], {
    type: 'application/json',
  });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'matrix-animation.json';
  a.click();
  URL.revokeObjectURL(url);
}

function importAnimationJson(event) {
  const file = event.target.files?.[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const data = JSON.parse(reader.result);
      if (data.rows !== ROWS || data.cols !== COLS) {
        return alert('Matrix size mismatch');
      }
      state.frames = data.frames.map((frame) => frame.slice());
      state.fps = data.fps || state.fps;
      dom.fpsSlider.value = state.fps;
      dom.fpsValue.textContent = state.fps;
      state.loop = data.loop ?? state.loop;
      dom.loopToggle.checked = state.loop;
      state.currentFrame = 0;
      updateMatrix();
      updateFrameStrip();
    } catch (err) {
      alert('Invalid JSON file');
    }
  };
  reader.readAsText(file);
}

function pushUndo() {
  state.undoStack.push(cloneFrame(currentFrame()));
  if (state.undoStack.length > 30) {
    state.undoStack.shift();
  }
  state.redoStack = [];
}

function undo() {
  if (!state.undoStack.length) return;
  state.redoStack.push(cloneFrame(currentFrame()));
  const frame = state.undoStack.pop();
  state.frames[state.currentFrame] = frame;
  updateMatrix();
  updateFrameStrip();
}

function redo() {
  if (!state.redoStack.length) return;
  state.undoStack.push(cloneFrame(currentFrame()));
  const frame = state.redoStack.pop();
  state.frames[state.currentFrame] = frame;
  updateMatrix();
  updateFrameStrip();
}

function renderEffectOptions() {
  const def = effectDefinitions[state.selectedEffect];
  if (!state.effectParams[state.selectedEffect]) {
    state.effectParams[state.selectedEffect] = Object.fromEntries(
      Object.entries(def.params).map(([key, meta]) => [key, meta.value])
    );
  }
  dom.effectOptions.innerHTML = '';
  Object.entries(def.params).forEach(([key, meta]) => {
    const wrap = document.createElement('label');
    wrap.textContent = `${meta.label}: ${state.effectParams[state.selectedEffect][key]}`;
    const input = document.createElement('input');
    input.type = 'range';
    input.min = meta.min;
    input.max = meta.max;
    input.value = state.effectParams[state.selectedEffect][key];
    input.addEventListener('input', (e) => {
      state.effectParams[state.selectedEffect][key] = Number(e.target.value);
      wrap.firstChild.textContent = `${meta.label}: ${e.target.value}`;
    });
    wrap.appendChild(input);
    dom.effectOptions.appendChild(wrap);
  });
}

function generateEffectFrame(effect, params) {
  switch (effect) {
    case 'rainbow':
      return generateRainbow(params);
    case 'fire':
      return generateFire(params);
    case 'twinkle':
      return generateTwinkle(params);
    case 'snow':
      return generateSnow(params);
    case 'glitch':
      return generateGlitch(params);
    case 'meteor':
      return generateMeteor(params);
    default:
      return createBlankFrame();
  }
}

function generateRainbow({ shift = 0, saturation = 90, brightness = 80 }) {
  const frame = createBlankFrame();
  for (let row = 0; row < ROWS; row++) {
    for (let col = 0; col < COLS; col++) {
      const hue = (col / COLS) * 360 + shift;
      frame[xyToIndex(row, col)] = hsvToHex(hue, saturation, brightness);
    }
  }
  return frame;
}

function generateFire({ intensity = 70, flicker = 4 }) {
  const frame = createBlankFrame();
  const noise = Array.from({ length: ROWS }, () =>
    Array.from({ length: COLS }, () => Math.random())
  );
  for (let row = ROWS - 1; row >= 0; row--) {
    for (let col = 0; col < COLS; col++) {
      const heat =
        noise[row][col] * (row / ROWS) * (intensity / 100) +
        (Math.random() * flicker) / 10;
      const hue = 15 - heat * 15;
      const sat = 100;
      const val = Math.min(100, 30 + heat * 70);
      frame[xyToIndex(row, col)] = hsvToHex(hue, sat, val);
    }
  }
  return frame;
}

function generateTwinkle({ density = 12, hue = 210 }) {
  const frame = createBlankFrame();
  for (let i = 0; i < density; i++) {
    const row = Math.floor(Math.random() * ROWS);
    const col = Math.floor(Math.random() * COLS);
    const brightness = 60 + Math.random() * 40;
    frame[xyToIndex(row, col)] = hsvToHex(hue, 30, brightness);
  }
  return frame;
}

function generateSnow({ count = 24 }) {
  const frame = createBlankFrame('#0b1a2b');
  for (let i = 0; i < count; i++) {
    const row = Math.floor(Math.random() * ROWS);
    const col = Math.floor(Math.random() * COLS);
    frame[xyToIndex(row, col)] = '#f8fbff';
  }
  return frame;
}

function generateGlitch({ blocks = 4, chaos = 6 }) {
  const frame = createBlankFrame();
  for (let i = 0; i < blocks; i++) {
    const width = Math.max(1, Math.floor(Math.random() * chaos));
    const height = Math.max(1, Math.floor(Math.random() * chaos));
    const row = Math.floor(Math.random() * (ROWS - height));
    const col = Math.floor(Math.random() * (COLS - width));
    const color = hsvToHex(Math.random() * 360, 70, 90);
    for (let r = row; r < row + height; r++) {
      for (let c = col; c < col + width; c++) {
        frame[xyToIndex(r, c)] = color;
      }
    }
  }
  return frame;
}

function generateMeteor({ length = 8, hue = 180, trails = 2 }) {
  const frame = createBlankFrame();
  const startCol = Math.floor(Math.random() * COLS);
  for (let i = 0; i < length; i++) {
    const row = Math.min(ROWS - 1, i);
    const col = (startCol + i) % COLS;
    const brightness = 100 - (i / length) * 80;
    frame[xyToIndex(row, col)] = hsvToHex(hue, 80, brightness);
    for (let t = 1; t <= trails; t++) {
      const trailRow = row + t;
      const trailCol = col;
      if (trailRow < ROWS) {
        const fade = brightness * Math.pow(0.7, t);
        frame[xyToIndex(trailRow, trailCol)] = hsvToHex(hue, 60, fade);
      }
    }
  }
  return frame;
}

function currentFrame() {
  return state.frames[state.currentFrame];
}

function createBlankFrame(fill = '#000000') {
  return Array.from({ length: LED_COUNT }, () => fill);
}

function cloneFrame(frame) {
  return frame.slice();
}

function setFramePixel(frame, row, col, color) {
  const idx = xyToIndex(row, col);
  frame[idx] = color;
}

function getFramePixel(frame, row, col) {
  return frame[xyToIndex(row, col)];
}

function xyToIndex(row, col) {
  if (row % 2 === 0) {
    return row * COLS + col;
  }
  return row * COLS + (COLS - 1 - col);
}

function isOff(color) {
  return color === '#000000' || color === '#000';
}

function hexToRgb(hex) {
  const normalized = hex.replace('#', '');
  const bigint = parseInt(normalized, 16);
  if (normalized.length === 6) {
    return {
      r: (bigint >> 16) & 255,
      g: (bigint >> 8) & 255,
      b: bigint & 255,
    };
  }
  return { r: 0, g: 0, b: 0 };
}

function hexToRgba(hex, alpha = 1) {
  const { r, g, b } = hexToRgb(hex);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function hsvToHex(h, s, v) {
  const sat = s / 100;
  const val = v / 100;
  const c = val * sat;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = val - c;
  let r = 0;
  let g = 0;
  let b = 0;
  if (0 <= h && h < 60) {
    r = c;
    g = x;
  } else if (60 <= h && h < 120) {
    r = x;
    g = c;
  } else if (120 <= h && h < 180) {
    g = c;
    b = x;
  } else if (180 <= h && h < 240) {
    g = x;
    b = c;
  } else if (240 <= h && h < 300) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }
  const toHex = (value) =>
    Math.round((value + m) * 255)
      .toString(16)
      .padStart(2, '0');
  return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}
