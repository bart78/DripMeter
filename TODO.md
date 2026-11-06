# DripMeter — Product & Engineering Roadmap (To-Do)

This document captures the design and implementation plan for the next three stages of **DripMeter**:
1. **Water Quantity & Droplet Volume Flow** (Calibration UI, per-drop $\mu\text{L}$ logging, and rolling volume trends)
2. **Cloud Portal & AI Pattern Analysis** (Firebase backend, remote tuning & scope, and Gemini diagnostic engine)
3. **Zero-Code Wi-Fi & Cloud Onboarding** (On-screen QR code + captive portal setup)

---

## 1. Water Quantity & Volume Flow (On-Device + Web UI + CSV)

### A. Physics & Calibration Model
Because droplets fall from a fixed ceiling height onto the same catchment surface, impact velocity is constant and the acoustic impulse (`EnvPeak`, 14 ms AC-RMS integral) scales with droplet mass/volume ($1\text{ g} = 1\text{ mL} = 1000\text{ }\mu\text{L}$).
* **Primary formula:**
  $$\text{Volume}_i\text{ (µL)} = \text{round}\!\left(\text{EnvPeak}_i \times k_{\text{env}}\right)$$
  where $k_{\text{env}}$ is the calibrated microliters per `EnvPeak` unit (default `~1.60 µL/unit`, corresponding to a standard `~50 µL` pendant water drop at `EnvPeak ≈ 31`).

### B. Web UI: Collapsible "Water Quantity & Calibration" Drawer (`src/web_export.cpp`)
Add a second `<details class="tuning-drawer" id="quantity-drawer">` section on `/` containing:
- [ ] **"Measure quantity" Enable Checkbox (`q_en`):**
  - Toggles volume calculation, KPI display, and CSV volume logging on/off (persisted in NVS).
- [ ] **Guided 2-Step Calibration Workflow (with clear plain-English instructions):**
  1. **Timed / Windowed Collection (Recommended):**
     - Click **"Start Calibration Counter"** (records `calStartEpoch`, live `calDrops` count, and cumulative $\sum \text{EnvPeak}_i$ while you collect water in a small cup or weigh the container on a $0.01\text{ g}$ scale).
     - Enter the measured water quantity in **`mL` (or `grams`)** into the calibration input box and click **"Apply Calibration"**.
     - Computes $k_{\text{env}} = \frac{V_{\text{mL}} \times 1000}{\sum \text{EnvPeak}_i}$ and displays the resulting **Average Drop Size** (e.g., `51.4 µL/drop · 19.5 drops/mL`).
  2. **Manual / Direct Override:**
     - Allow directly entering an **Average Drop Volume (`mL` or `µL`)** (default `0.05 mL` / `50 µL`) if enabling quantity tracking before running a weighed collection.

### C. Rolling Volume Metrics & KPIs (`src/histogram.*`, `src/web_export.*`, `src/dashboard.*`)
Maintain parallel 15-minute microliter buckets (`_volTodayUl[96]`, `_volYdayUl[96]`) and a 7-day daily volume history reconstructed from `/drops.csv` on boot:
- [ ] **Last Hour (`mL`):** Rolling sum of the last 60 minutes (e.g., `3.8 mL`).
- [ ] **Last 24 Hours (`mL`):** Rolling sum across the last 96 15-minute buckets spanning today + yesterday (e.g., `91.4 mL`).
- [ ] **24-Hour Rolling Average (`mL/day`) & Day-over-Day Trend Badge:**
  - Compare the last 24h volume against the prior 24h / multi-day rolling average (e.g., `Avg 88.0 mL/day · ▲ +3.9% vs prev 24h` or `▼ -14% slowing down`) so you can immediately see if the leak is growing or shrinking day by day.

### D. CSV Logging (`src/drop_log.*`)
- [ ] Update `/drops.csv` to include `Volume_uL` (`ISO_Timestamp,Epoch,PeakADC,EnvPeak,Volume_uL`).
- [ ] Record calibration state in `#cfg,vN,...` markers (`q_en=1,ul_env=1.62`).

---

## 2. Cloud Portal & AI Pattern Analysis (Firebase)

Eliminates local LAN / mobile browser access friction by mirroring the full dashboard, controls, and AI diagnostics in a cloud web app (Next.js App Router + Firebase App Hosting + Firestore + Firebase AI Logic).

### A. Lightweight Device-to-Cloud Sync Protocol (`Core 0`)
Instead of holding a memory-heavy persistent WebSocket open on the ESP32-S3:
- [ ] **Event Batch Push (`POST /api/device/sync`):**
  - Triggered on Core 0 at the end of each drop burst (when detector state returns to `ARMED` after new drops) or once every 60 seconds.
  - Sends batched `{epoch, peak, envPeak, volume_uL}` events + current `/status.json` metrics.
