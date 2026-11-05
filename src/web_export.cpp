// ============================================================================
//  web_export.cpp — implementation
// ============================================================================
#include "web_export.h"
#include "time_service.h"
#include "tuning.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <stdio.h>
#include <atomic>

// Loop liveness counter maintained by main's loop() (diagnostic).
extern std::atomic<uint32_t> gLoopMs;

namespace
{
    const char *PAGE_STYLE =
        "<style>body{font-family:monospace;background:#0e1116;color:#eaf0f6;"
        "max-width:640px;margin:0 auto;padding:8px}a{color:#39c2ff}"
        "input,select{background:#161b22;color:#eaf0f6;border:1px solid #2c3644;"
        "padding:2px;margin:2px 0}b{color:#39c2ff}</style>";

    const char *detectModeName(uint8_t m)
    {
        switch (m)
        {
        case 1: return "match";
        case 2: return "both";
        case 3: return "either";
        default: return "envelope";
        }
    }

    const char *detectModeLabel(uint8_t m)
    {
        switch (m)
        {
        case 1: return "MATCH";
        case 2: return "BOTH";
        case 3: return "EITHER";
        default: return "ENVELOPE";
        }
    }
}

void WebExport::begin()
{
    if (_started)
        return;

    _server.on("/", HTTP_GET, [this]() { onRoot(); });
    _server.on("/cal", HTTP_GET, [this]() { onCal(); });
    _server.on("/drops.csv", HTTP_GET, [this]() { onCsv(); });
    _server.on("/status.json", HTTP_GET, [this]() { onStatus(); });
    _server.on("/set", HTTP_GET, [this]() { onSet(); });
    _server.on("/learn", HTTP_GET, [this]() { onLearn(); });
    _server.on("/learnnoise", HTTP_GET, [this]() { onLearnNoise(); });
    _server.on("/forget", HTTP_GET, [this]() { onForget(); });
    _server.on("/forgetnoise", HTTP_GET, [this]() { onForgetNoise(); });
    _server.on("/clearlog", HTTP_GET, [this]() { onClearLog(); });
    _server.on("/autocal", HTTP_GET, [this]() { onAutoCal(); });
    _server.on("/caldump", HTTP_GET, [this]() { onCalDump(); });
    _server.on("/tpl.bin", HTTP_POST, [this]() { onUploadTemplate(); });
    _server.on("/learnpick", HTTP_GET, [this]() { onLearnPick(); });
    _server.on("/learncancel", HTTP_GET, [this]() {
        _sampler.cancelLearn();
        _server.send(200, "application/json", "{\"ok\":1}");
    });
    _server.on("/noise.bin", HTTP_POST, [this]() { onUploadNoise(); });
    _server.on("/record", HTTP_GET, [this]() { onRecord(); });
    _server.on("/rec.wav", HTTP_GET, [this]() { onRecWav(); });
    _server.on("/bootlog.csv", HTTP_GET, [this]() {
        fs::File rd = LittleFS.open("/bootlog.csv", "r");
        if (!rd) { _server.send(404, "text/plain", "no boot log yet"); return; }
        _server.setContentLength(rd.size());
        _server.sendHeader("Content-Type", "text/csv");
        _server.sendHeader("Connection", "close");
        _server.send(200, "text/csv", "");
        uint8_t b2[1024];
        while (rd.available()) { size_t n = rd.read(b2, sizeof(b2)); _server.client().write(b2, n); }
        rd.close();
        _server.client().stop();
    });
    _server.on("/stream.json", HTTP_GET, [this]() { onStream(); });
    _server.onNotFound([this]() {
        _server.send(404, "text/plain",
                     "not found - try / /cal /drops.csv /status.json");
    });
    _server.begin();
    _started = true;

    // Run the server in its OWN task on CORE 0 (the WiFi/idle core): a
    // stalled/dead client connection can then never freeze drop detection
    // or the display, and bulk transfers (WAV saves/downloads) run at full
    // speed instead of competing with the priority-20 sampler on core 1.
    xTaskCreatePinnedToCore(
        taskEntry, "drip_web", 16384, this, 3, nullptr, 0);
    Serial.println("[WEB] http server on port 80 (core 0, dedicated task)");
}

