# Ceiling Leak Forensics & Analysis Document (`1402동 201호`)

> [!IMPORTANT]
> **Core Principle ("Sherlock Holmes Protocol"):** Separate **hard physical facts** from **hypotheses**. Use the multi-day time-series signature (cycle period $P(t)$, slowdown duration, and lagged weather cross-correlation) to systematically eliminate candidates before any destructive work.

---

## 1. Hard Established Facts

### A. Building & Pre-Metering Observations (`산운마을 14단지 경남아너스빌`, ~2009 Construction)
1. **Thermal Boundary Conditions (`201호` OFF vs. `301호` ON):**
   - **`201호` (Your Unit):** Floor heating is **completely OFF**—eliminating any thermal confounding from below.
   - **`301호` (Upstairs):** Occupied by an older Korean couple (`어르신 세대`)—floor heating (`난방`) is **definitely running** during this `-1°C..9°C` November cold snap.
2. **Short Water Shutoff Did Not Immediately Stop Drips:** Turning off water briefly prior to metering did not stop the ceiling drip—indicating the slab/substrate between the source and ceiling exit is **already saturated** and holds buffered water.
2. **Initial `누수방지 / 누수탐지 팀` Inspection:** Standard short-duration pressure/acoustic tests on `301호`'s heating (`난방`) and domestic supply (`급수/급탕`) lines did not reveal a gross rupture.
3. **Fixed Exit Geometry:** Drops fall from a fixed ceiling exit point (constant fall height and target impact location) into the living-room catch box, with a secondary slow channel on the wall (`Orangina` bottle). Electrical conduits (`CD관`) / junction box paths in the slab are a suspected transport route.

### B. Sensor Facts (`data/drops11-07.csv`, First Clean 23.4h Run)
1. **Continuous 24/7 Operation (Zero Overnight Stop):**
   - **1,233 drops** across **87 primary siphon cycles** in `23.37 hours` (`2026-11-06 20:25` → `2026-11-07 19:48`).
   - Deep Night (`00:00–07:00`) logged **28 full cycles** (`63.7 drops/hr`), definitively ruling out unbuffered sanitary drain/sewer discharge (`오·배수관`).
2. **Local Siphon Exit ("Water Clock" Relaxation Oscillator):**
   - Every burst starts with rapid full-head drops (`dt = 2.45s ± 0.38s`) and decays exponentially (`2.94s → 3.47s → 4.08s → ... → 33s → 65s`) across `~15–16 drops` (`~0.8 mL`), followed by `~13.2 min` of silence.
   - **Refill Memory (`r = +0.540` vs. `r = +0.011`):** Wait time *after* a burst correlates with how many drops just drained (`14.20 min` after 11–13 drops vs. `17.27 min` after 17–20 drops), while burst size has zero correlation with the wait time *before* it (`r = +0.011`). This proves the 16-minute schedule is created by a **passive capillary siphon pocket at the ceiling exit** filling to a fixed upper lip ($V_{\max}$) and breaking at $V_{\min}$.
3. **Systematic Diurnal Variance (`15.4 min` Night vs. `17.7 min` Late Morning + Real Physical Midday Dip):**
   - The cycle period is **fastest all night (`18:00–06:00`: `15.40–15.86 min` mean period, `~3.3 mL/hr`)**, slows down from morning through midday (`08:00–14:00`: `17.09–17.70 min` mean period), and drops sharply in both drop count and **physical drop size** during **`13:00–16:30` (`PeakADC` `156 → 70`, `EnvPeak` `22 → 13`, confirmed by ear to be audibly much smaller drops $\to$ `~0.5 mL/hr`, an ~85% volumetric reduction)** before rebounding to full strength at **`16:37–17:06` (`15.52 min` period, `PeakADC` `156`)**.

---

## 2. Physical System Model: Saturated Substrate + Phase Lag ($\Delta t$) + Exit Siphon

