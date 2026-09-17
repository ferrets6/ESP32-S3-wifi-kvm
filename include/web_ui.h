#pragma once

// Main control page: touchpad + text keyboard + special keys, served from
// flash (no filesystem/LittleFS upload needed) and driven entirely over the
// /ws WebSocket for low latency.
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 KVM</title>
<style>
  :root{
    --bg:#121316; --panel:#1c1e23; --border:#2c2f36; --text:#e8e8ea;
    --muted:#8a8d94; --accent:#4f8cff; --accent-press:#3a6fd8; --danger:#ff5f5f; --ok:#3ecf6d;
    --key:#25272e; --key-border:#33363e; --key-press:#4f8cff; --key-mod-armed:#a855f7;
  }
  *{box-sizing:border-box; -webkit-tap-highlight-color:transparent; user-select:none;}
  html,body{margin:0;padding:0;background:var(--bg);color:var(--text);height:100%;
    font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;}
  body{display:flex;flex-direction:column;min-height:100vh;}
  .wrap{padding:12px;max-width:1400px;margin:0 auto;width:100%;flex:1;display:flex;flex-direction:column;}
  h1{font-size:16px;font-weight:600;margin:4px 0 12px;display:flex;align-items:center;gap:8px;}
  .status{width:10px;height:10px;border-radius:50%;background:var(--danger);flex:none;}
  .status.on{background:var(--ok);}
  .panel{background:var(--panel);border:1px solid var(--border);border-radius:14px;
    padding:12px;margin-bottom:14px;}
  .panel h2{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);
    margin:0 0 10px;font-weight:600;}

  .layout{display:flex;flex-wrap:wrap;gap:14px;flex:1;align-items:flex-start;}
  .mainCol{flex:1 1 640px;min-width:300px;}
  .padCol{flex:1 1 300px;min-width:260px;max-width:420px;display:flex;flex-direction:column;}

  /* ---- Last action feedback ---- */
  .lastAction{margin-top:2px;font-size:13px;color:var(--muted);min-height:20px;transition:color .15s;}
  .lastAction.flash{color:var(--text);}

  /* ---- Full graphical keyboard (desktop) ---- */
  .kbRow{display:flex;gap:5px;margin-bottom:5px;}
  .kbRow:last-child{margin-bottom:0;}
  .key{
    flex:1; background:var(--key); border:1px solid var(--key-border); border-radius:7px;
    color:var(--text); font:inherit; cursor:pointer; padding:0; position:relative;
    height:44px; display:flex; align-items:center; justify-content:center;
    font-size:14px; transition:background .05s, transform .05s;
  }
  .key:active, .key.pressed{background:var(--key-press);border-color:var(--key-press);transform:translateY(1px);}
  .key.mod.armed{background:var(--key-mod-armed);border-color:var(--key-mod-armed);color:#fff;}
  .key .lbl-shift{position:absolute;top:3px;left:6px;font-size:9px;color:var(--muted);}
  .key.pressed .lbl-shift, .key.armed .lbl-shift{color:rgba(255,255,255,.8);}
  .key .lbl-altgr{position:absolute;bottom:3px;right:6px;font-size:9px;color:#7fa8ff;}
  .key.wide-2{flex:2;}
  .key.wide-tab{flex:1.6;}
  .key.wide-caps{flex:1.9;}
  .key.wide-enter{flex:2;background:#282b33;}
  .key.wide-shift{flex:2.3;}
  .key.wide-space{flex:6;}
  .key.wide-ctrl{flex:1.3;}
  .key small{font-size:11px;color:var(--muted);}
  .key.pressed small, .key.armed small{color:rgba(255,255,255,.85);}

  .kbMainArea{display:flex;gap:10px;}
  .kbLetters{flex:1;}
  .kbNav{width:140px;display:flex;flex-direction:column;gap:5px;}
  .navGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:5px;}
  .navGrid .key{height:34px;font-size:11px;}
  .arrowGrid{display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(2,1fr);gap:5px;margin-top:auto;}
  .arrowGrid .key{height:34px;}
  .arrowGrid .key.up{grid-column:2;grid-row:1;}
  .arrowGrid .key.left{grid-column:1;grid-row:2;}
  .arrowGrid .key.down{grid-column:2;grid-row:2;}
  .arrowGrid .key.right{grid-column:3;grid-row:2;}

  /* ---- Touchpad ---- */
  #touchpad{flex:1;min-height:220px;border-radius:12px;background:#25272e;
    border:1px solid var(--border);touch-action:none;position:relative;overflow:hidden;
    display:flex;align-items:center;justify-content:center;color:var(--muted);font-size:13px;text-align:center;padding:10px;}
  #touchpad.active{background:#2a2d36;}
  .row{display:flex;gap:8px;margin-top:8px;}
  button.btn{font:inherit;color:var(--text);background:#2a2d34;border:1px solid var(--border);
    border-radius:10px;padding:10px 8px;cursor:pointer;user-select:none;}
  button.btn:active{background:var(--accent-press);border-color:var(--accent-press);}
  .row button.btn{flex:1;}
  button.primary{background:var(--accent);border-color:var(--accent);color:#fff;}
  button.primary:active{background:var(--accent-press);}
  button.toggle.on{background:var(--ok);border-color:var(--ok);color:#08210f;}
  textarea{width:100%;min-height:70px;background:#25272e;border:1px solid var(--border);
    border-radius:10px;color:var(--text);font:inherit;padding:10px;resize:vertical;}
  .grid4{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}

  /* ---- Mobile-only compact controls ---- */
  #mobilePanel{display:none;}
  .mobKeyGrid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;}
  .mobKeyGrid button.btn{padding:14px 4px;font-size:15px;}
  .lastActionBig{font-size:22px;font-weight:600;text-align:center;padding:14px;
    background:#1c1e23;border:1px solid var(--border);border-radius:12px;margin-bottom:10px;
    color:var(--muted);transition:color .15s, background .15s;}
  .lastActionBig.flash{color:#fff;background:#2a2d3a;}

  a.link{color:var(--accent);font-size:13px;text-decoration:none;}
  footer{text-align:center;color:var(--muted);font-size:12px;margin-top:6px;}

  @media (max-width: 760px){
    #keyboardPanel{display:none;}
    #mobilePanel{display:block;}
    .padCol{max-width:none;}
    #touchpad{min-height:38vh;}
    .wrap{padding:10px;}
  }
  @media (min-width: 761px){
    .lastAction{display:none;} /* the keycap itself already gives feedback */
  }
</style>
</head>
<body>
  <div class="wrap">
  <h1>
    <span id="dot" class="status"></span> ESP32 KVM
  </h1>

  <div class="layout">
    <div class="mainCol">

      <div class="panel" id="keyboardPanel">
        <h2>Keyboard</h2>
        <div class="kbMainArea">
          <div class="kbLetters" id="kbLetters"></div>
          <div class="kbNav">
            <div class="navGrid" id="kbNavGrid"></div>
            <div class="arrowGrid" id="kbArrows"></div>
          </div>
        </div>
        <div class="lastAction" id="lastAction">&nbsp;</div>
      </div>

      <div class="panel" id="mobilePanel">
        <h2>Keys</h2>
        <div class="lastActionBig" id="lastActionBig">ready</div>
        <div class="mobKeyGrid">
          <button class="btn" data-mk="ENTER">Enter</button>
          <button class="btn" data-mk="BACKSPACE">&larr;Del</button>
          <button class="btn" data-mk="ESC">Esc</button>
          <button class="btn" data-mk="TAB">Tab</button>
          <button class="btn" data-mk="ARROW_UP">&uarr;</button>
          <button class="btn" data-mk="ARROW_LEFT">&larr;</button>
          <button class="btn" data-mk="ARROW_DOWN">&darr;</button>
          <button class="btn" data-mk="ARROW_RIGHT">&rarr;</button>
        </div>
      </div>

      <div class="panel">
        <h2>Quick typing</h2>
        <textarea id="typebox" placeholder="Type here... sent key by key in real time" autocapitalize="off" autocomplete="off" spellcheck="false"></textarea>
        <div class="row">
          <button class="btn primary" id="btnSendAll">Send all</button>
          <button class="btn" id="btnClear">Clear</button>
        </div>
      </div>

      <div class="panel">
        <h2>Combos</h2>
        <div class="grid4">
          <button class="btn" data-combo="CTRL_C">Ctrl+C</button>
          <button class="btn" data-combo="CTRL_V">Ctrl+V</button>
          <button class="btn" data-combo="CTRL_X">Ctrl+X</button>
          <button class="btn" data-combo="CTRL_A">Ctrl+A</button>
          <button class="btn" data-combo="CTRL_Z">Ctrl+Z</button>
          <button class="btn" data-combo="CTRL_S">Ctrl+S</button>
          <button class="btn" data-combo="ALT_TAB">Alt+Tab</button>
          <button class="btn" data-combo="WIN">Win</button>
        </div>
      </div>

    </div>

    <div class="padCol">
      <div class="panel" style="flex:1;display:flex;flex-direction:column;">
        <h2>Touchpad</h2>
        <div id="touchpad">drag to move &middot; tap for left click &middot; two fingers for right click</div>
        <div class="row">
          <button class="btn" id="btnLeft">Left</button>
          <button class="btn" id="btnMid">Middle</button>
          <button class="btn" id="btnRight">Right</button>
        </div>
        <div class="row">
          <button class="btn toggle" id="btnDrag">&#128274; Drag</button>
        </div>
      </div>
    </div>
  </div>

  <footer>
    <a class="link" href="/wifi">&#9881; Settings</a>
    &nbsp;&middot;&nbsp;
    <a class="link" href="/update">&#8635; Update firmware (OTA)</a>
    &nbsp;&middot;&nbsp;
    <a class="link" href="/status">&#128203; Status/log</a>
  </footer>
  </div>

<script>
(function(){
  var dot = document.getElementById('dot');
  var ws = null;
  var wantClose = false;

  function connect(){
    ws = new WebSocket('ws://' + location.host + '/ws');
    ws.onopen = function(){ dot.classList.add('on'); };
    ws.onclose = function(){ dot.classList.remove('on'); if(!wantClose) setTimeout(connect, 1200); };
    ws.onerror = function(){ try{ws.close();}catch(e){} };
  }
  connect();

  function send(msg){
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(msg);
  }

  // ---- IT keymap table (mirrors src/it_keymap.cpp / KeyboardLayout_it_IT.cpp) ----
  var ROW0 = [
    {t:'special', code:'ESC', label:'Esc'},
    {t:'special', code:'F1', label:'F1'}, {t:'special', code:'F2', label:'F2'},
    {t:'special', code:'F3', label:'F3'}, {t:'special', code:'F4', label:'F4'},
    {t:'special', code:'F5', label:'F5'}, {t:'special', code:'F6', label:'F6'},
    {t:'special', code:'F7', label:'F7'}, {t:'special', code:'F8', label:'F8'},
    {t:'special', code:'F9', label:'F9'}, {t:'special', code:'F10', label:'F10'},
    {t:'special', code:'F11', label:'F11'}, {t:'special', code:'F12', label:'F12'}
  ];
  var ROW1 = [
    {t:'char', b:'\\', s:'|'},
    {t:'char', b:'1', s:'!'}, {t:'char', b:'2', s:'"'}, {t:'char', b:'3', s:'£'},
    {t:'char', b:'4', s:'$'}, {t:'char', b:'5', s:'%'}, {t:'char', b:'6', s:'&'},
    {t:'char', b:'7', s:'/'}, {t:'char', b:'8', s:'('}, {t:'char', b:'9', s:')'},
    {t:'char', b:'0', s:'='},
    {t:'char', b:'\'', s:'?'},
    {t:'char', b:'ì', s:'^'},
    {t:'special', code:'BACKSPACE', label:'&larr; Backspace', w:'wide-2'}
  ];
  var ROW2 = [
    {t:'special', code:'TAB', label:'Tab', w:'wide-tab'},
    {t:'char', b:'q', s:'Q'}, {t:'char', b:'w', s:'W'}, {t:'char', b:'e', s:'E'},
    {t:'char', b:'r', s:'R'}, {t:'char', b:'t', s:'T'}, {t:'char', b:'y', s:'Y'},
    {t:'char', b:'u', s:'U'}, {t:'char', b:'i', s:'I'}, {t:'char', b:'o', s:'O'},
    {t:'char', b:'p', s:'P'},
    {t:'char', b:'è', s:'é', a:'[', as:'{'},
    {t:'char', b:'+', s:'*', a:']', as:'}'}
  ];
  var ROW3 = [
    {t:'mod', code:'CAPS', label:'Caps', w:'wide-caps'},
    {t:'char', b:'a', s:'A'}, {t:'char', b:'s', s:'S'}, {t:'char', b:'d', s:'D'},
    {t:'char', b:'f', s:'F'}, {t:'char', b:'g', s:'G'}, {t:'char', b:'h', s:'H'},
    {t:'char', b:'j', s:'J'}, {t:'char', b:'k', s:'K'}, {t:'char', b:'l', s:'L'},
    {t:'char', b:'ò', s:'ç', a:'@'},
    {t:'char', b:'à', s:'°', a:'#'},
    {t:'char', b:'ù', s:'§'},
    {t:'special', code:'ENTER', label:'Enter', w:'wide-enter'}
  ];
  var ROW4 = [
    {t:'mod', code:'SHIFT', label:'Shift', w:'wide-shift'},
    {t:'char', b:'<', s:'>'},
    {t:'char', b:'z', s:'Z'}, {t:'char', b:'x', s:'X'}, {t:'char', b:'c', s:'C'},
    {t:'char', b:'v', s:'V'}, {t:'char', b:'b', s:'B'}, {t:'char', b:'n', s:'N'},
    {t:'char', b:'m', s:'M'},
    {t:'char', b:',', s:';'}, {t:'char', b:'.', s:':'}, {t:'char', b:'-', s:'_'},
    {t:'mod', code:'SHIFT', label:'Shift', w:'wide-shift'}
  ];
  var ROW5 = [
    {t:'mod', code:'CTRL', label:'Ctrl', w:'wide-ctrl'},
    {t:'mod', code:'WIN', label:'Win', w:'wide-ctrl'},
    {t:'mod', code:'ALT', label:'Alt', w:'wide-ctrl'},
    {t:'special', code:'SPACE', label:'', w:'wide-space'},
    {t:'mod', code:'ALTGR', label:'AltGr', w:'wide-ctrl'},
    {t:'mod', code:'CTRL', label:'Ctrl', w:'wide-ctrl'}
  ];
  var ROWS = [ROW0, ROW1, ROW2, ROW3, ROW4, ROW5];

  var NAV_KEYS = [
    {code:'INSERT', label:'Ins'}, {code:'HOME', label:'Home'}, {code:'PAGE_UP', label:'PgUp'},
    {code:'DELETE', label:'Del'}, {code:'END', label:'End'}, {code:'PAGE_DOWN', label:'PgDn'}
  ];

  // ---- Modifier state (one-shot: armed by one tap, consumed by the next key) ----
  var mods = {SHIFT:false, CTRL:false, ALT:false, ALTGR:false, WIN:false};
  var capsOn = false;

  function clearOneShotMods(){
    mods = {SHIFT:false, CTRL:false, ALT:false, ALTGR:false, WIN:false};
    document.querySelectorAll('.key.mod').forEach(function(k){ k.classList.remove('armed'); });
  }

  function flashLast(text){
    var la = document.getElementById('lastAction');
    var lb = document.getElementById('lastActionBig');
    la.textContent = text; lb.textContent = text;
    la.classList.remove('flash'); lb.classList.remove('flash');
    void la.offsetWidth;
    la.classList.add('flash'); lb.classList.add('flash');
  }

  function pressFx(el){
    el.classList.add('pressed');
    setTimeout(function(){ el.classList.remove('pressed'); }, 130);
  }

  function activeShortcutMods(){
    var list = [];
    if (mods.CTRL) list.push('CTRL');
    if (mods.ALT) list.push('ALT');
    if (mods.WIN) list.push('WIN');
    if (list.length && mods.SHIFT) list.push('SHIFT');
    return list;
  }

  function sendChar(def){
    var shortcuts = activeShortcutMods();
    if (shortcuts.length){
      send('kg:' + shortcuts.join(',') + ':' + def.b);
      flashLast(shortcuts.join('+') + '+' + def.b.toUpperCase());
    } else {
      var isLetter = /^[a-z]$/i.test(def.b);
      var shiftEffective = isLetter ? (mods.SHIFT !== capsOn) : mods.SHIFT;
      var ch = def.b;
      if (mods.ALTGR && mods.SHIFT && def.as) ch = def.as;
      else if (mods.ALTGR && def.a) ch = def.a;
      else if (shiftEffective && def.s) ch = def.s;
      send('kt:' + ch);
      flashLast(ch);
    }
    clearOneShotMods();
  }

  function sendSpecial(code, label){
    var list = [];
    if (mods.CTRL) list.push('CTRL');
    if (mods.ALT) list.push('ALT');
    if (mods.SHIFT) list.push('SHIFT');
    if (mods.ALTGR) list.push('ALTGR');
    if (mods.WIN) list.push('WIN');
    if (code === 'SPACE'){
      if (list.length) send('kg:' + list.join(',') + ':\x20');
      else send('kt:\x20');
    } else if (list.length) {
      send('kg:' + list.join(',') + ':' + code);
    } else {
      send('kk:' + code);
    }
    flashLast((list.length ? list.join('+') + '+' : '') + (label || code));
    clearOneShotMods();
  }

  function toggleMod(code, el){
    if (code === 'CAPS'){
      capsOn = !capsOn;
      el.classList.toggle('armed', capsOn);
      send('kk:CAPSLOCK');
      flashLast('CapsLock ' + (capsOn ? 'on' : 'off'));
      return;
    }
    mods[code] = !mods[code];
    document.querySelectorAll('.key.mod[data-code="' + code + '"]').forEach(function(k){
      k.classList.toggle('armed', mods[code]);
    });
  }

  // ---- Build keyboard DOM ----
  function buildKey(def){
    var el = document.createElement('button');
    el.className = 'key' + (def.w ? ' ' + def.w : '') + (def.t === 'mod' ? ' mod' : '');
    if (def.code) el.dataset.code = def.code;

    if (def.t === 'char'){
      var main = document.createElement('span');
      main.textContent = def.b;
      el.appendChild(main);
      if (def.s){
        var sh = document.createElement('span');
        sh.className = 'lbl-shift'; sh.textContent = def.s;
        el.appendChild(sh);
      }
      if (def.a){
        var ag = document.createElement('span');
        ag.className = 'lbl-altgr'; ag.textContent = def.a;
        el.appendChild(ag);
      }
      el.addEventListener('click', function(){ pressFx(el); sendChar(def); });
    } else if (def.t === 'special'){
      el.innerHTML = '<small>' + def.label + '</small>';
      el.addEventListener('click', function(){ pressFx(el); sendSpecial(def.code, def.label); });
    } else if (def.t === 'mod'){
      el.innerHTML = '<small>' + def.label + '</small>';
      el.addEventListener('click', function(){ pressFx(el); toggleMod(def.code, el); });
    }
    return el;
  }

  var lettersEl = document.getElementById('kbLetters');
  ROWS.forEach(function(row){
    var rowEl = document.createElement('div');
    rowEl.className = 'kbRow';
    row.forEach(function(def){ rowEl.appendChild(buildKey(def)); });
    lettersEl.appendChild(rowEl);
  });

  var navGridEl = document.getElementById('kbNavGrid');
  NAV_KEYS.forEach(function(k){
    var el = document.createElement('button');
    el.className = 'key';
    el.innerHTML = '<small>' + k.label + '</small>';
    el.addEventListener('click', function(){ pressFx(el); sendSpecial(k.code, k.label); });
    navGridEl.appendChild(el);
  });

  var arrowsEl = document.getElementById('kbArrows');
  [
    {code:'ARROW_UP', label:'&uarr;', cls:'up'},
    {code:'ARROW_LEFT', label:'&larr;', cls:'left'},
    {code:'ARROW_DOWN', label:'&darr;', cls:'down'},
    {code:'ARROW_RIGHT', label:'&rarr;', cls:'right'}
  ].forEach(function(a){
    var el = document.createElement('button');
    el.className = 'key ' + a.cls;
    el.innerHTML = a.label;
    el.addEventListener('click', function(){ pressFx(el); sendSpecial(a.code, a.label); });
    arrowsEl.appendChild(el);
  });

  // ---- Mobile quick keys ----
  document.querySelectorAll('[data-mk]').forEach(function(btn){
    btn.addEventListener('click', function(){ sendSpecial(btn.dataset.mk, btn.textContent); });
  });

  // ---- Combos ----
  document.querySelectorAll('[data-combo]').forEach(function(btn){
    btn.addEventListener('click', function(){ send('kc:' + btn.dataset.combo); flashLast(btn.textContent); });
  });

  // ---- Touchpad (Pointer Events: works for mouse + touch + pen) ----
  var pad = document.getElementById('touchpad');
  var active = new Set();
  var maxPointers = 0, lastX = 0, lastY = 0, gestureStart = 0, moved = 0;
  var SENSITIVITY = 1.4;

  pad.addEventListener('pointerdown', function(e){
    pad.setPointerCapture(e.pointerId);
    active.add(e.pointerId);
    if (active.size === 1){ gestureStart = Date.now(); moved = 0; maxPointers = 1; }
    else { maxPointers = Math.max(maxPointers, active.size); }
    lastX = e.clientX; lastY = e.clientY;
    pad.classList.add('active');
  });

  pad.addEventListener('pointermove', function(e){
    if (!active.has(e.pointerId)) return;
    var dx = e.clientX - lastX, dy = e.clientY - lastY;
    lastX = e.clientX; lastY = e.clientY;
    moved += Math.abs(dx) + Math.abs(dy);
    if (dx || dy){
      send('mm:' + Math.round(dx * SENSITIVITY) + ':' + Math.round(dy * SENSITIVITY));
    }
  });

  function endPointer(e){
    if (!active.has(e.pointerId)) return;
    active.delete(e.pointerId);
    if (active.size === 0){
      pad.classList.remove('active');
      var dt = Date.now() - gestureStart;
      if (dt < 300 && moved < 14){
        send(maxPointers >= 2 ? 'mc:right' : 'mc:left');
      }
    }
  }
  pad.addEventListener('pointerup', endPointer);
  pad.addEventListener('pointercancel', endPointer);

  document.getElementById('btnLeft').addEventListener('click', function(){ send('mc:left'); });
  document.getElementById('btnMid').addEventListener('click', function(){ send('mc:middle'); });
  document.getElementById('btnRight').addEventListener('click', function(){ send('mc:right'); });

  var dragBtn = document.getElementById('btnDrag');
  var dragOn = false;
  dragBtn.addEventListener('click', function(){
    dragOn = !dragOn;
    dragBtn.classList.toggle('on', dragOn);
    send(dragOn ? 'md:left' : 'mu:left');
  });

  // ---- Text typing: diff textarea content so real keydown/keyup HID
  // events are sent per character (also handles paste and deletions) ----
  var ta = document.getElementById('typebox');
  var lastValue = '';
  ta.addEventListener('input', function(){
    var newValue = ta.value;
    var minLen = Math.min(lastValue.length, newValue.length);
    var p = 0;
    while (p < minLen && lastValue[p] === newValue[p]) p++;
    var s = 0;
    while (s < (minLen - p) &&
           lastValue[lastValue.length - 1 - s] === newValue[newValue.length - 1 - s]) s++;
    var removed = lastValue.length - p - s;
    var inserted = newValue.substring(p, newValue.length - s);
    for (var i = 0; i < removed; i++) send('kk:BACKSPACE');
    if (inserted) { send('kt:' + inserted); flashLast(inserted); }
    lastValue = newValue;
  });

  document.getElementById('btnSendAll').addEventListener('click', function(){
    send('kt:' + ta.value);
  });
  document.getElementById('btnClear').addEventListener('click', function(){
    ta.value = '';
    lastValue = '';
  });
})();
</script>
</body>
</html>
)HTMLPAGE";

// WiFi setup page: lets the user point the ESP32 at their home network
// without hardcoding credentials in the firmware. Submits to POST /wifi.
const char WIFI_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 KVM - Settings</title>
<style>
  body{margin:0;padding:24px;background:#121316;color:#e8e8ea;
    font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;max-width:420px;margin:0 auto;}
  .logo{font-size:13px;color:#8a8d94;letter-spacing:.05em;text-transform:uppercase;margin-bottom:2px;}
  h1{font-size:18px;margin:0 0 20px;}
  h2{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:#8a8d94;margin:26px 0 10px;font-weight:600;}
  h2:first-of-type{margin-top:0;}
  label{display:block;margin:14px 0 4px;font-size:13px;color:#8a8d94;}
  input{width:100%;padding:10px;border-radius:8px;border:1px solid #2c2f36;background:#1c1e23;color:#e8e8ea;font:inherit;}
  button{margin-top:18px;width:100%;padding:12px;border-radius:10px;border:none;background:#4f8cff;color:#fff;font:inherit;cursor:pointer;}
  button.danger{background:#3a2020;color:#ff9c9c;border:1px solid #5c2c2c;}
  a{color:#4f8cff;font-size:13px;}
  .note{font-size:12px;color:#8a8d94;margin-top:14px;line-height:1.5;}
  .pwrow{display:flex;gap:8px;align-items:stretch;}
  .pwrow input{flex:1;}
  .pwrow button{margin-top:0;width:auto;padding:0 14px;background:#2a2d34;border:1px solid #2c2f36;flex:none;}
  .switchRow{display:flex;align-items:center;gap:10px;font-size:14px;padding:4px 0;}
  .switchRow input{width:auto;margin:0;}
  hr{border:none;border-top:1px solid #2c2f36;margin:24px 0 0;}
</style>
</head>
<body>
  <div class="logo">&#9881; ESP32 KVM</div>
  <h1>Settings</h1>

  <h2>WiFi network</h2>
  <form method="POST" action="/wifi">
    <label>Network name (SSID)</label>
    <input name="ssid" value="%SSID%" required maxlength="32">
    <label>Password</label>
    <div class="pwrow">
      <input id="pass" name="pass" type="password" value="" maxlength="63" placeholder="leave empty for an open network">
      <button type="button" id="togglePass">Show</button>
    </div>
    <button type="submit">Save and restart</button>
  </form>
  <p class="note">
    On reconnect the board will try to join this network as a client.
    If that fails, it automatically falls back to Access Point mode (%APSSID%).
  </p>

  <hr>
  <h2>Command LED</h2>
  <form method="POST" action="/led">
    <label class="switchRow">
      <input type="checkbox" name="enabled" value="1" %LEDCHECKED% onchange="this.form.submit()">
      Flash on every command received from the app
    </label>
  </form>

  <hr>
  <h2>Maintenance</h2>
  <form method="POST" action="/reboot"
        onsubmit="return confirm('Restart the board now? WiFi and USB will drop for a few seconds.');">
    <button type="submit" class="danger">&#8635; Restart the board</button>
  </form>

  <script>
    document.getElementById('togglePass').addEventListener('click', function(){
      var f = document.getElementById('pass');
      var hidden = f.type === 'password';
      f.type = hidden ? 'text' : 'password';
      this.textContent = hidden ? 'Hide' : 'Show';
    });
  </script>
  <p><a href="/">&larr; Back to control panel</a></p>
</body>
</html>
)HTMLPAGE";

// OTA firmware update page. Talks directly to the /ota/start and
// /ota/upload endpoints registered by the ElegantOTA library (its own
// bundled page, which we don't serve, is what carries its promo banner).
const char OTA_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 KVM - Firmware update</title>
<style>
  body{margin:0;padding:24px;background:#121316;color:#e8e8ea;
    font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;max-width:420px;margin:0 auto;}
  h1{font-size:16px;}
  .drop{margin-top:16px;border:2px dashed #2c2f36;border-radius:12px;padding:28px 16px;
    text-align:center;color:#8a8d94;font-size:13px;cursor:pointer;}
  .drop.over{border-color:#4f8cff;color:#e8e8ea;}
  input[type=file]{display:none;}
  #fname{margin-top:10px;font-size:13px;word-break:break-all;}
  progress{width:100%;margin-top:16px;height:10px;border-radius:6px;overflow:hidden;}
  button{margin-top:16px;width:100%;padding:12px;border-radius:10px;border:none;background:#4f8cff;color:#fff;font:inherit;cursor:pointer;}
  button:disabled{background:#2a2d34;color:#8a8d94;cursor:default;}
  #status{margin-top:14px;font-size:13px;white-space:pre-wrap;}
  #status.err{color:#ff5f5f;}
  #status.ok{color:#3ecf6d;}
  a{color:#4f8cff;font-size:13px;}
</style>
</head>
<body>
  <h1>Firmware update (OTA)</h1>
  <div class="drop" id="drop">Drop firmware.bin here<br>or tap to choose the file</div>
  <input type="file" id="file" accept=".bin">
  <div id="fname"></div>
  <progress id="bar" value="0" max="100" hidden></progress>
  <button id="go" disabled>Upload and update</button>
  <div id="status"></div>
  <p><a href="/">&larr; Back to control panel</a></p>
<script>
(function(){
  var drop = document.getElementById('drop');
  var fileInput = document.getElementById('file');
  var fname = document.getElementById('fname');
  var bar = document.getElementById('bar');
  var go = document.getElementById('go');
  var status = document.getElementById('status');
  var selected = null;

  function pick(file){
    if (!file) return;
    selected = file;
    fname.textContent = file.name + ' (' + Math.round(file.size/1024) + ' KB)';
    go.disabled = false;
    status.textContent = '';
    status.className = '';
  }

  drop.addEventListener('click', function(){ fileInput.click(); });
  fileInput.addEventListener('change', function(){ pick(fileInput.files[0]); });
  ['dragenter','dragover'].forEach(function(ev){
    drop.addEventListener(ev, function(e){ e.preventDefault(); drop.classList.add('over'); });
  });
  ['dragleave','drop'].forEach(function(ev){
    drop.addEventListener(ev, function(e){ e.preventDefault(); drop.classList.remove('over'); });
  });
  drop.addEventListener('drop', function(e){
    if (e.dataTransfer.files.length) pick(e.dataTransfer.files[0]);
  });

  go.addEventListener('click', function(){
    if (!selected) return;
    go.disabled = true;
    bar.hidden = false;
    bar.value = 0;
    status.className = '';
    status.textContent = 'Starting update...';

    fetch('/ota/start?mode=firmware').then(function(r){
      if (!r.ok) return r.text().then(function(t){ throw new Error(t || 'start failed'); });

      return new Promise(function(resolve, reject){
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/ota/upload');
        xhr.upload.onprogress = function(e){
          if (e.lengthComputable) bar.value = Math.round(e.loaded / e.total * 100);
        };
        xhr.onload = function(){
          if (xhr.status >= 200 && xhr.status < 300) resolve();
          else reject(new Error(xhr.responseText || ('HTTP error ' + xhr.status)));
        };
        xhr.onerror = function(){ reject(new Error('network error during upload')); };
        var fd = new FormData();
        fd.append('firmware', selected);
        xhr.send(fd);
      });
    }).then(function(){
      status.className = 'ok';
      status.textContent = 'Uploaded. The board is restarting with the new firmware...';
    }).catch(function(err){
      status.className = 'err';
      status.textContent = 'Error: ' + err.message;
      go.disabled = false;
    });
  });
})();
</script>
</body>
</html>
)HTMLPAGE";