- [ ] **Bi-Directional Knob & Calibration Sync:**
  - The cloud sync response returns any updated tuning parameters (`margin`, `noisemult`, `captwin`, `release`, `refract`) or quantity calibration settings changed in the Cloud Portal, which the ESP32 applies to `DropSampler` and NVS (`Tuning::save`).
- [ ] **On-Demand Live Signal Scope Relay:**
  - When a user opens the **Live Signal Scope** drawer in the Cloud Portal, the server sets a `streamRequestedUntil` timestamp (`now + 60s`) in the sync response.
  - Only while `now < streamRequestedUntil`, Core 0 pushes the compact 3 KB `/stream.json` (512 peak-held envelope points + state flags) every 1.5–2 seconds—giving the cloud UI a live scope with draggable threshold and knob overlays while using zero extra bandwidth when closed.

### B. Cloud Web Portal UI (Next.js + Firestore)
- [ ] **Full Feature Parity with On-Device UI:**
  - Live KPI cards (Drops, Last Drop, Impact Peak, Envelope/Threshold, plus Last Hour / Last 24h / 24h Rolling Average Volume).
  - Interactive 24-hour and multi-day histograms (toggle between **Drop Count** and **Water Volume `mL`**).
  - Collapsible **Live Signal Scope & Detection Tuning** drawer and **Water Quantity & Calibration** drawer.
  - One-click CSV export and WAV recording request.

### C. AI Pattern & Root-Cause Analyzer (Firebase AI Logic / Gemini)
Pre-compute statistical features from the raw drop timestamp & amplitude series (inter-drop $\Delta t_i$ distribution, burst duration/periodicity autocorrelation, within-burst relaxation curve fit, droplet volume distribution, and optional local weather/rainfall API lookup) and feed them into Gemini to generate structured diagnostics:
- [ ] **1. Overall Assessment & Trajectory:**
  - Multi-day summary of whether the leak is steady, accelerating, or resolving, with estimated weekly/monthly volume.
- [ ] **2. Last 24 Hours Summary:**
  - Total drops and `mL`, number of discrete bursts vs. isolated drips, peak activity window, and comparison to the 7-day baseline.
- [ ] **3. Hour-by-Hour Breakdown:**
  - Pattern shifts across the day (e.g., whether burst intervals shorten during morning/evening hot-water or HVAC usage, or stay clockwork 24/7).
- [ ] **4. Likely Physical Causes & Verification Steps:**
  - Classify the hydrodynamic signature against known leak archetypes:
    - **Periodic Burst + Exponential Relaxation ($\Delta t_1 < \Delta t_2 < \dots$):** Fill-and-siphon cavity, HVAC condensate pan/pump cycle, boiler/water-heater thermal expansion relief, or trap overflow.
    - **Constant $\Delta t$ Cadence:** Pressurized supply-pipe pinhole or weeping fitting.
    - **Time-of-Day Clustered Bursts:** Upstairs shower pan, drain trap, dishwasher, or washing machine discharge.
    - **Weather-Correlated Onset:** Roof, flashing, or exterior facade intrusion.
  - Suggest concrete 15-minute isolation tests to confirm the source.

---

## 3. Wi-Fi & Cloud Onboarding Flow (On-Screen QR Code + Captive Portal)

Removes hardcoded Wi-Fi credentials from `src/config.h` and provides a consumer-grade setup experience.

### A. NVS Credential Storage & Fallback Trigger
- [ ] Store Wi-Fi `ssid`, `password`, and optional `cloud_api_key` / `device_id` in NVS (`Preferences` namespace `"net"`).
- [ ] Enter **Onboarding Mode** automatically if:
  - No Wi-Fi credentials are stored in NVS, or
  - STA connection fails for >20 seconds on boot, or
  - User triggers "Reset Wi-Fi" from the UI.

### B. On-Screen QR Code on the 320×480 Display (`src/dashboard.cpp`)
- [ ] When in Onboarding Mode, render a high-contrast QR code card on the TFT display:
  - **QR Code:** Standard Wi-Fi auto-join payload (`WIFI:S:DripMeter-Setup;T:nopass;;`) so pointing an Android or iPhone camera at the screen connects to the `DripMeter-Setup` hotspot with one tap.
  - Display clear fallback text below the QR code: `1. Join "DripMeter-Setup" Wi-Fi  2. Open http://192.168.4.1`.

### C. Captive Portal Web Setup (`DNSServer` + `WebExport`)
- [ ] Run Arduino ESP32's built-in `DNSServer` on port 53 in Onboarding Mode so phones automatically pop up the captive portal window upon connecting to `DripMeter-Setup`.
- [ ] Serve a clean, mobile-first `/setup` page:
  - Scans and lists nearby 2.4 GHz Wi-Fi networks with signal-strength bars (`WiFi.scanNetworks()`).
  - Lets the user tap their network, enter the password, and optionally enter/scan a Cloud Portal pairing code.
  - Tests the connection while keeping the setup portal alive, displays the newly assigned LAN IP (`http://192.168.0.XX`) and Cloud Portal link, persists credentials to NVS, and switches to normal monitoring.