Because the substrate between `301호`'s floor (`층간소음 완충재` styrofoam + `기포콘크리트` foam concrete) and `201호`'s ceiling slab is **already saturated**, the system behaves like a **saturated hydraulic RC low-pass filter**:

$$\text{Upstream Inflow } Q_{\text{in}}(t) \;\xrightarrow{\text{Saturated Slab Buffer (Lag }\Delta t\text{, Time Constant }\tau\text{)}}\; \text{Local Head } h(t) \;\xrightarrow{\text{Capillary Siphon}}\; \text{Cycle Period } P(t) = \frac{\Delta V_{\text{siphon}}}{Q_{\text{exit}}(h(t))}$$

1. **Why Short Shutoffs Don't Stop Drips Instantly:** Stored head $h(t)$ in the saturated slab continues driving seepage during a brief shutoff.
2. **Why Upstream Changes Are Still Visible:** Because the path is saturated, an upstream pressure or flow change propagates as a **hydrostatic head change after a phase delay $\Delta t$ (`~1.5 to 3 hours`)**, stretching or compressing the cycle period $P(t)$ and shifting the duration of the slowdown window.
3. **Why Pure Unmodulated Cold Supply (`냉수`) Is Less Likely Than Heating (`난방`):** Unit cold water is PRV-regulated (`감압변`, `~2.8–3.0 bar`), and brief tap openings are smoothed out by the slab buffer. A regulated cold supply has no natural reason to run fast for 12 hours every night (`18:00–06:00`) and slow down for 6–8 hours every late morning/afternoon (`08:00–16:00`).

---

## 3. Multi-Day Discrimination Matrix (Separating the 3 Hypotheses)

A single 24-hour run shows one daytime slowdown; **7–14 days of logging across weekdays, two weekends, and November cooling weather** separates the three possible drivers of that variance:

| Hypothesis | Physical Mechanism | Multi-Day & Weather Signature | How to Isolate / Confirm |
| :--- | :--- | :--- | :--- |
| **#1 Fixed Building / Plant Schedule** | Automated apartment-wide schedule (e.g., district-heating secondary differential pressure / supply-temp setback schedule `지역난방 공급 스케줄` or booster pump program). | **Clockwork Daily Repeat:** The slowdown onset (`~08:00`), recovery (`~17:00`), and **duration (`~8–9h`)** stay locked to the same clock hours every day—insensitive to whether the afternoon is `16°C` sunny or `5°C` overcast, and identical on weekdays vs. weekends. | Compare daily slowdown onset & duration across 7 days; verify with `관리사무소 기전실` (mechanical room schedule). |
| **#2 Weather / Thermostat-Driven Heating (`난방`)** | Leak in `301호` heating coil (`엑셀관`) or under-sink manifold/actuator (`온수분배기 구동기/부속`) that weeps faster when heating circulates (`16:00–06:00`) and slows when daytime solar warmth satisfies the thermostat (`06:00–15:00`, plus `-2h` lag $\Delta t$). | **Weather-Modulated Duration ($T_{\text{out}}$ Correlation):** As November cools down, **colder/cloudy days will shorten or eliminate the midday dip** (longer heating duty → faster cycle period $P(t)$), while warm/sunny afternoons will lengthen the midday slowdown. | **1)** Cross-correlate $P(t)$ and daily dip duration against **KMA (`기상청` / `data.go.kr`)** hourly temperature & solar radiation.<br>**2)** **Afternoon Valve Test (`14:00–20:00`):** Shut off `301호` heating valves at `14:00` (at midday low head, before evening recharge); watch if the `18:00` rebound fails to occur and $P(t)$ stretches past `19–22 min`. |
| **#3 Human / Occupancy-Modulated Source** | Manual boiler/thermostat toggling by `301호`, hot-water (`온수`) thermal expansion/contraction weep, or bathroom/appliance recharge of the under-styrofoam pool. | **Weekday vs. Weekend Shift (Irregular Duration):** Slowdown timing and duration shift between workdays (`Mon–Fri`) and weekends (`Sat–Sun`), or stretch significantly (`> 20 min` period) when `301호` is away for half a day. | Easy to isolate passively from the **Weekdays vs. Weekend** baseline table and day-to-day variance over 1–2 weeks. |