void WebExport::taskEntry(void *arg)
{
    WebExport *w = static_cast<WebExport *>(arg);
    for (;;)
    {
        w->_server.handleClient();
        w->_server.client().setTimeout(3000);   // bound per-request stalls
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---------------------------------------------------------------------------
// Shared: server-side SVG bar chart of both days.
// ---------------------------------------------------------------------------
void WebExport::histogramSVG(String &out, uint8_t nowIdx)
{
    const int16_t W = 302, H = 120, BASE = 118;
    uint16_t mx = _histo.max();
    if (mx == 0)
        mx = 1;

    out.reserve(8192);
    out += "<svg viewBox='0 0 302 122' xmlns='http://www.w3.org/2000/svg' "
           "style='width:100%;max-width:640px;background:#12161d'>";

    char r[110];
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        const int16_t x = 1 + i * 3;
        const uint16_t cy = _histo.bucketYesterday(i);
        const uint16_t ct = _histo.bucket(i);
        if (cy)
        {
            int16_t h = (int16_t)((uint32_t)cy * 116 / mx);
            if (h < 1) h = 1;
            snprintf(r, sizeof(r),
                     "<rect x='%d' y='%d' width='2' height='%d' fill='#2e3a4c'/>",
                     x, BASE - h, h);
            out += r;
        }
        if (ct)
        {
            int16_t h = (int16_t)((uint32_t)ct * 116 / mx);
            if (h < 1) h = 1;
            snprintf(r, sizeof(r),
                     "<rect x='%d' y='%d' width='2' height='%d' fill='%s'/>",
                     x, BASE - h, h, (i == nowIdx) ? "#60d2ff" : "#2fa8e8");
            out += r;
        }
    }

    // "now" marker + hour ticks.
    const int16_t nx = 1 + nowIdx * 3 + 1;
    snprintf(r, sizeof(r),
             "<line x1='%d' y1='2' x2='%d' y2='118' stroke='#ff5252'/>", nx, nx);
    out += r;
    for (uint8_t h = 0; h <= 4; ++h)
    {
        snprintf(r, sizeof(r),
                 "<text x='%d' y='122' font-size='7' fill='#8794a5'>%02d</text>",
                 1 + h * 72, h * 6);
        out += r;
    }
    out += "</svg>";
}

// ---------------------------------------------------------------------------
// / — STATUS page: results view.
// ---------------------------------------------------------------------------
void WebExport::onRoot()
{
    struct tm tmv;
    const bool haveTm = TimeService::localTime(tmv);

    char timeStr[24] = "--:--:--";
    if (haveTm)
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    char lastIso[48] = "never";
    if (_sampler.lastDropEpochUs() != 0)
        if (!TimeService::formatISO(_sampler.lastDropEpochUs(), lastIso, sizeof(lastIso)))
            snprintf(lastIso, sizeof(lastIso), "(clock not synced)");

    uint8_t nowIdx = 0;
    if (haveTm)
        nowIdx = Histogram::indexFor(tmv);

    String svg;
    histogramSVG(svg, nowIdx);

    char head[1400];
    snprintf(head, sizeof(head),
        "<!DOCTYPE html><html><head><title>DripMeter</title>"
        "<meta http-equiv='refresh' content='5'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "%s</head><body>"
        "<h2>DripMeter <span style='font-size:14px;color:#8794a5'>%s %s</span></h2>"
        "<p>drops today: <b>%lu</b> | triggers: %lu | last: %s (peak %u)</p>"
        "<p>mode: %s | template: %s | wifi: %ddBm</p>",
        PAGE_STYLE,
        timeStr, TimeService::synced() ? "(NTP)" : "(no NTP)",
        (unsigned long)_histo.total(),
        (unsigned long)_sampler.triggerCount(),
        lastIso, (unsigned)_sampler.lastPeak(),
        detectModeLabel(_sampler.detectMode()),
        _sampler.templateLearned() ? "LEARNED" : "none",
        (int)WiFi.RSSI());

    String page;
    page.reserve(10000);
    page += head;
    page += "<p style='color:#8794a5'>solid = today, dim = yesterday</p>";
    page += svg;
    page +=
        "<p><a href='/cal'>calibration</a> · "
        "<a href='/drops.csv'>download drops.csv</a> · "
        "<a href='/status.json'>status.json</a> · "
        "<a href='/clearlog' onclick=\"return confirm('Wipe the entire drop log?')\">"
        "clear log</a></p>"
        "</body></html>";

    _server.send(200, "text/html", page);
}

// ---------------------------------------------------------------------------
// /cal — CALIBRATION page: live levels + tuning bench.
// ---------------------------------------------------------------------------
void WebExport::onCal()
{
    char tplInfo[48] = "";
    if (_sampler.templateLearned())
        snprintf(tplInfo, sizeof(tplInfo), " (avg %u, last peak %u)",
                 _sampler.templateStore().avgCount(),
                 _sampler.templateStore().lastCapturePeak());

    char html[12000];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>DripMeter - calibration</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "%s</head><body>"
        "<h2>Calibration <a style='font-size:13px' href='/cal'>[reload]</a></h2>"
        "<p><a href='/'>back to status</a></p>"

        // --- live chart ----------------------------------------------------
        "<canvas id='cv' width='640' height='180' "
        "style='width:100%%;max-width:660px;background:#12161d;touch-action:none'>"
        "</canvas>"
        "<p style='color:#8794a5;font-size:12px'>"
        "<b style='color:#ff5252'>red = live threshold</b> (drag to set) · "
        "<span style='color:#8899aa'>gray = baseline</span> · "
        "threshold = baseline + max(margin, noiseMult x noise) · "
        "<button id='pausebtn' onclick=\"paused=!paused;this.textContent=paused?'resume':'pause'\">"
        "pause</button> · <span id='calstat'>ready</span></p>"
        "<p style='margin:8px 0 2px'><b>scope</b> <span style='color:#8794a5'>"
        "- live signal; shows the captured candidate while recording</span></p>"
        "<canvas id='capcv' width='300' height='70' "
        "style='width:100%%;max-width:320px;background:#0d1117;"
        "border:1px solid #2c3644;touch-action:none'></canvas>"
        "<p style='color:#8794a5;font-size:12px;margin-top:2px' id='caplabel'>"
        "live signal (1s)</p>"
        "<p><button id='autocalbtn' onclick=\"calApplied=false;fetch('/autocal')\">"
        "auto-calibrate (10s quiet + drops)</button></p>"
        "<p><button onclick=\"var b=this;fetch('/record').then(r=>r.json())"
        ".then(j=>{b.nextSibling.textContent=j.ok?(' saved '+j.seconds+'s @ '"
        "+j.rate+'Hz'):(' failed: '+j.err);})\">save last 60s as WAV</button>"
        "<span style='color:#8794a5;font-size:12px'></span> · "
        "<a href='/rec.wav'>download rec.wav</a> "
        "<span style='color:#8794a5;font-size:12px'>"
        "(label the drops in Audacity, tell me the times)</span></p>"

        "<p>raw: <b><span id='raw'>%u</span></b> | "
        "env: <b><span id='env'>%u</span></b> | "
        "match: <b><span id='mat'>%u</span></b> | "
        "threshold: <b><span id='thr'>%u</span></b></p>"
        "<p>mode: %s | <span id='tplline'>template: %s%s</span> "
        "<button onclick=\"calApplied=false;fetch('/learn')\">record drop now</button> "
        "<button onclick=\"fetch('/forget').then(function(){location.reload();})\">forget</button> | noise: %s "
        "<button onclick=\"fetch('/learnnoise')\">record noise now</button> "
        "<button onclick=\"fetch('/forgetnoise').then(function(){location.reload();})\">forget</button> "
        "<button onclick=\"fetch('/learncancel')\">cancel</button></p>"

        "<h3>Knobs (auto-apply)</h3>"
        "<form id='f'>"
        "mode: <select name='mode'>"
        "<option value='0'%s>envelope</option>"
        "<option value='1'%s>match</option>"
        "<option value='2'%s>both (and)</option>"
        "<option value='3'%s>either (or)</option></select>"
        " <span style='color:#8794a5;font-size:11px'>both = loud AND matches "
        "the drop pattern - recommended for noisy rooms</span><br>"
        "margin = threshold floor (10-2000): "
        "<input type='number' name='margin' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>minimum distance of the "
        "trigger above the noise</span><br>"
        "noise mult (2-40): <input type='number' name='noisemult' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>adaptive: raises the "
        "trigger in noisy rooms</span><br>"
        "match thr (30-950): <input type='number' name='matchthr' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>correlation with the "
        "drop template required to fire</span><br>"
        "match gate rms (2-20): <input type='number' name='mrm' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>minimum signal level for "
        "a match - lower it to catch softer drops</span><br>"
        "refractory ms (30-5000): <input type='number' name='refract' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>minimum gap between "
        "events - lower it to catch rapid taps</span><br>"
        "release ms (50-10000): <input type='number' name='release' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>quiet time before "
        "re-arming - raise if one tap = many drips</span><br>"
        "capture ms (20-1000): <input type='number' name='captwin' value='%u'>"
        " <span style='color:#8794a5;font-size:11px'>how long to track the "
        "peak after a trigger</span><br>"
        "</form>"
        "<p><a href='/set?reset=1'>reset all to defaults</a></p>"
        "<p style='color:#8794a5'>reachable via hotspot too: join WiFi "
        "\"DripMeter\" and browse http://192.168.4.1</p>"

        "<script>"
        "var paused=false,thrY=0,ymax=64,calApplied=false;"
        "var lastData=[],lastBase=0,lastThr=0,lastMode='envelope',spss=16000;"
        "var cv=document.getElementById('cv'),ctx=cv.getContext('2d');"
        "function draw(data,thr,base,isMatch){var W=cv.width,H=cv.height,m=0,i;"
        "var srt=data.slice().sort(function(a,b){return a-b;});"
        "var p95=srt[Math.floor(srt.length*0.95)]||0;"
        "var tgt=Math.max(p95*1.3,isMatch?thr*1.4:thr*2,isMatch?200:8);"
        "ymax=Math.ceil(tgt/8)*8;"
        "ctx.fillStyle='#12161d';ctx.fillRect(0,0,W,H);"
        "ctx.strokeStyle='#232b36';ctx.beginPath();"
        "for(var q=1;q<4;q++){ctx.moveTo(0,H*q/4);ctx.lineTo(W,H*q/4);}ctx.stroke();"
        "function Y(v){return 20+(1-v/ymax)*(H-44);}"
        "if(!isMatch){ctx.strokeStyle='#8899aa';ctx.setLineDash([4,4]);ctx.beginPath();"
        "ctx.moveTo(0,Y(base));ctx.lineTo(W,Y(base));ctx.stroke();ctx.setLineDash([]);}"
        "ctx.beginPath();var n=data.length,x0=W-n*0.625;"
        "for(i=0;i<n;i++){var x=x0+i*0.625,y=Y(data[i]);"
        "if(i==0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}"
        "ctx.strokeStyle=isMatch?'#ffb02e':'#2fa8e8';ctx.lineWidth=1.4;ctx.stroke();"
        "ctx.lineTo(x0+(n-1)*0.625,H-20);ctx.lineTo(x0,H-20);ctx.closePath();"
        "ctx.fillStyle=isMatch?'rgba(255,176,46,0.12)':'rgba(47,168,232,0.15)';"
        "ctx.fill();ctx.lineWidth=1;"
        "thrY=Y(thr);ctx.strokeStyle='#ff5252';ctx.beginPath();"
        "ctx.moveTo(0,thrY);ctx.lineTo(W,thrY);ctx.stroke();"
        "ctx.fillStyle='#ff5252';ctx.font='11px monospace';"
        "ctx.fillText((isMatch?'MATCH ':'THR ')+thr,4,thrY-3);"
        "ctx.fillStyle='#8794a5';ctx.fillText('max '+ymax,4,14);"
        "ctx.fillText('10s',W-26,H-6);ctx.fillText('now',W-30,14);"
        "lastData=data;lastBase=base;lastThr=thr;}"
        "async function poll(){"
        "try{var j=await (await fetch('/stream.json')).json();"
        "document.getElementById('raw').textContent=j.raw;"
        "document.getElementById('env').textContent=j.env[j.env.length-1];"
        "document.getElementById('mat').textContent=j.match_score;"
        "document.getElementById('thr').textContent=j.thr;"
        "lastMode=j.mode;if(j.sps)spss=j.sps;"
        "var isMatch=(j.mode=='match'||j.mode=='both');"
        "if(!paused)draw(isMatch?j.match:j.env,isMatch?j.mthr:j.thr,j.base,isMatch);"
        "var s=document.getElementById('calstat');"
        "if(j.tpl)"
        "document.getElementById('tplline').textContent="
        "'template: LEARNED (avg '+j.tpl_avg+', last peak '+j.tpl_peak+')';"
        "else"
        "document.getElementById('tplline').textContent='template: none';"
        "if(j.cap&&j.cap.length){var c2=document.getElementById('capcv'),"
        "x2=c2.getContext('2d');var W2=c2.width,H2=c2.height,cm=1,i2;"
        "for(i2=0;i2<j.cap.length;i2++){var a2=Math.abs(j.cap[i2]);if(a2>cm)cm=a2;}"
        "x2.fillStyle='#0d1117';x2.fillRect(0,0,W2,H2);"
        "x2.strokeStyle='#ffb02e';x2.beginPath();"
        "for(i2=0;i2<j.cap.length;i2++){var yy=H2/2-(j.cap[i2]/cm)*(H2/2-4);"
        "var xx=i2/j.cap.length*W2;"
        "if(i2==0)x2.moveTo(xx,yy);else x2.lineTo(xx,yy);}"
        "x2.stroke();"
        "document.getElementById('caplabel').textContent="
        "'candidate: peak '+j.learn_peak+(j.learn_armed?' (recording...)':'');}"
        "if(j.learn_armed)s.textContent='RECORDING - make drops now; "
        "loudest candidate peak '+j.learn_peak;"
        "else if(j.cal_phase==1)s.textContent='listening to environment (10s)...';"
        "else if(j.cal_phase==2)s.textContent='MAKE DROPS NOW - drop '"
        "+(Math.min(j.cal_drops+1,4))+'/4';"
         "else if(j.cal_done&&!calApplied){"
         "fetch('/caldump');"
         "var p=j.cal_peaks.filter(function(v){return v>0;});"
         "if(p.length){var lo=Math.min.apply(null,p);"
         "var m=Math.max(10,Math.min(2000,Math.round(j.cal_noise+(lo-j.cal_noise)/2)));"
         "fetch('/set?margin='+m+'&ajax=1');calApplied=true;"
         "s.textContent='DONE: noise '+j.cal_noise+', drops '+p.join('/')"
         "+\" -> margin \"+m+\" (session saved as /cal_session.csv)\";}"
         "else{var m2=Math.max(150,Math.round(j.cal_noise*2));"
         "fetch('/set?margin='+m2+'&ajax=1');calApplied=true;"
         "s.textContent='no clean drops beat the noise - margin raised to '+m2"
         "+\"; noisy room? use MATCH mode. Session saved.\";}"
         "}"
         "}catch(e){}}"
        "setInterval(poll,700);poll();"
        // --- drag the threshold line ---------------------------------------
        "var dragging=false;"
        "function toVal(y){return Math.max(2,Math.round((1-(y-20)/(cv.height-44))*ymax));}"
        "var downX=0,downY=0,didDrag=false;"
        "cv.addEventListener('pointerdown',function(e){dragging=true;didDrag=false;"
        "downX=e.clientX;downY=e.clientY;cv.setPointerCapture(e.pointerId);});"
        "cv.addEventListener('pointermove',function(e){if(dragging&&"
        "(Math.abs(e.clientX-downX)>6||Math.abs(e.clientY-downY)>6))didDrag=true;});"
        "cv.addEventListener('pointermove',function(e){"
        "var r=cv.getBoundingClientRect();var y=(e.clientY-r.top)*(cv.height/r.height);"
        "if(dragging){thrY=y;var v=toVal(y);"
        "draw(lastData,v,lastBase,(lastMode=='match'||lastMode=='both'));"
        "ctx.fillStyle='#ff5252';ctx.font='11px monospace';"
        "ctx.fillText(((lastMode=='match'||lastMode=='both')?'MATCH ':'THR ')+v,4,thrY-3);}"
        "else if(lastData.length){var x=(e.clientX-r.left)*(cv.width/r.width);"
        "draw(lastData,lastThr,lastBase,lastMode=='match');"
        "ctx.strokeStyle='#556070';ctx.beginPath();ctx.moveTo(x,0);"
        "ctx.lineTo(x,cv.height);ctx.stroke();"
        "var i=Math.max(0,Math.min(lastData.length-1,Math.round((x-(cv.width-lastData.length*0.625))/0.625)));"
        "var ts=-(lastData.length-1-i)*0.08;"
        "ctx.fillStyle='#eaf0f6';ctx.font='10px monospace';"
        "ctx.fillText('t='+ts.toFixed(1)+'s v='+lastData[i],x+4,16);}"
        "});"
        "cv.addEventListener('pointerup',function(e){if(!dragging)return;dragging=false;"
        "var r=cv.getBoundingClientRect();"
        "if(didDrag){var y=(e.clientY-r.top)*(cv.height/r.height);var v=toVal(y);"
        "fetch('/set?'+(lastMode=='match'||lastMode=='both'?'matchthr':'margin')"
        "+'='+v+'&ajax=1');}"
        "else{var x=(e.clientX-r.left)*(cv.width/r.width);var n=lastData.length,"
        "x0=cv.width-n*0.625;var i2=Math.round((x-x0)/0.625);"
        "if(i2>=0&&i2<n){var perPt=256000/(spss||16000);"
        "var ago=Math.round((n-1-i2)*perPt);fetch('/learnpick?t='+ago);"
        "document.getElementById('calstat').textContent="
        "'picked @ '+(ago/1000).toFixed(1)+'s ago - learning...';}}});"
        // --- knobs auto-apply ----------------------------------------------
        "var t;"
        "function apply(){clearTimeout(t);t=setTimeout(function(){"
        "var q=new URLSearchParams(new FormData(document.getElementById('f')));"
        "q.append('ajax','1');fetch('/set?'+q.toString());},400);}"
        "document.querySelectorAll('#f input,#f select').forEach(function(e){"
        "e.addEventListener('input',apply);e.addEventListener('change',apply);});"
        "</script>"
        "</body></html>",
        PAGE_STYLE,
        (unsigned)_sampler.rawADC(), (unsigned)_sampler.envelope(),
        (unsigned)_sampler.matchScore(), (unsigned)_sampler.threshold(),
        detectModeLabel(_sampler.detectMode()),
        _sampler.templateLearned() ? "LEARNED" : "none",
        tplInfo,
        _sampler.noiseLearned() ? "LEARNED" : "none",
        _sampler.detectMode() == 0 ? " selected" : "",
        _sampler.detectMode() == 1 ? " selected" : "",
        _sampler.detectMode() == 2 ? " selected" : "",
        _sampler.detectMode() == 3 ? " selected" : "",
        (unsigned)_sampler.margin(), (unsigned)_sampler.noiseMult(),
        (unsigned)_sampler.matchThr(),
        (unsigned)_sampler.matchMinRms(),
        (unsigned)_sampler.refractoryMs(), (unsigned)_sampler.releaseMs(),
        (unsigned)_sampler.captureWindowMs());

    _server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// /drops.csv — stream the log.
// ---------------------------------------------------------------------------
void WebExport::onCsv()
{
    fs::File rd = LittleFS.open(LOG_PATH, "r");
    if (!rd)
    {
        _server.send(500, "text/plain", "log unavailable");
        return;
    }

    // Stream with a fixed content length: any rows appended mid-transfer
    // simply wait for the next download.
    _server.setContentLength(rd.size());
    _server.sendHeader("Content-Type", "text/csv");
    _server.sendHeader("Content-Disposition", "attachment; filename=\"drops.csv\"");
    _server.sendHeader("Connection", "close");
    _server.send(200, "text/csv", "");

    uint8_t buf[2048];
    while (rd.available())
    {
        const size_t n = rd.read(buf, sizeof(buf));
        _server.client().write(buf, n);
    }
    rd.close();
    _server.client().stop();
}

// ---------------------------------------------------------------------------
// /status.json — machine-readable snapshot (incl. both histogram arrays).
// ---------------------------------------------------------------------------
void WebExport::onStatus()
{
    struct tm tmv;
    const bool haveTm = TimeService::localTime(tmv);

    char timeStr[32] = "null";
    if (haveTm)
        snprintf(timeStr, sizeof(timeStr),
                 "\"%04d-%02d-%02dT%02d:%02d:%02d\"",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    char lastIso[48] = "null";
    if (_sampler.lastDropEpochUs() != 0)
    {
        char tmp[48];
        if (TimeService::formatISO(_sampler.lastDropEpochUs(), tmp, sizeof(tmp)))
            snprintf(lastIso, sizeof(lastIso), "\"%s\"", tmp);
    }

    char head[900];
    snprintf(head, sizeof(head),
        "{\"time\":%s,\"time_synced\":%s,"
        "\"drops_today\":%lu,\"triggers_total\":%lu,"
        "\"last_drop\":%s,\"last_drop_epoch\":%lld,\"last_peak\":%u,"
        "\"raw\":%u,\"envelope\":%u,\"match_score\":%u,\"threshold\":%u,"
        "\"sps\":%u,"
        "\"mode\":\"%s\",\"template\":%s,\"template_captures\":%u,"
        "\"noise_tpl\":%s,\"noise_captures\":%u,"
         "\"template_peak\":%u,\"match_thr\":%u,"
         "\"margin\":%u,\"noise_mult\":%u,"
         "\"refractory_ms\":%u,\"capture_ms\":%u,\"release_ms\":%u,"
         "\"wifi_up\":%s,\"rssi\":%d,\"ip\":\"%s\","
        "\"fs_free\":%u,\"fs_total\":%u,\"log_bytes\":%u,"
        "\"uptime_s\":%lu,\"loop_ms_ago\":%lu,"
        "\"histogram\":{\"bucket_minutes\":%d,\"today\":[",
         timeStr, TimeService::synced() ? "true" : "false",
         (unsigned long)_histo.total(),
         (unsigned long)_sampler.triggerCount(),
         lastIso, (long long)_sampler.lastDropEpochUs(),
         (unsigned)_sampler.lastPeak(),
         (unsigned)_sampler.rawADC(), (unsigned)_sampler.envelope(),
         (unsigned)_sampler.matchScore(), (unsigned)_sampler.threshold(),
         (unsigned)_sampler.samplesPerSecond(),
         detectModeName(_sampler.detectMode()),
         _sampler.templateLearned() ? "true" : "false",
         (unsigned)_sampler.templateStore().avgCount(),
         (unsigned)_sampler.templateStore().lastCapturePeak(),
         (unsigned)_sampler.matchThr(),
        (unsigned)_sampler.margin(), (unsigned)_sampler.noiseMult(),
        (unsigned)_sampler.refractoryMs(), (unsigned)_sampler.captureWindowMs(),
        (unsigned)_sampler.releaseMs(),
        WiFi.status() == WL_CONNECTED ? "true" : "false",
        (int)WiFi.RSSI(), WiFi.localIP().toString().c_str(),
        (unsigned)(_log.totalBytes() - _log.usedBytes()),
        (unsigned)_log.totalBytes(), (unsigned)_log.fileSize(),
        (unsigned long)(millis() / 1000),
        (unsigned long)(millis() - gLoopMs.load()),
        BUCKET_MINUTES);

    String js;
    js.reserve(4000);
    js += head;
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        if (i) js += ',';
        js += String(_histo.bucket(i));
    }
    js += "],\"yesterday\":[";
    for (uint8_t i = 0; i < Histogram::kBuckets; ++i)
    {
        if (i) js += ',';
        js += String(_histo.bucketYesterday(i));
    }
    js += "]}}";

    _server.send(200, "application/json", js);
}

// ---------------------------------------------------------------------------
// /learn — arm one-shot template recording.
// ---------------------------------------------------------------------------
void WebExport::onLearn()
{
    _sampler.armRecord();
    char html[700];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>learn</title>"
        "<meta http-equiv='refresh' content='10;url=/cal'></head>"
        "<body style='font-family:monospace;background:#0e1116;color:#eaf0f6'>"
        "<h2>Recording armed (30s)</h2><p>Make your drops. The loudest one of "
        "this window is kept and averaged into the template — press /learn "
        "again for more captures (watch the avg count on /cal).</p>"
        "</body></html>");
    _server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
void WebExport::onForget()
{
    _sampler.templateStore().forget();
    _server.sendHeader("Location", "/cal");
    _server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// /learnnoise — arm recording of the NOISE signature (e.g., HVAC/ambient).
// ---------------------------------------------------------------------------
void WebExport::onLearnNoise()
{
    _sampler.armNoiseRecord();
    char html[700];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>learn noise</title>"
        "<meta http-equiv='refresh' content='10;url=/cal'></head>"
        "<body style='font-family:monospace;background:#0e1116;color:#eaf0f6'>"
        "<h2>Noise recording armed (30s)</h2><p>Let the ambient noise play. "
        "The loudest candidate of this window is kept and averaged into the "
        "noise template - press again for more captures.</p>"
        "</body></html>");
    _server.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// /learnpick?t=<msAgo> — click-to-select: learn the window the user picked
// on the live chart into the currently targeted template.
// ---------------------------------------------------------------------------
void WebExport::onLearnPick()
{
    const long t = _server.arg("t").toInt();
    const bool ok = (t > 0) && _sampler.pickTemplate((uint32_t)t);
    char j[64];
    snprintf(j, sizeof(j), "{\"ok\":%d}", ok ? 1 : 0);
    _server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
void WebExport::onForgetNoise()
{
    _sampler.noiseStore().forget();
    _server.sendHeader("Location", "/cal");
    _server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// /clearlog — wipe the CSV + reset today's/yesterday's on-screen counts.
// ---------------------------------------------------------------------------
void WebExport::onClearLog()
{
    _sampler.discardEvents();   // drop pre-wipe events still in the ring
    _log.clearLog();
    struct tm tmv;
    if (TimeService::localTime(tmv))
        _histo.resetForDate(tmv);
    _server.sendHeader("Location", "/");
    _server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// /caldump — persist the calibration flight recorder:
//   /cal_session.csv : t_ms, phase, env, match   (one row per history point)
//   /cal_cap_N.bin   : raw int16 waveform of captured candidate N
// Returns JSON with the point/capture counts so callers can fetch them.
// ---------------------------------------------------------------------------
void WebExport::onCalDump()
{
    const uint32_t cnt = _sampler.envHistCount();
    const uint32_t rows = cnt < DropSampler::kEnvHist ? cnt : DropSampler::kEnvHist;
    const uint32_t first = cnt > DropSampler::kEnvHist ? cnt - DropSampler::kEnvHist : 0;

    fs::File f = LittleFS.open("/cal_session.csv", "w");
    if (f)
    {
        f.print("t_ms,phase,env,match\n");
        for (uint32_t i = 0; i < rows; ++i)
        {
            char row[48];
            snprintf(row, sizeof(row), "%lu,%u,%u,%u\n",
                     (unsigned long)((first + i) * 12),
                     _sampler.phaseHistAt(first + i),
                     _sampler.envHistAt(first + i),
                     _sampler.matchHistAt(first + i));
            f.print(row);
        }
        f.close();
    }

    const uint8_t caps = _sampler.calCapCount();
    for (uint8_t n = 0; n < caps; ++n)
    {
        char path[24];
        snprintf(path, sizeof(path), "/cal_cap_%u.bin", n);
        fs::File c = LittleFS.open(path, "w");
        if (c)
        {
            for (uint16_t i = 0; i < DropTemplate::kLen; ++i)
            {
                const int16_t v = _sampler.calCapAt(n, i);
                c.write((const uint8_t *)&v, 2);
            }
            c.close();
        }
    }

    char j[128];
    snprintf(j, sizeof(j),
             "{\"ok\":1,\"points\":%lu,\"captures\":%u,\"noise\":%u,"
             "\"drop_peaks\":[%u,%u,%u,%u]}",
             (unsigned long)rows, caps, _sampler.calNoiseCeil(),
             _sampler.calDropPeak(0), _sampler.calDropPeak(1),
             _sampler.calDropPeak(2), _sampler.calDropPeak(3));
    _server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// /autocal — start the auto-calibration session (quiet phase, then drops).
// ---------------------------------------------------------------------------
void WebExport::onAutoCal()
{
    _sampler.startAutoCal();
    _server.send(200, "application/json", "{\"ok\":1}");
}

// ---------------------------------------------------------------------------
// /record — save the rolling WAV ring to /rec.wav (single file, overwritten).
// ---------------------------------------------------------------------------
void WebExport::onRecord()
{
    if (!_sampler.wavOK())
    {
        _server.send(500, "application/json", "{\"ok\":0,\"err\":\"recorder disabled\"}");
        return;
    }
    const uint32_t fill = _sampler.wavFill();
    const uint32_t rate = _sampler.samplesPerSecond() ? _sampler.samplesPerSecond() : 16000;

    fs::File f = LittleFS.open("/rec.wav", "w");
    if (!f)
    {
        _server.send(500, "application/json", "{\"ok\":0,\"err\":\"fs write failed\"}");
        return;
    }

    // WAV header: PCM, mono, 16-bit (ESP32 is little-endian — matches WAV).
    uint8_t hdr[44] = {0};
    const uint32_t chunkSize = 36 + fill * 2;
    const uint16_t audioFmt = 1, ch = 1, blockAlign = 2, bits = 16;
    const uint32_t fmtSize = 16, byteRate = rate * 2, dataSize = fill * 2;
    memcpy(hdr + 0, "RIFF", 4);  memcpy(hdr + 4, &chunkSize, 4);
    memcpy(hdr + 8, "WAVE", 4);  memcpy(hdr + 12, "fmt ", 4);
    memcpy(hdr + 16, &fmtSize, 4); memcpy(hdr + 20, &audioFmt, 2);
    memcpy(hdr + 22, &ch, 2);    memcpy(hdr + 24, &rate, 4);
    memcpy(hdr + 28, &byteRate, 4); memcpy(hdr + 32, &blockAlign, 2);
    memcpy(hdr + 34, &bits, 2);  memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &dataSize, 4);
    f.write(hdr, 44);

    int16_t buf[4096];
    for (uint32_t i = 0; i < fill; i += 4096)
    {
        const uint16_t n = (fill - i) < 4096 ? (uint16_t)(fill - i) : 4096;
        for (uint16_t j = 0; j < n; ++j)
            buf[j] = _sampler.wavRingAt(i + j);
        f.write((const uint8_t *)buf, (size_t)n * 2);
    }
    f.close();

    char j[128];
    snprintf(j, sizeof(j), "{\"ok\":1,\"seconds\":%lu,\"bytes\":%lu,\"rate\":%lu}",
             (unsigned long)(fill / rate),
             (unsigned long)(fill * 2 + 44), (unsigned long)rate);
    _server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// /rec.wav — download the saved recording.
// ---------------------------------------------------------------------------
void WebExport::onRecWav()
{
    fs::File rd = LittleFS.open("/rec.wav", "r");
    if (!rd)
    {
        _server.send(404, "text/plain", "no recording yet - use /record first");
        return;
    }
    _server.setContentLength(rd.size());
    _server.sendHeader("Content-Type", "audio/wav");
    _server.sendHeader("Content-Disposition", "attachment; filename=\"drip.wav\"");
    _server.sendHeader("Connection", "close");
    _server.send(200, "audio/wav", "");

    uint8_t buf[2048];
    while (rd.available())
    {
        const size_t n = rd.read(buf, sizeof(buf));
        _server.client().write(buf, n);
    }
    rd.close();
    _server.client().stop();
}

// ---------------------------------------------------------------------------
// POST /tpl.bin — upload a drop template built offline (tools/build_template.py
// from a WAV + labels). The body is the raw template file; it is written to
// flash and loaded. Same for /noise.bin with the noise template.
// ---------------------------------------------------------------------------
static bool receiveTemplate(WebServer &srv, const char *path)
{
    uint8_t buf[2048];
    size_t total = 0;
    fs::File f = LittleFS.open(path, "w");
    if (!f)
        return false;
    while (srv.client().available())
    {
        const size_t n = srv.client().readBytes(buf, sizeof(buf));
        if (n == 0)
            break;
        f.write(buf, n);
        total += n;
    }
    f.close();
    return total >= 100;    // sanity: template file is ~1 KB
}

void WebExport::onUploadTemplate()
{
    const bool ok = receiveTemplate(_server, "/template.bin");
    _sampler.templateStore().load();
    char j[96];
    snprintf(j, sizeof(j), "{\"ok\":%d,\"learned\":%s}",
             ok ? 1 : 0,
             _sampler.templateLearned() ? "true" : "false");
    _server.send(200, "application/json", j);
}

void WebExport::onUploadNoise()
{
    const bool ok = receiveTemplate(_server, "/noise.bin");
    _sampler.noiseStore().load();
    char j[96];
    snprintf(j, sizeof(j), "{\"ok\":%d,\"learned\":%s}",
             ok ? 1 : 0,
             _sampler.noiseLearned() ? "true" : "false");
    _server.send(200, "application/json", j);
}

// ---------------------------------------------------------------------------
// /stream.json — live envelope history + calibration state for the chart.
// ---------------------------------------------------------------------------
void WebExport::onStream()
{
    String js;
    js.reserve(4096);
    js += "{\"env\":[";

    const uint32_t cnt = _sampler.envHistCount();
    for (uint16_t i = 0; i < DropSampler::kEnvHist; ++i)
    {
        if (i)
            js += ',';
        const int32_t idx = (int32_t)cnt - (int32_t)DropSampler::kEnvHist + i;
        js += String(idx >= 0 ? _sampler.envHistAt((uint32_t)idx) : 0);
    }
    js += "],\"match\":[";
    for (uint16_t i = 0; i < DropSampler::kEnvHist; ++i)
    {
        if (i)
            js += ',';
        const int32_t idx = (int32_t)cnt - (int32_t)DropSampler::kEnvHist + i;
        js += String(idx >= 0 ? _sampler.matchHistAt((uint32_t)idx) : 0);
    }
    js += "],\"rawbuf\":[";
    for (uint16_t i = 0; i < 512; ++i)
    {
        if (i) js += ',';
        js += String(_sampler.rawScopeAt(i));
    }
    js += "],\"raw\":";
    js += String(_sampler.rawADC());
    js += ",\"thr\":";
    js += String(_sampler.threshold());
    js += ",\"mthr\":";
    js += String(_sampler.matchThr());
    js += ",\"base\":";
    js += String(_sampler.envBase());
    js += ",\"sps\":";
    js += String(_sampler.samplesPerSecond());
    js += ",\"mode\":\"";
    js += detectModeName(_sampler.detectMode());
    js += "\",\"cal_phase\":";
    js += String(_sampler.calPhase());
    js += ",\"cal_drops\":";
    js += String(_sampler.calDropCount());
    js += ",\"cal_noise\":";
    js += String(_sampler.calNoiseCeil());
    js += ",\"cal_done\":";
    js += _sampler.calDone() ? "true" : "false";
    js += ",\"learn_armed\":";
    js += _sampler.recordArmed() ? "true" : "false";
    js += ",\"learn_peak\":";
    js += String(_sampler.learnBestPeak());
    js += ",\"cap\":[";
    for (uint16_t i = 0; i < DropTemplate::kLen; ++i)
    {
        if (i)
            js += ',';
        js += String(_sampler.learnCapAt(i));
    }
    js += "]";
    js += ",\"tpl\":";
    js += _sampler.templateLearned() ? "true" : "false";
    js += ",\"tpl_avg\":";
    js += String(_sampler.templateStore().avgCount());
    js += ",\"tpl_peak\":";
    js += String(_sampler.templateStore().lastCapturePeak());
    js += ",\"cal_peaks\":[";
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (i)
            js += ',';
        js += String(_sampler.calDropPeak(i));
    }
    js += "]}";

    _server.send(200, "application/json", js);
}

// ---------------------------------------------------------------------------
// /set — apply + persist calibration values.
// ---------------------------------------------------------------------------
void WebExport::onSet()
{
    if (_server.hasArg("reset"))
    {
        Tuning::reset(_sampler);
        _server.sendHeader("Location", "/cal");
        _server.send(302, "text/plain", "");
        return;
    }

    if (_server.hasArg("mode"))
        {
        const long md = _server.arg("mode").toInt();
        if (md >= 0 && md <= 3)
            _sampler.setDetectMode((uint8_t)md);
    }

    if (_server.hasArg("margin"))
    {
        const long m = _server.arg("margin").toInt();
        if (m >= 3 && m <= 2000)
            _sampler.setMargin((uint16_t)m);
    }
    if (_server.hasArg("noisemult"))
    {
        const long n = _server.arg("noisemult").toInt();
        if (n >= 2 && n <= 40)
            _sampler.setNoiseMult((uint8_t)n);
    }
    if (_server.hasArg("mrm"))
    {
        const long r = _server.arg("mrm").toInt();
        if (r >= 2 && r <= 20)
            _sampler.setMatchMinRms((uint8_t)r);
    }
    if (_server.hasArg("matchthr"))
    {
        const long t = _server.arg("matchthr").toInt();
        if (t >= 30 && t <= 950)
            _sampler.setMatchThr((uint16_t)t);
    }
    if (_server.hasArg("refract"))
    {
        const long r = _server.arg("refract").toInt();
        if (r >= 30 && r <= 5000)
            _sampler.setRefractoryMs((uint16_t)r);
    }
    if (_server.hasArg("captwin"))
    {
        const long c = _server.arg("captwin").toInt();
        if (c >= 20 && c <= 1000)
            _sampler.setCaptureWindowMs((uint16_t)c);
    }
    if (_server.hasArg("release"))
    {
        const long r = _server.arg("release").toInt();
        if (r >= 50 && r <= 10000)
            _sampler.setReleaseMs((uint16_t)r);
    }
    Tuning::save(_sampler);
    _log.appendSettings(_sampler.margin(), _sampler.noiseMult(),
                        _sampler.matchThr(), _sampler.matchMinRms(),
                        _sampler.refractoryMs(), _sampler.releaseMs(),
                        _sampler.captureWindowMs(), _sampler.detectMode());

    if (_server.hasArg("ajax"))
    {
        _server.send(200, "application/json", "{\"ok\":1}");
        return;
    }
    _server.sendHeader("Location", "/cal");
    _server.send(302, "text/plain", "");
}
