/*

  web page for ET Phone Boat, sent by SendWebsite() in ETPhoneBoat.ino

  same approach as the SuperMon.h example: the whole page (HTML + CSS + javascript) is stored
  in one big char array using R"=====( ... )=====" so it can be written naturally

  debugging tip: copy everything from <!DOCTYPE html> to </html> into a .html file and open it
  in a browser. The layout and controls work, there's just no ESP to talk to (the status pill
  will say "No link"). In desktop Safari/Chrome use responsive/device mode to see the phone layout

  how the page talks to the ESP (AJAX)
  1. rudder slider  -> PUT RUDDER?VALUE=-12         -> server.on("/RUDDER", UpdateRudder);
  2. sail buttons   -> PUT MAIN?DIR=IN / OUT / STOP -> server.on("/MAIN", UpdateMain);
                    -> PUT JIB?DIR=IN / OUT / STOP  -> server.on("/JIB", UpdateJib);
  3. every 200 ms   -> PUT json                     -> server.on("/json", SendJSON);
     the ESP replies with {"R":-12,"M":45,"J":60} and the page updates the gauges and boat picture

  the sail buttons are press-and-hold: while held, the command is re-sent every 200 ms so the ESP
  knows the finger is still down. Releasing sends STOP

*/

const char PAGE_MAIN[] PROGMEM = R"=====(

<!DOCTYPE html>
<html lang="en">