---

## 4. KMA (`기상청` 운중동 `37.38, 127.07`) Weather Log & A–B–A Solar/Temperature Experiment

### A. Captured KMA Weather Timeline (`성남시 분당구 운중동`, Sunrise `07:03`, Sunset `17:28`)
- **Fri Nov 6 (`6일 금`):** Milder (`~4°C` warmer than Nov 7 evening; evening `~6°C`).
- **Sat Nov 7 (`7일 토`, Baseline Day 1):** **Clear & Sunny (`맑음`)**, Low **`-1°C`** → High **`9°C`** (`12:00–16:00`), cooling rapidly to **`2.1°C`** by `20:50` (`1°C` from `23:00` through `06:00` Sunday).
  - *Observed DripMeter response:* Fast overnight cycles (`15.4m`), morning slowdown (`17.7m` at `10–12h`), sharp midday dip during peak sun/warmth (`12:30–16:30`), and full recovery right as sun left the facade (`16:37–17:06`, sunset `17:28`).
- **Sun Nov 8 (`8일 일`, Natural "No-Sun" Control Day):**
  - `00:00–02:00`: Clear (`맑음`), **`1°C`**
  - `03:00–08:00`: **Overcast (`흐림`)**, **`1–2°C`**
  - `09:00–11:00`: **Overcast (`흐림`)**, **`4°C → 6°C`**
  - `12:00–17:00`: **Overcast (`흐림`)**, flat **`8°C`** (45–55% RH, **zero direct sunshine**)
  - `18:00–21:00`: **Light Rain (`약한비 ~1mm`)**, **`7°C → 6°C`**
  - `22:00–24:00`: **Overcast (`흐림`)**, **`6°C → 5°C`**
- **Mon Nov 9 (`9일 월`):**
  - `00:00–13:00`: **Overcast / Mostly Cloudy (`흐림/구름많음`)**, **`5°C`** (`00–08h`) rising to **`10°C`** (`13h`).
  - `14:00–24:00`: **Clears to Sunny (`맑음`) at `14:00`** (`10°C` at `14–16h`), dropping to **`3°C`** by midnight.
- **Tue Nov 10 (`10일 화`, Cold + Sunny Repeat):**
  - **Clear & Sunny (`맑음`) all day**, Low **`-1°C`** (`05–07h`: `1°C`) → High **`10°C`** (`12–16h`), cooling to **`4–5°C`** at night.
- **Wed Nov 11 – Sun Nov 15 Warming Trend:**
  - **Wed Nov 11:** `2°C / 13°C` (Cloudy AM, Sunny PM)
  - **Thu Nov 12:** `3°C / 15°C` (Sunny)
  - **Fri Nov 13:** `6°C / 15°C` (Sunny)
  - **Sat Nov 14:** `7°C / 16°C` (Cloudy AM, Rain PM)
  - **Sun Nov 15:** `11°C / 15°C` (Overcast, **+12°C warmer overnight low** than Sat Nov 7 / Tue Nov 10!).

### B. Hour-by-Hour KMA Temperature & Sky Matrix (`운중동 37.38, 127.07` — Also Saved in `data/kma_hourly.csv`)

