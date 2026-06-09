/* login.js — login page behaviour */
(function () {
  'use strict';

  /* SVG paths: eye icon (visible / hidden) */
  const EYE_OPEN  = '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/>';
  const EYE_SLASH = '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94"/>'
                  + '<path d="M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19"/>'
                  + '<line x1="1" y1="1" x2="23" y2="23"/>';

  function initPasswordToggle() {
    const pwInput  = document.getElementById('password');
    const pwToggle = document.getElementById('pwToggle');
    const eyeIcon  = document.getElementById('eyeIcon');
    if (!pwInput || !pwToggle || !eyeIcon) return;

    pwToggle.addEventListener('click', () => {
      const isHidden = pwInput.type === 'password';
      pwInput.type   = isHidden ? 'text' : 'password';
      /* Replace inner SVG path only — safe, constant values */
      eyeIcon.innerHTML = isHidden ? EYE_SLASH : EYE_OPEN;
    });
  }

  function setLoading(on) {
    const btn = document.getElementById('loginBtn');
    if (!btn) return;
    btn.disabled    = on;
    btn.textContent = on ? 'Signing in…' : 'Log in';
  }

  function showError(msg) {
    const err = document.getElementById('loginError');
    if (!err) return;
    err.textContent = msg; /* textContent — XSS safe */
    err.classList.add('visible');
  }

  function hideError() {
    document.getElementById('loginError')?.classList.remove('visible');
  }

  async function doLogin() {
    const usernameEl = document.getElementById('username');
    const passwordEl = document.getElementById('password');
    if (!usernameEl || !passwordEl) return;

    const username = usernameEl.value.trim();
    const password = passwordEl.value;

    hideError();

    if (!username || !password) {
      showError('Please enter your username and password.');
      return;
    }

    setLoading(true);
    try {
      const res = await fetch('/api/login', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        /* Session cookie is set HttpOnly server-side — no client storage needed */
        body:    JSON.stringify({ username, password }),
      });

      if (res.ok) {
        window.location.href = 'dashboard.html';
      } else if (res.status === 401) {
        showError('Invalid username or password.');
      } else {
        showError(`Login failed (${res.status}). Please try again.`);
      }
    } catch {
      showError('Network error. Is the server running?');
    } finally {
      setLoading(false);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    initPasswordToggle();
    document.getElementById('loginBtn')?.addEventListener('click', doLogin);
    /* Submit on Enter — applies to the whole page */
    document.addEventListener('keydown', e => { if (e.key === 'Enter') doLogin(); });
  });
}());
