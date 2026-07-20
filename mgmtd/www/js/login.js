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

  function showChangeError(msg) {
    const err = document.getElementById('changeError');
    if (!err) return;
    err.textContent = msg; /* textContent — XSS safe */
    err.classList.add('visible');
  }

  /* The password the operator just authenticated with — sent as old_password when
     the server requires a forced change. Kept only in memory for this page. */
  let pendingOldPassword = '';

  /* Swap the login card for the forced password-change card. */
  function showChangeView() {
    document.getElementById('loginCard').style.display  = 'none';
    document.getElementById('changeCard').style.display = '';
    document.getElementById('newPassword')?.focus();
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
        /* Forced first-login change: stay on this page and show the change form. */
        const body = await res.json().catch(() => ({}));
        if (body.must_change) {
          pendingOldPassword = password;
          showChangeView();
        } else {
          window.location.href = 'home';
        }
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

  async function doChangePassword() {
    const newEl     = document.getElementById('newPassword');
    const confirmEl = document.getElementById('confirmPassword');
    if (!newEl || !confirmEl) return;

    const newPass = newEl.value;
    const confirm = confirmEl.value;

    document.getElementById('changeError')?.classList.remove('visible');

    if (!newPass) {
      showChangeError('Please enter a new password.');
      return;
    }
    if (newPass !== confirm) {
      showChangeError('Passwords do not match.');
      return;
    }
    if (newPass === pendingOldPassword) {
      showChangeError('Choose a password different from the default.');
      return;
    }

    const btn = document.getElementById('changeBtn');
    if (btn) { btn.disabled = true; btn.textContent = 'Saving…'; }
    try {
      const res = await fetch('/api/change-password', {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify({ old_password: pendingOldPassword, new_password: newPass }),
      });

      if (res.ok) {
        pendingOldPassword = '';
        window.location.href = 'home';
      } else if (res.status === 401) {
        showChangeError('Current password is incorrect. Please sign in again.');
      } else {
        showChangeError(`Could not set password (${res.status}). Please try again.`);
      }
    } catch {
      showChangeError('Network error. Is the server running?');
    } finally {
      if (btn) { btn.disabled = false; btn.textContent = 'Set password & continue'; }
    }
  }

  /* Reveal the "Sign in with Okta" button only when the server reports federated
     login is configured (GET /api/auth/sso/info → {enabled, method, label}).
     Clicking it hands the browser to the server, which 302-redirects to the IdP. */
  function initSso() {
    const section = document.getElementById('ssoSection');
    const btn     = document.getElementById('ssoBtn');
    if (!section || !btn) return;

    // Shown by default so the button is visible before the backend
    // /api/auth/sso/info route exists. Once that route is live, an explicit
    // {enabled:false} hides it again (and {label} overrides the button text).
    section.style.display = '';
    fetch('/api/auth/sso/info', { headers: { 'Accept': 'application/json' } })
      .then(r => (r.ok ? r.json() : null))
      .then(info => {
        if (!info) return;
        if (info.enabled === false) section.style.display = 'none';
        else if (info.label) btn.textContent = info.label;
      })
      .catch(() => { /* backend route not up yet — keep it visible for now */ });

    btn.addEventListener('click', () => {
      window.location.href = '/api/auth/sso/login';
    });
  }

  /* Surface an SSO failure passed back on the redirect: …/index.html?sso_error=… */
  function showSsoErrorFromUrl() {
    try {
      const e = new URLSearchParams(window.location.search).get('sso_error');
      if (e) showError('SSO sign-in failed: ' + e);
    } catch { /* ignore */ }
  }

  document.addEventListener('DOMContentLoaded', () => {
    initPasswordToggle();
    initSso();
    showSsoErrorFromUrl();
    document.getElementById('loginBtn')?.addEventListener('click', doLogin);
    document.getElementById('changeBtn')?.addEventListener('click', doChangePassword);
    /* Submit on Enter — routes to whichever card is visible. */
    document.addEventListener('keydown', e => {
      if (e.key !== 'Enter') return;
      const changing = document.getElementById('changeCard')?.style.display !== 'none';
      if (changing) doChangePassword(); else doLogin();
    });
  });
}());