| Hour | Sat Nov 7 (`7일 토`) | Sun Nov 8 (`8일 일`) | Mon Nov 9 (`9일 월`) | Tue Nov 10 (`10일 화`) |
| :---: | :--- | :--- | :--- | :--- |
| **00:00** | *(~4°C, Clear)* | **1°C** (맑음, RH 60%) | **5°C** (흐림, RH 90%) | **3°C** (맑음, RH 80%) |
| **01:00** | *(~3°C, Clear)* | **1°C** (맑음, RH 65%) | **5°C** (흐림, RH 95%) | **2°C** (맑음, RH 80%) |
| **02:00** | *(~2°C, Clear)* | **1°C** (맑음, RH 65%) | **5°C** (흐림, RH 95%) | **2°C** (맑음, RH 80%) |
| **03:00** | *(~1°C, Clear)* | **1°C** (흐림, RH 65%) | **5°C** (흐림, RH 95%) | **2°C** (맑음, RH 85%) |
| **04:00** | *(~0°C, Clear)* | **1°C** (흐림, RH 65%) | **5°C** (흐림, RH 95%) | **2°C** (맑음, RH 85%) |
| **05:00** | *(~-1°C, Clear)* | **1°C** (흐림, RH 70%) | **5°C** (흐림, RH 95%) | **1°C** (맑음, RH 85%) |
| **06:00** | *(~-1°C, Clear)* | **1°C** (흐림, RH 70%) | **5°C** (흐림, RH 95%) | **1°C** (맑음, RH 85%) |
| **07:00** | *(~0°C, Clear)* | **2°C** (흐림, RH 70%) | **5°C** (구름많음, RH 95%) | **1°C** (맑음, RH 85%) |
| **08:00** | *(~2°C, Clear)* | **2°C** (흐림, RH 65%) | **5°C** (구름많음, RH 90%) | **2°C** (맑음, RH 85%) |
| **09:00** | *(~4°C, Clear)* | **4°C** (흐림, RH 60%) | **6°C** (구름많음, RH 85%) | **4°C** (맑음, RH 75%) |
| **10:00** | *(~6°C, Clear)* | **5°C** (흐림, RH 50%) | **7°C** (흐림, RH 85%) | **6°C** (맑음, RH 65%) |
| **11:00** | *(~7°C, Clear)* | **6°C** (흐림, RH 50%) | **7°C** (구름많음, RH 80%) | **8°C** (맑음, RH 60%) |
| **12:00** | *(~8°C, Clear)* | **8°C** (흐림, RH 45%) | **8°C** (구름많음, RH 75%) | **10°C** (맑음, RH 55%) |
| **13:00** | *(~9°C, Clear)* | **8°C** (흐림, RH 45%) | **10°C** (흐림, RH 70%) | **10°C** (맑음, RH 50%) |
| **14:00** | *(~9°C, Clear)* | **8°C** (흐림, RH 45%) | **10°C** (맑음, RH 70%) | **10°C** (맑음, RH 45%) |
| **15:00** | *(~9°C, Clear)* | **8°C** (흐림, RH 45%) | **10°C** (맑음, RH 65%) | **10°C** (맑음, RH 40%) |
| **16:00** | *(~8°C, Clear)* | **8°C** (흐림, RH 50%) | **10°C** (맑음, RH 65%) | **10°C** (맑음, RH 45%) |
| **17:00** | *(~7°C, Clear)* | **8°C** (흐림, RH 55%) | **9°C** (맑음, RH 70%) | **8°C** (맑음, RH 55%) |
| **18:00** | *(~5°C, Clear)* | **7°C** (흐림, RH 65%) | **7°C** (맑음, RH 70%) | **6°C** (맑음, RH 65%) |
| **19:00** | *(~4°C, Clear)* | **6°C** (약한비 ~1mm, 70%) | **7°C** (맑음, RH 70%) | **5°C** (맑음, RH 70%) |
| **20:00** | **3.7°C** (맑음, RH 43%) | **6°C** (약한비 ~1mm, 75%) | **6°C** (맑음, RH 70%) | **5°C** (맑음, RH 70%) |
| **21:00** | **2.1°C** (맑음, RH 46%) | **6°C** (약한비 ~1mm, 85%) | **5°C** (맑음, RH 75%) | **5°C** (맑음, RH 75%) |
| **22:00** | **2.0°C** (맑음, RH 55%) | **6°C** (흐림, RH 90%) | **4°C** (맑음, RH 75%) | **4°C** (맑음, RH 75%) |
| **23:00** | **1.0°C** (맑음, RH 60%) | **5°C** (흐림, RH 90%) | **3°C** (맑음, RH 80%) | **4°C** (맑음, RH 80%) |

