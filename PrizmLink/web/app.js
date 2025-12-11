const q = (selector, base = document) => base.querySelector(selector);
const qa = (selector, base = document) => base.querySelectorAll(selector);

const smoothScrollTo = (targetSelector) => {
  if (!targetSelector) return;
  const el = typeof targetSelector === 'string' ? document.querySelector(targetSelector) : targetSelector;
  if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
};

const bindNavigation = () => {
  const nav = q('.primary-nav');
  const toggle = q('.nav-toggle');
  toggle?.addEventListener('click', () => {
    nav?.classList.toggle('active');
  });

  qa('[data-target]').forEach((btn) => {
    btn.addEventListener('click', () => {
      smoothScrollTo(btn.dataset.target);
      nav?.classList.remove('active');
    });
  });

  qa('.primary-nav a').forEach((link) => {
    link.addEventListener('click', () => nav?.classList.remove('active'));
  });
};

const animateTimeline = () => {
  const playhead = q('.timeline-track .playhead');
  const timeLabel = q('#timeline-time');
  const duration = 180000; // 3 minutes in ms
  let start = null;

  const formatTime = (ms) => {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = String(Math.floor(totalSeconds / 60)).padStart(2, '0');
    const seconds = String(totalSeconds % 60).padStart(2, '0');
    const millis = String(Math.floor(ms % 1000)).padStart(3, '0');
    return `${minutes}:${seconds}.${millis}`;
  };

  const tick = (ts) => {
    if (!start) start = ts;
    const elapsed = (ts - start) % duration;
    const percent = (elapsed / duration) * 100;
    if (playhead) playhead.style.left = `${percent}%`;
    if (timeLabel) timeLabel.textContent = formatTime(elapsed);
    requestAnimationFrame(tick);
  };

  requestAnimationFrame(tick);
};

const hydrateWaveform = () => {
  const waveform = q('#waveform-bars');
  if (!waveform) return;
  const bars = 28;
  for (let i = 0; i < bars; i += 1) {
    const bar = document.createElement('span');
    bar.style.animationDelay = `${i * 0.07}s`;
    bar.style.height = `${30 + Math.random() * 50}px`;
    waveform.appendChild(bar);
  }
};

const cycleModes = () => {
  const indicator = q('#mode-indicator');
  if (!indicator) return;
  const modes = ['Manual', 'Auto', 'Monitor'];
  let idx = 0;
  setInterval(() => {
    indicator.textContent = modes[idx % modes.length];
    idx += 1;
  }, 3500);
};

const bindAuthTabs = () => {
  const tabs = qa('.auth-tabs button');
  const forms = qa('.auth-form');
  if (!tabs.length) return;

  tabs.forEach((tab) => {
    tab.addEventListener('click', () => {
      tabs.forEach((btn) => btn.classList.remove('active'));
      tab.classList.add('active');
      const target = tab.dataset.authPanel;
      forms.forEach((form) => {
        form.classList.toggle('active', form.dataset.panel === target);
      });
    });
  });
};

const initReveal = () => {
  const revealables = qa('.hero, .section');
  revealables.forEach((el) => el.classList.add('reveal'));

  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add('in-view');
          observer.unobserve(entry.target);
        }
      });
    },
    { threshold: 0.2 }
  );

  revealables.forEach((el) => observer.observe(el));
};

const initGlitchPing = () => {
  const title = q('.glitch-title');
  if (!title) return;
  setInterval(() => {
    title.classList.add('flash');
    setTimeout(() => title.classList.remove('flash'), 250);
  }, 5000);
};

const updateYear = () => {
  const yearEl = q('#year');
  if (yearEl) yearEl.textContent = new Date().getFullYear();
};

document.addEventListener('DOMContentLoaded', () => {
  bindNavigation();
  hydrateWaveform();
  animateTimeline();
  cycleModes();
  bindAuthTabs();
  initReveal();
  initGlitchPing();
  updateYear();
});
