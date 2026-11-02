import RFB from '../core/rfb.js';

const screen = document.getElementById('screen');
const status = document.getElementById('status');
const keyboardPanel = document.getElementById('keyboard-panel');
const keyboardToggle = document.getElementById('keyboard-toggle');
const textEntry = document.getElementById('text-entry');
const params = new URLSearchParams(location.search);
const host = params.get('host') || location.hostname;
const port = params.get('port') || '6080';
const path = params.get('path') || 'websockify';
const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
const rfb = new RFB(screen, `${scheme}://${host}:${port}/${path}`);
rfb.scaleViewport = true;
rfb.resizeSession = false;
rfb.addEventListener('connect', () => { status.textContent = 'Conectado'; });
rfb.addEventListener('disconnect', (event) => {
  status.textContent = event.detail.clean ? 'Desconectado' : 'Conexão perdida';
});
rfb.addEventListener('credentialsrequired', () => {
  rfb.sendCredentials({ password: window.prompt('Senha VNC:') || '' });
});

function localPoint(touch) {
  const canvas = screen.querySelector('canvas');
  if (!canvas) return null;
  const rect = canvas.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(rect.width - 1, touch.clientX - rect.left)),
    y: Math.max(0, Math.min(rect.height - 1, touch.clientY - rect.top)),
  };
}

function dispatchMouse(type, point, button = 0, buttons = 0) {
  const canvas = screen.querySelector('canvas');
  if (!point || !canvas) return;
  const rect = canvas.getBoundingClientRect();
  canvas.dispatchEvent(new MouseEvent(type, {
    bubbles: true,
    cancelable: true,
    view: window,
    clientX: rect.left + point.x,
    clientY: rect.top + point.y,
    button,
    buttons,
  }));
}
function sendMove(point) {
  dispatchMouse('mousemove', point);
}
function sendButton(point, mask, down) {
  const button = mask === 4 ? 2 : 0;
  const buttons = down ? (mask === 4 ? 2 : 1) : 0;
  dispatchMouse(down ? 'mousedown' : 'mouseup', point, button, buttons);
}

let contact = null;
let multitouchBlocked = false;
function stopContact(point, cancelled = false) {
  if (!contact) return;
  clearTimeout(contact.timer);
  if (contact.button) sendButton(point || contact.last, contact.button, false);
  else if (!cancelled) {
    sendMove(point || contact.last);
    sendButton(point || contact.last, 1, true);
    sendButton(point || contact.last, 1, false);
  }
  contact = null;
}

screen.addEventListener('touchstart', (event) => {
  event.preventDefault();
  event.stopImmediatePropagation();
  screen.querySelector('canvas')?.focus();
  if (contact || event.changedTouches.length !== 1) {
    if (contact) stopContact(contact.last, true);
    multitouchBlocked = true;
    return;
  }
  if (multitouchBlocked) return;
  const point = localPoint(event.changedTouches[0]);
  if (!point) return;
  contact = { start: point, last: point, button: 0, timer: setTimeout(() => {
    if (!contact || contact.button !== 0) return;
    contact.button = 4;
    sendButton(contact.last, 4, true);
  }, 500) };
}, { capture: true, passive: false });

screen.addEventListener('touchmove', (event) => {
  event.preventDefault();
  event.stopImmediatePropagation();
  if (multitouchBlocked || !contact || event.touches.length !== 1) return;
  const point = localPoint(event.touches[0]);
  if (!point) return;
  const dx = point.x - contact.start.x;
  const dy = point.y - contact.start.y;
  if (contact.button === 0 && Math.hypot(dx, dy) > 8) {
    clearTimeout(contact.timer);
    contact.button = 1;
    sendButton(contact.start, 1, true);
  }
  if (contact.button) sendMove(point);
  contact.last = point;
}, { capture: true, passive: false });

screen.addEventListener('touchend', (event) => {
  event.preventDefault();
  event.stopImmediatePropagation();
  if (event.touches.length > 0) return;
  const touch = event.changedTouches[0];
  stopContact(touch ? localPoint(touch) : null);
  multitouchBlocked = false;
}, { capture: true, passive: false });

screen.addEventListener('touchcancel', (event) => {
  event.preventDefault();
  event.stopImmediatePropagation();
  stopContact(null, true);
  multitouchBlocked = false;
}, { capture: true, passive: false });

keyboardToggle.addEventListener('click', () => {
  keyboardPanel.hidden = !keyboardPanel.hidden;
  if (!keyboardPanel.hidden) textEntry.focus({ preventScroll: true });
  else textEntry.blur();
});

const specialKeysyms = {
  Escape: [0xff1b, 'Escape'], Backspace: [0xff08, 'Backspace'],
  Tab: [0xff09, 'Tab'], Enter: [0xff0d, 'Enter'],
  ArrowUp: [0xff52, 'ArrowUp'], ArrowDown: [0xff54, 'ArrowDown'],
  ArrowLeft: [0xff51, 'ArrowLeft'], ArrowRight: [0xff53, 'ArrowRight'],
};
function sendTapKey(keysym, code) { rfb.sendKey(keysym, code); }
document.querySelectorAll('[data-key]').forEach((button) => {
  button.addEventListener('click', () => {
    const [keysym, code] = specialKeysyms[button.dataset.key];
    sendTapKey(keysym, code);
    textEntry.focus({ preventScroll: true });
  });
});

textEntry.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' || event.key === 'Backspace' || event.key === 'Tab') {
    event.preventDefault();
    const [keysym, code] = specialKeysyms[event.key];
    sendTapKey(keysym, code);
  }
});
textEntry.addEventListener('input', (event) => {
  const text = event.data;
  if (!text || event.inputType !== 'insertText') return;
  for (const character of text) {
    const codepoint = character.codePointAt(0);
    const keysym = codepoint <= 0xff ? codepoint : 0x01000000 | codepoint;
    rfb.sendKey(keysym, null);
  }
  textEntry.value = '';
});

window.addEventListener('pagehide', () => stopContact(null, true));