<head>
  <meta charset="utf-8">
  <!-- scale correctly on phones, and don't let a double tap zoom the page while driving -->
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
  <!-- lets the page run full screen when added to the iPhone home screen -->
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="RC Boat">
  <meta name="theme-color" content="#0b1220">
  <title>ET Phone Boat</title>

  <style>
    :root {
      --bg: #0b1220;
      --card: rgba(255,255,255,0.06);
      --card-border: rgba(255,255,255,0.10);
      --text: #e8eefc;
      --muted: #8a97b4;
      --accent: #38bdf8;
      --accent2: #6366f1;
      --port: #f43f5e;
      --stbd: #22c55e;
      --radius: 22px;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }

    html, body {
      height: 100%;
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", Roboto, sans-serif;
      /* no text selection, long-press menus or gray tap flashes, it should feel like an app */
      -webkit-user-select: none;
      user-select: none;
      -webkit-touch-callout: none;
      -webkit-tap-highlight-color: transparent;
      overscroll-behavior: none;
    }

    body {
      background:
        radial-gradient(120% 60% at 50% -10%, rgba(56,189,248,0.25), transparent 60%),
        radial-gradient(80% 50% at 100% 100%, rgba(99,102,241,0.18), transparent 60%),
        var(--bg);
      background-attachment: fixed;
    }

    .app {
      max-width: 480px;
      margin: 0 auto;
      padding: calc(env(safe-area-inset-top) + 14px) 16px calc(env(safe-area-inset-bottom) + 16px);
      display: flex;
      flex-direction: column;
      gap: 14px;
      min-height: 100%;
    }

    /* ---------- header ---------- */
    .top {
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .brand { font-size: 22px; font-weight: 700; letter-spacing: -0.02em; }
    .brand span {
      background: linear-gradient(90deg, var(--accent), var(--accent2));
      -webkit-background-clip: text;
      background-clip: text;
      color: transparent;
    }
    .sub { font-size: 12px; color: var(--muted); margin-top: 2px; }

    .pill {
      display: flex;
      align-items: center;
      gap: 7px;
      padding: 7px 12px;
      border-radius: 999px;
      font-size: 13px;
      font-weight: 600;
      background: var(--card);
      border: 1px solid var(--card-border);
    }
    .pill .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--port); }
    .pill.on .dot { background: var(--stbd); box-shadow: 0 0 10px var(--stbd); }

    /* ---------- cards ---------- */
    .card {
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: var(--radius);
      padding: 16px;
      -webkit-backdrop-filter: blur(20px);
      backdrop-filter: blur(20px);
    }
    .card-head {
      display: flex;
      align-items: baseline;
      justify-content: space-between;
      margin-bottom: 12px;
    }
    .label {
      font-size: 12px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.12em;
      color: var(--muted);
    }
    .value { font-size: 28px; font-weight: 700; font-variant-numeric: tabular-nums; }
    .value small { font-size: 14px; font-weight: 600; color: var(--muted); margin-left: 3px; }

    /* ---------- boat picture ---------- */
    /* water: a few soft light spots for sun glare, faint diagonal ripples, over a deep blue gradient */
    .boat-card {
      padding: 6px 16px;
      display: flex;
      justify-content: center;
      border-color: rgba(125,211,252,0.30);
      background:
        radial-gradient(60% 40% at 25% 20%, rgba(186,230,253,0.22), transparent 70%),
        radial-gradient(50% 35% at 80% 75%, rgba(125,211,252,0.16), transparent 70%),
        repeating-linear-gradient(160deg, rgba(255,255,255,0.05) 0 2px, transparent 2px 14px),
        linear-gradient(180deg, #0c6aa8 0%, #0a4d84 55%, #083a68 100%);
      box-shadow: inset 0 0 40px rgba(2,32,64,0.55);
    }
    .boat { width: 100%; max-height: 30vh; }

    /* ---------- rudder slider ---------- */
    .rudder {
      -webkit-appearance: none;
      appearance: none;
      width: 100%;
      height: 54px;
      background: transparent;
      touch-action: none;
    }
    .rudder::-webkit-slider-runnable-track {
      height: 10px;
      border-radius: 10px;
      background: linear-gradient(90deg, var(--port), rgba(255,255,255,0.15) 50%, var(--stbd));
    }
    .rudder::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 44px;
      height: 44px;
      margin-top: -17px;
      border-radius: 50%;
      background: #fff;
      border: 4px solid var(--accent);
      box-shadow: 0 4px 18px rgba(56,189,248,0.55);
    }
    .rudder::-moz-range-track {
      height: 10px;
      border-radius: 10px;
      background: linear-gradient(90deg, var(--port), rgba(255,255,255,0.15) 50%, var(--stbd));
    }
    .rudder::-moz-range-thumb {
      width: 36px;
      height: 36px;
      border-radius: 50%;
      background: #fff;
      border: 4px solid var(--accent);
    }
    .scale {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 12px;
      font-weight: 600;
      color: var(--muted);
      margin-top: 4px;
    }
    .scale .p { color: var(--port); }
    .scale .s { color: var(--stbd); }

    .center-btn {
      font: inherit;
      font-size: 12px;
      font-weight: 700;
      letter-spacing: 0.08em;
      color: var(--text);
      background: rgba(255,255,255,0.08);
      border: 1px solid var(--card-border);
      border-radius: 999px;
      padding: 8px 16px;
    }
    .center-btn:active { background: rgba(56,189,248,0.3); }

    /* ---------- sail controls ---------- */
    .sails { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }

    .bar {
      height: 8px;
      border-radius: 8px;
      background: rgba(255,255,255,0.08);
      overflow: hidden;
      margin-bottom: 14px;
    }
    .bar div {
      height: 100%;
      width: 50%;
      border-radius: 8px;
      background: linear-gradient(90deg, var(--accent2), var(--accent));
      transition: width 0.2s linear;
    }

    .btns { display: flex; flex-direction: column; gap: 10px; }
    .hold {
      font: inherit;
      font-size: 18px;
      font-weight: 700;
      color: var(--text);
      height: 64px;
      border-radius: 18px;
      border: 1px solid var(--card-border);
      background: linear-gradient(180deg, rgba(255,255,255,0.10), rgba(255,255,255,0.04));
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      touch-action: none;
      transition: transform 0.08s, background 0.08s, box-shadow 0.08s;
    }
    .hold svg { width: 20px; height: 20px; }
    .hold.active {
      transform: scale(0.96);
      background: linear-gradient(135deg, var(--accent2), var(--accent));
      box-shadow: 0 6px 24px rgba(56,189,248,0.45);
      border-color: transparent;
    }

    .hint { text-align: center; font-size: 12px; color: var(--muted); margin-top: auto; }
  </style>
</head>