### C. Why Nov 7 → Nov 15 Is a Textbook Natural Experiment
1. **A–B–A Solar Test (`Sat Nov 7 Sunny` → `Sun Nov 8 Overcast` → `Tue Nov 10 Sunny`):**
   - Today (`Sat Nov 7`) and Tuesday (`Nov 10`) have almost identical temperatures (`-1°C` low, `9–10°C` high) and **bright afternoon sun (`맑음`)**.
   - Tomorrow (`Sun Nov 8`) has a similar high (`8°C`) but is **100% Overcast (`흐림`) all day**.
   - *Prediction:* If the `12:30–16:30` midday dip is driven by **direct solar heating** through the south/southwest windows satisfying `301호`'s living-room thermostat, the midday dip will **flatten/shrink tomorrow (Sunday)** and **return strongly on Tuesday (`Nov 10`)**.
2. **Cold-Night vs. Warm-Night Heating Test (`Sat/Sun Night 1°C` & `Tue Night 1°C` vs. `Fri/Sat/Sun Nov 13–15 Nights 6–11°C`):**
   - Tonight (`Nov 7→8`) and Monday night (`Nov 9→10`) drop to **`1°C`** overnight, whereas late next week (`Nov 13–15`) overnight lows stay mild at **`6–11°C`** with afternoon highs of **`15–16°C`**.
   - *Prediction:* If **Floor Heating (`난방`)** drives the leak, overnight cycle periods will be fastest (`~15.4 min`) on the `1°C` nights and noticeably slower (`> 17–19 min`) during the mild `11–16°C` stretch on Nov 13–15.
3. **Thermostat Threshold Estimation ($T_{\text{indoor}} \ge T_{\text{set}}$):**
   - Because a room thermostat acts as a **hard threshold switch** (`zone actuator closes when` $T_{\text{indoor}} \ge T_{\text{set}}$, `reopens when` $T_{\text{indoor}} \le T_{\text{set}} - \Delta T_{\text{hyst}}$), the midday throttling shouldn't be a smooth linear function of outdoor temperature—it should behave like a **step/threshold transition** once outdoor temperature $T_{\text{out}}(t)$ + solar gain pushes `301호` across $T_{\text{set}}$ (shifted by the slab's hydraulic lag $\Delta t$). Comparing the exact cutoff/re-onset hours across sunny (`9–15°C`) vs. overcast (`8–15°C`) days would even allow back-calculating `301호`'s approximate thermostat setpoint.

---

## 5. Why the First `누수탐지 팀` Missed It (Field Inspection Checklist for Next Visit)

When a leak averages `~2.9 mL/hr` (`0.048 mL/min` = `< 1 mL` in a 20-minute pressure test), standard static air-pressure gauges (`±0.05 bar` resolution) and acoustic stethoscopes (`청음기`) cannot see or hear it. When we have narrowed the pattern down, the targeted physical checks are:
1. **`301호` Under-Sink Heating Manifold (`싱크대 하부 온수분배기`):** Check motorized zone actuator stems (`구동기`), air vent (`에어핀`), and brass compression nuts for a slow weep running down the **unsealed floor pipe-sleeve cutout (`슬리브 틈새`)** into the under-styrofoam layer.
2. **15-Year-Old PB Fitting / Double-Sleeve (`에이콘 PB 부속 / 이중관`):** Hardened PB O-rings often **seal tight under `6–8 bar` high-pressure air tests** (`고압 불감지 누수`) and only weep at normal `2.5–3 bar` operating pressure or during thermal contraction.
3. **Under-Styrofoam Perched Water (`완충재 하부 고임수`) Entering `CD관`:** Blueprint check (`단위세대 전등·전열 평면도` + `난방/급수 평면도`) to map where `301호`'s pipe trenches cross `201호`'s ceiling `CD관` conduit run.
