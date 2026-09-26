/* Talks to the C backend purely through the global functions bound in src/ui/bridge.c
 * (window.getState/selectSd/...), each of which resolves with the wizard's full state -- so
 * every action here just re-renders from whatever state comes back, the same "redraw everything
 * every frame" model the old Nuklear screens used, just against the DOM instead of a pixel
 * framebuffer. */
(function () {
  "use strict";

  const el = (id) => document.getElementById(id);

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  const SD_ICON =
    '<svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path fill="none" ' +
    'stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" d="M7 3h8l4 4v13a1 1 0 ' +
    '0 1-1 1H6a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"/><path fill="none" stroke="currentColor" ' +
    'stroke-width="1.6" d="M9 3v5h6V3"/></svg>';

  const THEME_ICON =
    '<svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path fill="none" ' +
    'stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" ' +
    'd="M12 3a8.5 8.5 0 1 0 0 17c1 0 1.6-.7 1.6-1.5 0-.4-.15-.75-.4-1.05-.25-.3-.4-.65-.4-1.05 ' +
    '0-.83.67-1.4 1.5-1.4H16a4 4 0 0 0 4-4A8.5 8.5 0 0 0 12 3z"/>' +
    '<circle cx="7.5" cy="10.5" r="1" fill="currentColor" stroke="none"/>' +
    '<circle cx="11" cy="7.5" r="1" fill="currentColor" stroke="none"/>' +
    '<circle cx="15" cy="8.5" r="1" fill="currentColor" stroke="none"/></svg>';

  function renderSdPick(state) {
    const foundBlock = el("found-cards-block");
    const foundList = el("found-cards-list");
    const mountsBlock = el("all-mounts-block");
    const mountsList = el("all-mounts-list");
    const noneBlock = el("no-drives-block");

    foundList.innerHTML = "";
    mountsList.innerHTML = "";

    if (state.foundCards.length > 0) {
      foundBlock.hidden = false;
      mountsBlock.hidden = true;
      noneBlock.hidden = true;
      state.foundCards.forEach((path) => foundList.appendChild(makePickItem(path, SD_ICON)));
    } else if (state.allMounts.length > 0) {
      foundBlock.hidden = true;
      mountsBlock.hidden = false;
      noneBlock.hidden = true;
      state.allMounts.forEach((path) => mountsList.appendChild(makePickItem(path, SD_ICON)));
    } else {
      foundBlock.hidden = true;
      mountsBlock.hidden = true;
      noneBlock.hidden = false;
    }

    const errorEl = el("sd-pick-error");
    if (state.sdPickError) {
      errorEl.hidden = false;
      errorEl.textContent = state.sdPickError;
    } else {
      errorEl.hidden = true;
    }
  }

  function makePickItem(path, iconSvg) {
    const li = document.createElement("li");
    li.innerHTML =
      '<button type="button" class="pick-item">' + iconSvg +
      '<span class="path">' + escapeHtml(path) + "</span></button>";
    li.querySelector("button").addEventListener("click", () => {
      call(window.selectSd(path));
    });
    return li;
  }

  function renderAlreadyInstalled(state) {
    el("chosen-sd-label").textContent = state.chosenSd;
  }

  function renderThemePick(state) {
    const list = el("theme-list");
    const noThemesHint = el("no-themes-hint");
    list.innerHTML = "";

    if (state.themes.length === 0) {
      noThemesHint.hidden = false;
      noThemesHint.textContent = "No theme found in " + state.appDir;
      return;
    }
    noThemesHint.hidden = true;

    state.themes.forEach((theme) => {
      const li = document.createElement("li");
      const selected = theme.index === state.selectedTheme;
      li.innerHTML =
        '<button type="button" class="pick-item' + (selected ? " selected" : "") + '">' +
        THEME_ICON + '<span class="name">' + escapeHtml(theme.name) + "</span></button>";
      li.querySelector("button").addEventListener("click", () => {
        call(window.selectTheme(theme.index));
      });
      list.appendChild(li);
    });
  }

  function renderScrapeLogin(state) {
    const scrape = state.scrape;
    el("scrape-login-user").value = scrape.savedUser || "";
    el("scrape-login-pass").value = "";
    el("scrape-login-saved-hint").hidden = !scrape.hasSavedPassword;
    el("scrape-login-forget-btn").hidden = !(scrape.savedUser || scrape.hasSavedPassword);

    const errorEl = el("scrape-login-error");
    if (scrape.loginError) {
      errorEl.hidden = false;
      errorEl.textContent = scrape.loginError;
    } else {
      errorEl.hidden = true;
    }

    const continueBtn = el("scrape-login-continue-btn");
    continueBtn.disabled = false;
    continueBtn.textContent = "Sign in and continue";
  }

  const CHECK_ICON =
    '<svg class="icon" viewBox="0 0 24 24" aria-hidden="true"><path fill="none" ' +
    'stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" ' +
    'd="M5 12.5l4.5 4.5L19 7"/></svg>';

  function renderScrapePreview(state) {
    const scrape = state.scrape;
    const list = el("scrape-preview-list");
    const emptyBlock = el("scrape-preview-empty-block");
    const startBtn = el("scrape-preview-start-btn");
    list.innerHTML = "";

    const toFetch = scrape.games.filter((g) => !g.hasLogoAlready).length;
    el("scrape-preview-summary").textContent =
      scrape.games.length + " game(s) found (" + scrape.blockedCount +
      " system file(s) ignored) -- " + toFetch + " without a logo yet.";

    emptyBlock.hidden = scrape.games.length > 0;
    startBtn.disabled = scrape.games.length === 0;
    el("scrape-preview-force-row").hidden = scrape.games.length === 0;
    if (scrape.games.length === 0) el("scrape-preview-force").checked = false;

    scrape.games.forEach((g) => {
      const li = document.createElement("li");
      let badge = "";
      if (g.hasLogoAlready) badge = '<span class="badge badge-success">has logo</span>';
      else if (g.duplicateCount > 1) badge = '<span class="badge">x' + g.duplicateCount + '</span>';
      li.innerHTML =
        '<div class="pick-item static">' + THEME_ICON +
        '<span class="name">' + escapeHtml(g.romName) + "</span>" + badge + "</div>";
      list.appendChild(li);
    });
  }

  function renderScrapeScanning(state) {
    const scrape = state.scrape;
    const pct = scrape.progressTotal > 0 ? (scrape.progressDone / scrape.progressTotal) * 100 : 0;
    el("scrape-scan-progress-fill").style.width = pct + "%";
    el("scrape-scan-progress-label").textContent =
      scrape.progressDone + " / " + scrape.progressTotal + " (" + Math.round(pct) + "%) -- " +
      (scrape.currentName || "scanning...");
  }

  function formatBytes(n) {
    if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB";
    if (n >= 1024) return (n / 1024).toFixed(0) + " KB";
    return n + " B";
  }

  function formatSpeed(bytesPerSec) {
    return bytesPerSec > 0 ? formatBytes(bytesPerSec) + "/s" : "";
  }

  function renderScrapeProgress(state) {
    const scrape = state.scrape;
    const pct = scrape.progressTotal > 0 ? (scrape.progressDone / scrape.progressTotal) * 100 : 0;
    el("scrape-progress-fill").style.width = pct + "%";
    el("scrape-progress-label").textContent =
      scrape.progressDone + " / " + scrape.progressTotal + " (" + Math.round(pct) + "%) -- " +
      (scrape.currentName || "...");
    el("scrape-progress-detail").textContent = scrape.currentDetail || "";

    /* Byte-level progress for the ONE game currently downloading -- reset to (0, 0, 0) by the
     * backend right as each new game starts, see scrape_run()'s on_byte_progress. */
    const itemFill = el("scrape-item-progress-fill");
    const itemLabel = el("scrape-item-progress-label");
    const speed = formatSpeed(scrape.itemSpeedBps);

    if (scrape.itemBytesTotal > 0) {
      itemFill.classList.remove("indeterminate");
      const itemPct = (scrape.itemBytesDone / scrape.itemBytesTotal) * 100;
      itemFill.style.width = itemPct + "%";
      itemLabel.textContent =
        formatBytes(scrape.itemBytesDone) + " / " + formatBytes(scrape.itemBytesTotal) +
        " (" + Math.round(itemPct) + "%)" + (speed ? " -- " + speed : "");
    } else if (scrape.itemBytesDone > 0) {
      /* Server never sent a Content-Length (e.g. chunked transfer) -- percentage is meaningless,
       * so show a moving-stripes bar instead of a fill amount. */
      itemFill.classList.add("indeterminate");
      itemFill.style.width = "100%";
      itemLabel.textContent = formatBytes(scrape.itemBytesDone) + " downloaded" + (speed ? " -- " + speed : "");
    } else {
      itemFill.classList.remove("indeterminate");
      itemFill.style.width = "0%";
      itemLabel.textContent = "";
    }
  }

  /* These three steps all sit on a background job (see wizard.c's SCRAPE_JOB_*) that the backend
   * only advances past once wizard_state_json() notices it finished -- so the frontend has to
   * keep polling getState() on an interval while showing any of them, the same way scrape_progress
   * always has. */
  const BG_JOB_STEPS = ["scrape_login_checking", "scrape_scanning", "scrape_progress"];
  let bgJobTimer = null;

  function startBgJobPolling() {
    if (!bgJobTimer) bgJobTimer = setInterval(() => call(window.getState()), 400);
  }

  function stopBgJobPolling() {
    if (bgJobTimer) {
      clearInterval(bgJobTimer);
      bgJobTimer = null;
    }
  }

  function renderScrapeDone(state) {
    const s = state.scrape.stats;
    const list = el("scrape-stats-list");
    const rows = [
      ["Logos downloaded", s.downloaded],
      ["Already had a logo", s.alreadyHadLogo],
      ["Not found on ScreenScraper", s.notFound],
      ["No logo available", s.noArt],
      ["Failed", s.failed],
    ];
    list.innerHTML = rows
      .map(([label, value]) => "<li><span>" + label + "</span><span>" + value + "</span></li>")
      .join("");

    const errorEl = el("scrape-run-error");
    if (state.scrape.runError) {
      errorEl.hidden = false;
      errorEl.textContent = state.scrape.runError;
    } else {
      errorEl.hidden = true;
    }
  }

  function renderDone(state) {
    const logEl = el("done-log");
    if (state.log) {
      logEl.hidden = false;
      logEl.textContent = state.log;
    } else {
      logEl.hidden = true;
    }

    const ejectEl = el("done-eject-hint");
    if (state.ejectHint) {
      ejectEl.hidden = false;
      ejectEl.textContent = "Before removing the card: " + state.ejectHint;
    } else {
      ejectEl.hidden = true;
    }
  }

  function render(state) {
    document.querySelectorAll(".screen").forEach((section) => {
      section.classList.toggle("active", section.dataset.screen === state.step);
    });

    if (BG_JOB_STEPS.includes(state.step)) startBgJobPolling();
    else stopBgJobPolling();

    if (state.step === "sd_pick") renderSdPick(state);
    else if (state.step === "already_installed") renderAlreadyInstalled(state);
    else if (state.step === "theme_pick") renderThemePick(state);
    else if (state.step === "scrape_login") renderScrapeLogin(state);
    else if (state.step === "scrape_scanning") renderScrapeScanning(state);
    else if (state.step === "scrape_preview") renderScrapePreview(state);
    else if (state.step === "scrape_progress") renderScrapeProgress(state);
    else if (state.step === "scrape_done") renderScrapeDone(state);
    else if (state.step === "done") renderDone(state);
  }

  /* Every bound backend function returns a promise that resolves with the new state -- this just
   * feeds that straight back into render(), and surfaces backend/bridge errors instead of
   * silently doing nothing if a binding rejects. */
  function call(promise) {
    promise.then(render).catch((err) => console.error("Injector backend call failed:", err));
  }

  document.addEventListener("DOMContentLoaded", () => {
    el("use-path-btn").addEventListener("click", () => {
      call(window.pickManualSd(el("manual-sd-input").value));
    });
    el("manual-sd-input").addEventListener("keydown", (e) => {
      if (e.key === "Enter") el("use-path-btn").click();
    });
    el("rescan-btn").addEventListener("click", () => call(window.rescanSd()));

    el("already-yes-btn").addEventListener("click", () => call(window.confirmAlreadyInstalled(true)));
    el("already-no-btn").addEventListener("click", () => call(window.confirmAlreadyInstalled(false)));
    el("back-to-sd-btn").addEventListener("click", () => call(window.goBackToSdPick()));

    el("theme-back-btn").addEventListener("click", () => call(window.goBackToAlreadyInstalled()));
    el("install-btn").addEventListener("click", () =>
      call(window.install(el("srldr-path-input").value.trim())));
    /* Doesn't change wizard state, so it bypasses call()/render() entirely -- just fills the
     * input with whatever path the native dialog returned (or leaves it alone if cancelled). */
    el("srldr-path-browse-btn").addEventListener("click", () => {
      window.pickSrldrFile().then((path) => {
        if (path) el("srldr-path-input").value = path;
      }).catch((err) => console.error("Injector backend call failed:", err));
    });

    el("scrape-ask-skip-btn").addEventListener("click", () => call(window.scrapeSkip()));
    el("scrape-ask-yes-btn").addEventListener("click", () => call(window.scrapeAskYes()));
    el("scrape-login-continue-btn").addEventListener("click", () => {
      call(window.scrapeLoginContinue(
        el("scrape-login-user").value.trim(),
        el("scrape-login-pass").value,
        el("scrape-login-remember").checked
      ));
    });
    el("scrape-login-signup-link").addEventListener("click", (e) => {
      e.preventDefault();
      call(window.openScreenScraperSignup());
    });
    el("scrape-login-skip-btn").addEventListener("click", () => call(window.scrapeLoginSkip()));
    el("scrape-login-forget-btn").addEventListener("click", () => call(window.scrapeLoginForget()));
    el("scrape-scan-cancel-btn").addEventListener("click", () => call(window.scrapeCancel()));
    el("scrape-preview-skip-btn").addEventListener("click", () => call(window.scrapeSkip()));
    el("scrape-preview-start-btn").addEventListener("click", () =>
      call(window.scrapeStart(el("scrape-preview-force").checked)));
    el("scrape-cancel-btn").addEventListener("click", () => call(window.scrapeCancel()));
    el("scrape-done-ok-btn").addEventListener("click", () => call(window.scrapeDoneOk()));

    el("restart-btn").addEventListener("click", () => call(window.restart()));
    /* Closes the window -- no wizard state left to render afterward, so this bypasses call(). */
    el("finish-btn").addEventListener("click", () => window.quit());

    call(window.getState());
  });
})();