<body>
  <div class="app">

    <header class="top">
      <div>
        <div class="brand">ET Phone <span>Boat</span></div>
        <div class="sub">ESP32_RCBOAT</div>
      </div>
      <div class="pill" id="link"><span class="dot"></span><span id="linktxt">No link</span></div>
    </header>

    <!-- top down picture of the boat, the rudder and sails rotate to show the actual servo positions -->
    <section class="card boat-card">
      <svg class="boat" viewBox="0 0 200 290">
        <defs>
          <linearGradient id="hull" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stop-color="#1e293b"/>
            <stop offset="1" stop-color="#0f172a"/>
          </linearGradient>
          <linearGradient id="sail" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0" stop-color="#38bdf8" stop-opacity="0.9"/>
            <stop offset="1" stop-color="#6366f1" stop-opacity="0.7"/>
          </linearGradient>
        </defs>
        <!-- hull, bow at the top -->
        <path d="M100,12 C140,60 150,130 145,200 C142,240 130,255 100,258 C70,255 58,240 55,200 C50,130 60,60 100,12 Z"
              fill="url(#hull)" stroke="rgba(255,255,255,0.45)" stroke-width="2"/>
        <!-- rudder, pivots at the stern -->
        <g id="rudderG" transform="translate(100,256) rotate(0)">
          <rect x="-3" y="0" width="6" height="28" rx="3" fill="#f8fafc"/>
        </g>
        <!-- jib, pivots at the bow -->
        <g id="jibG" transform="translate(100,40) rotate(-20)">
          <path d="M0,0 Q16,40 0,78 Z" fill="url(#sail)"/>
          <line x1="0" y1="0" x2="0" y2="78" stroke="#e2e8f0" stroke-width="2" stroke-linecap="round"/>
        </g>
        <!-- main sail and boom, pivots at the mast -->
        <g id="mainG" transform="translate(100,105) rotate(-30)">
          <path d="M0,0 Q22,60 0,120 Z" fill="url(#sail)"/>
          <line x1="0" y1="0" x2="0" y2="120" stroke="#e2e8f0" stroke-width="3" stroke-linecap="round"/>
        </g>
        <circle cx="100" cy="105" r="5" fill="#f8fafc"/>
      </svg>
    </section>

    <section class="card">
      <div class="card-head">
        <span class="label">Rudder</span>
        <span class="value" id="rudderval">0&deg;</span>
      </div>
      <input type="range" class="rudder" id="rudder" min="-45" max="45" step="1" value="0"
             oninput="SetRudder(this.value)">
      <div class="scale">
        <span class="p">&larr; PORT</span>
        <button class="center-btn" onclick="CenterRudder()">CENTER</button>
        <span class="s">STBD &rarr;</span>
      </div>
    </section>

    <div class="sails">
      <section class="card">
        <div class="card-head">
          <span class="label">Main</span>
          <span class="value"><span id="mainval">--</span><small>%</small></span>
        </div>
        <div class="bar"><div id="mainbar"></div></div>
        <div class="btns">
          <button class="hold" data-sail="MAIN" data-dir="IN">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M15 6l-6 6 6 6"/></svg>IN
          </button>
          <button class="hold" data-sail="MAIN" data-dir="OUT">
            OUT<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"/></svg>
          </button>
        </div>
      </section>

      <section class="card">
        <div class="card-head">
          <span class="label">Jib</span>
          <span class="value"><span id="jibval">--</span><small>%</small></span>
        </div>
        <div class="bar"><div id="jibbar"></div></div>
        <div class="btns">
          <button class="hold" data-sail="JIB" data-dir="IN">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M15 6l-6 6 6 6"/></svg>IN
          </button>
          <button class="hold" data-sail="JIB" data-dir="OUT">
            OUT<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"/></svg>
          </button>
        </div>
      </section>
    </div>

    <div class="hint">Hold IN / OUT to trim the sails</div>
  </div>

  <script type="text/javascript">

    // how often the page asks the ESP for the servo positions, and how often a held
    // sail button repeats its command (the ESP stops the sail if the repeats stop)
    var POLL_MS = 200;
    var HOLD_REPEAT_MS = 200;

    // if no good JSON reply for this long, show "No link"
    var LINK_TIMEOUT_MS = 1500;

    // sail picture angles, 0% = sheeted in, 100% = fully out
    var MAIN_MIN_DEG = 5, MAIN_MAX_DEG = 80;
    var JIB_MIN_DEG = 5, JIB_MAX_DEG = 70;

    var jsonHttp = new XMLHttpRequest();
    var lastReply = 0;
    var firstReply = true;
    var dragging = false;

    // ---------------------------------------------------------------------------
    // rudder
    // the slider fires oninput many times a second while dragging. Instead of flooding the
    // ESP with requests, only one request is in flight at a time, and when it finishes the
    // latest slider value (if it changed) is sent. The boat always ends up at the last value
    // ---------------------------------------------------------------------------
    var rudderPending = null;
    var rudderBusy = false;
    var lastRudderSend = 0;

    function SetRudder(value) {
      ShowRudder(value);
      rudderPending = value;
      SendRudder();
    }

    function SendRudder() {
      if (rudderBusy || rudderPending === null) {
        return;
      }
      var xhttp = new XMLHttpRequest();
      var value = rudderPending;
      rudderPending = null;
      rudderBusy = true;
      lastRudderSend = Date.now();
      xhttp.onreadystatechange = function() {
        // readyState 4 = done, success, error or timeout -- either way, send the next value
        if (this.readyState == 4) {
          rudderBusy = false;
          SendRudder();
        }
      };
      // same syntax as the SuperMon example, RUDDER is used in the .on method
      // and VALUE is read with server.arg("VALUE")
      xhttp.open("PUT", "RUDDER?VALUE=" + value, true);
      xhttp.timeout = 1000;
      xhttp.send();
    }

    function CenterRudder() {
      document.getElementById("rudder").value = 0;
      SetRudder(0);
    }

    function ShowRudder(value) {
      var v = parseInt(value);
      var txt;
      if (v == 0) {
        txt = "0&deg;";
      }
      else if (v < 0) {
        txt = (-v) + "&deg;<small>PORT</small>";
      }
      else {
        txt = v + "&deg;<small>STBD</small>";
      }
      document.getElementById("rudderval").innerHTML = txt;
      // trailing edge of the rudder swings to the side the boat turns
      document.getElementById("rudderG").setAttribute("transform", "translate(100,256) rotate(" + (-v) + ")");
    }

    // ---------------------------------------------------------------------------
    // sails -- press and hold buttons
    // pointer events work for both touch (iPhone) and mouse (PC testing)
    // ---------------------------------------------------------------------------
    var holdTimers = {};

    // every press gets its own ID. The repeats and the STOP of a press all carry the same ID,
    // so the ESP can ignore a repeat that arrives late (after its STOP) instead of restarting
    // the sail. Start at a random number so a page reload doesn't reuse old IDs
    var pressId = Math.floor(Math.random() * 1000000);

    function SendSail(sail, dir, id) {
      var xhttp = new XMLHttpRequest();
      // MAIN or JIB is used in the .on method, DIR and ID are read with server.arg()
      xhttp.open("PUT", sail + "?DIR=" + dir + "&ID=" + id, true);
      xhttp.timeout = 1000;
      xhttp.send();
    }

    function StartHold(btn) {
      var sail = btn.dataset.sail;
      var dir = btn.dataset.dir;
      StopHold(sail);
      pressId++;
      var id = pressId;
      btn.classList.add("active");
      SendSail(sail, dir, id);
      holdTimers[sail] = { btn: btn, id: id, timer: setInterval(function() {
        SendSail(sail, dir, id);
      }, HOLD_REPEAT_MS) };
      // a short buzz on phones that support it (not iPhone Safari, but harmless)
      if (navigator.vibrate) { navigator.vibrate(10); }
    }

    function StopHold(sail) {
      var h = holdTimers[sail];
      if (!h) {
        return;
      }
      clearInterval(h.timer);
      h.btn.classList.remove("active");
      delete holdTimers[sail];
      SendSail(sail, "STOP", h.id);
      // send the STOP a second time in case the first one gets lost, the ID makes this harmless
      setTimeout(function() { SendSail(sail, "STOP", h.id); }, 150);
    }

    function StopAll() {
      for (var sail in holdTimers) {
        StopHold(sail);
      }
    }

    document.querySelectorAll(".hold").forEach(function(btn) {
      btn.addEventListener("pointerdown", function(e) {
        e.preventDefault();
        // keep getting events for this finger even if it slides off the button
        btn.setPointerCapture(e.pointerId);
        StartHold(btn);
      });
      // any of these means the finger is off the button, touchend/touchcancel are a backup
      // for older iOS versions. Only stop if this button is the one driving the sail
      ["pointerup", "pointercancel", "lostpointercapture", "touchend", "touchcancel"].forEach(function(ev) {
        btn.addEventListener(ev, function() {
          var h = holdTimers[btn.dataset.sail];
          if (h && h.btn === btn) { StopHold(btn.dataset.sail); }
        });
      });
      btn.addEventListener("contextmenu", function(e) { e.preventDefault(); });
    });

    // stop the sails if the phone is locked or Safari goes to the background
    document.addEventListener("visibilitychange", function() {
      if (document.hidden) { StopAll(); }
    });
    window.addEventListener("blur", StopAll);

    // ---------------------------------------------------------------------------
    // JSON updates from the ESP
    // the ESP sends JSON that looks like this:
    // {"R":-12,"M":45,"J":60}
    // ---------------------------------------------------------------------------
    function ShowSail(prefix, gid, pivot, pct, minDeg, maxDeg) {
      document.getElementById(prefix + "val").innerHTML = pct;
      document.getElementById(prefix + "bar").style.width = pct + "%";
      var deg = minDeg + (maxDeg - minDeg) * pct / 100;
      document.getElementById(gid).setAttribute("transform", "translate(" + pivot + ") rotate(" + (-deg) + ")");
    }

    function Response() {
      var data;

      if (this.readyState != 4 || this.status != 200) {
        return;
      }

      try {
        data = JSON.parse(this.responseText);
      }
      catch (e) {
        console.log("bad JSON from ESP: " + this.responseText);
        return;
      }

      lastReply = Date.now();

      ShowSail("main", "mainG", "100,105", data.M, MAIN_MIN_DEG, MAIN_MAX_DEG);
      ShowSail("jib", "jibG", "100,40", data.J, JIB_MIN_DEG, JIB_MAX_DEG);

      // when the page first loads (or reloads) pick up where the boat is
      // after that the slider belongs to the user
      if (firstReply) {
        firstReply = false;
        document.getElementById("rudder").value = data.R;
        ShowRudder(data.R);
      }
      // the ESP centers the rudder if it lost the link, follow it when the user isn't dragging
      else if (!dragging && !rudderBusy && (Date.now() - lastRudderSend) > 1000 &&
               data.R != document.getElementById("rudder").value) {
        document.getElementById("rudder").value = data.R;
        ShowRudder(data.R);
      }
    }

    function UpdateLink() {
      var ok = (Date.now() - lastReply) < LINK_TIMEOUT_MS;
      document.getElementById("link").className = ok ? "pill on" : "pill";
      document.getElementById("linktxt").innerHTML = ok ? "Connected" : "No link";
    }

    // asks the ESP for the JSON data, then calls itself again
    // only starts a new request if the previous one is finished (or none has been sent yet)
    function Process() {
      if (jsonHttp.readyState == 0 || jsonHttp.readyState == 4) {
        jsonHttp.open("PUT", "json", true);
        jsonHttp.onreadystatechange = Response;
        jsonHttp.timeout = 1000;
        jsonHttp.send(null);
      }
      UpdateLink();
      setTimeout(Process, POLL_MS);
    }

    var slider = document.getElementById("rudder");
    slider.addEventListener("pointerdown", function() { dragging = true; });
    slider.addEventListener("touchstart", function() { dragging = true; }, { passive: true });
    ["pointerup", "pointercancel", "touchend", "touchcancel"].forEach(function(ev) {
      slider.addEventListener(ev, function() { dragging = false; });
    });

    Process();

  </script>

</body>

</html>

)=====";
