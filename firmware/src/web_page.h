// The two pages the board serves. Plain HTML + JS, no external files, so they
// also work from the hotspot with no internet. Layout and wording follow the
// mockup Will approved on 2026-10-07 (claude.ai/artifact/Sn3CJMSGXtuWtxzehrp14L).
//
//  SETUP_PAGE ("/setup")  first-time setup: pick a network, password,
//                         connecting, done. Phones joining the hotspot get this
//                         until setup is finished.
//  WEB_PAGE   ("/")       everyday settings: brightness, mode, colors, and
//                         Wi-Fi / Firmware / Advanced screens.
//
// iOS's Wi-Fi login popup moves focus into a text field whenever the page
// changes around it, so neither page refreshes while a text field is on screen.
#pragma once
#include <pgmspace.h>

#define TM_STYLE R"CSS(
:root{--bg:#F5F4F0;--card:#FFFFFF;--fg:#1B1B19;--mut:#55554F;--mut2:#6A6A63;--line:#ECEBE6;--edge:#D9D7D0;--seg:#E7E5DF;--acc:#B0122B;--ok:#1F7A3A;--warn:#B26A00;--bad:#B0122B}
@media (prefers-color-scheme:dark){:root{--bg:#141413;--card:#1F1F1D;--fg:#EDEDE9;--mut:#B4B3AC;--mut2:#9C9B94;--line:#2E2E2B;--edge:#3A3A36;--seg:#2A2A27;--acc:#E0455C;--ok:#3FAF62;--warn:#E09A3A;--bad:#F06A7C}}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif}
main{max-width:480px;margin:0 auto;padding:28px 16px 40px;display:flex;flex-direction:column;gap:22px;min-height:100vh}
h1{margin:0;font-size:28px;font-weight:700;letter-spacing:-.4px;padding:0 4px}p{margin:0}
.cap{font-size:13px;font-weight:600;color:var(--mut2);text-transform:uppercase;letter-spacing:.4px;padding:0 12px;margin:0}
.note{font-size:13px;color:var(--mut2);padding:0 12px}.lead{color:var(--mut);padding:0 4px}
.grp{display:flex;flex-direction:column;gap:8px}
.card{background:var(--card);border-radius:14px;overflow:hidden;display:flex;flex-direction:column}
.row{min-height:52px;display:flex;align-items:center;gap:10px;padding:0 16px;border:0;border-bottom:1px solid var(--line);background:var(--card);color:var(--fg);font:inherit;font-size:17px;text-align:left;width:100%}
.row:last-child{border-bottom:0}button.row{cursor:pointer}.row .grow{flex-grow:1}.row .val{color:var(--mut2);font-size:16px}.row .chev{color:var(--edge);font-size:22px}
.kv{min-height:46px;font-size:15px}.kv .grow{color:var(--mut)}
.seg{display:grid;gap:4px;background:var(--seg);border-radius:12px;padding:4px}
.seg button{height:40px;border:0;border-radius:9px;background:transparent;color:var(--mut);font:inherit;font-size:15px;cursor:pointer}
.seg button.on{background:var(--card);color:var(--fg);font-weight:600;box-shadow:0 1px 3px rgba(0,0,0,.12)}
.btn{height:52px;border:0;border-radius:14px;background:var(--acc);color:#fff;font:inherit;font-size:17px;font-weight:600;cursor:pointer;width:100%;display:flex;align-items:center;justify-content:center;text-decoration:none}
.btn2{height:52px;border:1px solid var(--edge);border-radius:14px;background:var(--card);color:var(--fg);font:inherit;font-size:17px;font-weight:600;cursor:pointer;width:100%;display:flex;align-items:center;justify-content:center}
.link{align-self:flex-start;min-height:44px;border:0;background:transparent;color:var(--acc);font:inherit;font-size:16px;cursor:pointer;padding:0 8px}
.dot{width:9px;height:9px;border-radius:50%;flex:none;background:var(--mut2)}.dot.ok{background:var(--ok)}.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}
.field{display:flex;flex-direction:column;padding:6px 16px;gap:0}
.field label{font-size:13px;color:var(--mut2);padding-top:8px}
.field input,.field select{height:44px;border:0;border-bottom:1px solid var(--line);font:inherit;font-size:17px;background:transparent;color:var(--fg);padding:0;width:100%}
.field>:last-child{border-bottom:0}
input:focus,select:focus{outline:2px solid var(--acc);outline-offset:1px}
input[type=color]{-webkit-appearance:none;appearance:none;border:0;padding:0;width:32px;height:32px;border-radius:50%;background:none;cursor:pointer;flex:none}
input[type=color]::-webkit-color-swatch-wrapper{padding:0}input[type=color]::-webkit-color-swatch{border:0;border-radius:50%}input[type=color]::-moz-color-swatch{border:0;border-radius:50%}
.num{width:64px;height:36px;border:1px solid var(--edge);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit;font-size:15px;text-align:right;padding:0 8px}
.err{color:var(--bad);font-size:15px;padding:0 8px}.okt{color:var(--ok)}
.spacer{flex-grow:1}.hide{display:none!important}
.spin{width:48px;height:48px;border-radius:50%;border:4px solid var(--seg);border-top-color:var(--acc);animation:spin .9s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}
.center{flex-grow:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:20px;text-align:center;padding:0 12px}
.check{width:64px;height:64px;border-radius:50%;background:var(--ok);display:flex;align-items:center;justify-content:center}
.steps{padding:16px;gap:12px;font-size:15px}.steps div{display:flex;gap:12px}.steps b.n{color:var(--acc)}
.switch{position:relative;width:51px;height:31px;border:0;border-radius:16px;background:var(--edge);cursor:pointer;padding:0;flex:none}
.switch span{position:absolute;top:2px;left:2px;width:27px;height:27px;border-radius:50%;background:#fff;box-shadow:0 1px 3px rgba(0,0,0,.25);transition:left .15s}
.switch.on{background:var(--ok)}.switch.on span{left:22px}.switch:disabled{opacity:.6;cursor:default}
)CSS"

// Shared helpers for both pages.
#define TM_JS R"JS(
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const j=(u,o)=>fetch(u,o).then(r=>r.ok?r.json():r.text().then(t=>{throw new Error(t||r.status)}));
const post=(u,b)=>j(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});
function txt(id,v){const e=$(id);if(e&&e.textContent!==String(v))e.textContent=v}
function bars(rssi){const n=rssi>-60?3:rssi>-75?2:1;let s='<svg width="20" height="16" viewBox="0 0 20 16" aria-label="Signal '+n+' of 3">';
 [[1,11,5],[6.5,7,9],[12,3,13]].forEach((b,i)=>s+=`<rect x="${b[0]}" y="${b[1]}" width="3.5" height="${b[2]}" rx="1" fill="currentColor" opacity="${i<n?1:.2}"/>`);return s+'</svg>'}
const LOCK='<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-label="Password protected"><rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/></svg>';
async function scanList(){const r=await j('/api/wifi/scan');return r.sort((a,b)=>b.rssi-a.rssi)}
)JS"

static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Transit Map setup</title><style>)HTML" TM_STYLE R"HTML(</style></head><body><main>

<section id="s-pick" class="grp" style="gap:16px;flex-grow:1">
 <div class="grp" style="padding:4px 4px 0"><h1 style="padding:0">Austin Live Transit Map</h1>
 <p class="lead" style="padding:0">Connect the map to Wi-Fi to show live bus and train locations.</p></div>
 <h2 class="cap" style="margin-top:8px">Choose network</h2>
 <div class="card"><div id="nets"><div class="row" style="color:var(--mut2)">Looking for networks…</div></div>
  <button class="row" style="color:var(--acc)" onclick="pickOther()">Other network…</button></div>
 <div style="display:flex;justify-content:space-between;align-items:center;padding:0 12px"><span class="note" style="padding:0">Only 2.4 GHz networks are listed.</span>
  <button class="link" style="padding:0;font-size:15px" onclick="scan()">Scan again</button></div>
 <div class="spacer"></div>
 <button class="btn2" style="font-size:16px" onclick="show('offline')">Set up without Wi-Fi</button>
</section>

<section id="s-pw" class="grp hide" style="gap:16px;flex-grow:1">
 <button class="link" onclick="show('pick')">‹ Networks</button>
 <h1 id="pwTitle"></h1>
 <div class="card field">
  <label for="ssid" class="oth">Network name</label><input id="ssid" class="oth" type="text" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false">
  <label for="pw">Password</label>
  <div style="display:flex;align-items:center;gap:8px;border:0"><input id="pw" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" style="border:0">
   <button class="link" style="padding:0 4px;font-size:15px" onclick="togglePw()" id="pwShow">Show</button></div>
 </div>
 <p id="pwErr" class="err hide"></p>
 <div class="spacer"></div>
 <button class="btn" onclick="connect()">Connect</button>
</section>

<section id="s-offline" class="grp hide" style="gap:18px;flex-grow:1">
 <button class="link" onclick="show('pick')">‹ Networks</button>
 <h1>Set up without Wi-Fi</h1>
 <p class="lead">Configure settings using the setup network instead.</p>
 <div class="card steps">
  <div><b class="n">1</b><span>The setup network <b class="apn"></b> stays on.</span></div>
  <div><b class="n">2</b><span>To change settings later, join <span class="apn"></span> and this page opens again.</span></div>
 </div>
 <p id="offErr" class="err hide"></p>
 <div class="spacer"></div>
 <button class="btn" onclick="offline()">Open settings</button>
</section>

<section id="s-connecting" class="center hide">
 <div class="spin"></div>
 <h1 style="font-size:22px">Connecting to <span class="net"></span>…</h1>
 <p class="lead">This usually takes about 10 seconds. Stay on this screen.</p>
</section>

<section id="s-done" class="grp hide" style="gap:18px;flex-grow:1;padding-top:28px">
 <div class="check"><svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg></div>
 <h1 style="padding:0">You're connected</h1>
 <p class="lead" style="padding:0">The map is on <b class="net" style="color:var(--fg)"></b>.</p>
 <div class="card" style="padding:16px;gap:6px">
  <div style="font-size:13px;color:var(--mut2)">To change settings later, join <span class="net"></span> and open</div>
  <div style="font-size:20px;font-weight:600" class="lname">transitmap.local</div>
  <div style="font-size:14px;color:var(--mut2)">or <span id="ip"></span></div>
 </div>
 <div class="spacer"></div>
 <p class="note" style="text-align:center">This closes the setup network. Hold MODE for 3 seconds to open it again.</p>
 <p id="finErr" class="err hide"></p>
 <button class="btn" onclick="finish()">Finish setup</button>
</section>

<section id="s-finished" class="center hide">
 <h1 style="font-size:24px">All set</h1>
 <p class="lead" id="finMsg">The setup network is off and your phone will return to your Wi-Fi. You can close this window.</p>
</section>

</main><script>)HTML" TM_JS R"HTML(
let net='',other=false,NETS=[];
function show(n){for(const s of document.querySelectorAll('main>section'))s.classList.toggle('hide',s.id!=='s-'+n);window.scrollTo(0,0)}
function setNet(n){net=n;document.querySelectorAll('.net').forEach(e=>e.textContent=n)}
function err(id,m){const e=$(id);e.textContent=m||'';e.classList.toggle('hide',!m)}
async function scan(){$('nets').innerHTML='<div class="row" style="color:var(--mut2)">Looking for networks…</div>';
 try{NETS=await scanList();
  $('nets').innerHTML=NETS.length?NETS.map((n,i)=>`<button class="row" onclick="pick(${i})"><span class="grow">${esc(n.ssid)}</span>${n.secure?'<span style="color:var(--mut2)">'+LOCK+'</span>':''}${bars(n.rssi)}</button>`).join(''):'<div class="row" style="color:var(--mut2)">No networks found</div>'}
 catch(e){$('nets').innerHTML='<div class="row" style="color:var(--bad)">Scan failed. Tap Scan again.</div>'}}
function pick(i){const n=NETS[i];other=false;setNet(n.ssid);if(!n.secure){join('');return}openPw(n.ssid)}
function pickOther(){other=true;setNet('');openPw('Other network')}
function openPw(t){$('pwTitle').textContent=t;$('pw').value='';$('ssid').value='';err('pwErr');
 document.querySelectorAll('.oth').forEach(e=>e.classList.toggle('hide',!other));show('pw')}
function togglePw(){const p=$('pw');p.type=p.type==='password'?'text':'password';$('pwShow').textContent=p.type==='password'?'Show':'Hide'}
function connect(){if(other){const s=$('ssid').value.trim();if(!s)return err('pwErr','Enter the network name.');setNet(s)}
 const pw=$('pw').value;if(pw.length<8)return err('pwErr','That password looks too short. Wi-Fi passwords are at least 8 characters.');join(pw)}
// The phone can drop off the hotspot for a moment while the board joins (the
// hotspot moves to the router's channel), so failed polls just retry.
async function join(pw){if(document.activeElement)document.activeElement.blur();show('connecting');
 try{await post('/api/wifi',{ssid:net,pass:pw})}catch(e){}
 const t0=Date.now();
 while(Date.now()-t0<30000){await new Promise(r=>setTimeout(r,2000));
  try{const s=await j('/api/status');if(s.wifi.connected&&s.wifi.ssid===net){$('ip').textContent=s.wifi.ip;show('done');return}}catch(e){}}
 const typed=other?net:'';openPw(other?'Other network':net);if(other)$('ssid').value=typed;
 err('pwErr',"Couldn't connect to "+net+'. Check the password and try again.')}
async function finish(){err('finErr');try{const r=await post('/api/complete-setup',{});
  if(r.hotspot_locked)$('finMsg').textContent='Setup is saved. This debug build keeps the setup network on.';show('finished')}
 catch(e){err('finErr',e.message)}}
async function offline(){err('offErr');try{await post('/api/settings',{mode:'simulated',setup_done:true});location.href='/'}catch(e){err('offErr',e.message)}}
j('/api/status').then(s=>{document.querySelectorAll('.apn').forEach(e=>e.textContent=s.wifi.hotspot_name);document.querySelectorAll('.lname').forEach(e=>e.textContent=s.wifi.local_name)}).catch(()=>{});
scan();
</script></body></html>)HTML";

static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Transit Map</title><style>)HTML" TM_STYLE R"HTML(</style></head><body><main>

<section id="v-home" class="grp" style="gap:22px">
 <div class="grp" style="gap:10px;padding:0 4px"><h1 style="padding:0;font-size:30px">Transit Map</h1>
  <div style="display:flex;align-items:center;gap:8px;font-size:15px;color:var(--mut)"><span id="dot" class="dot"></span><span><b id="modeName" style="color:var(--fg)"></b> · <span id="statusLine"></span></span></div></div>
 <div class="grp"><h2 class="cap">Brightness</h2><div class="seg" id="levels" style="grid-template-columns:repeat(3,minmax(0,1fr))"></div>
  <p class="note">Press MODE on the board to cycle brightness, then the same three with the whole line lit.</p></div>
 <div class="grp"><h2 class="cap">Style</h2><div class="seg" id="styles" style="grid-template-columns:repeat(2,minmax(0,1fr))"></div>
  <p class="note">Whole line lights each route in its color and shows vehicles as white dots.</p></div>
 <div class="grp"><h2 class="cap">Show</h2><div class="seg" id="modes" style="grid-template-columns:repeat(2,minmax(0,1fr))"></div>
  <p class="note" id="modeHelp"></p></div>
 <div class="grp"><h2 class="cap">Route colors</h2><div class="card" id="colors"></div>
  <button class="link" style="font-size:15px;padding:0 12px" onclick="resetColors()">Reset to CapMetro colors</button></div>
 <div class="card">
  <button class="row" onclick="go('veh')"><span class="grow">Vehicles by route</span><span class="val" id="vehVal"></span><span class="chev">›</span></button>
  <button class="row" onclick="go('wifi')"><span class="grow">Wi-Fi</span><span class="val" id="wifiVal"></span><span class="chev">›</span></button>
  <button class="row" onclick="go('fw')"><span class="grow">Firmware</span><span class="val" id="fwVal"></span><span class="chev">›</span></button>
  <button class="row" onclick="go('adv')"><span class="grow">Advanced</span><span class="chev">›</span></button>
 </div>
</section>

<section id="v-wifi" class="grp hide" style="gap:22px">
 <button class="link" onclick="go('home')">‹ Transit Map</button><h1>Wi-Fi</h1>
 <div id="wOn" class="grp" style="gap:22px">
  <div class="card">
   <div class="row"><span class="dot ok"></span><span class="grow" id="wSsid"></span><span class="val" style="font-size:15px">Connected</span></div>
   <div class="row kv"><span class="grow">Signal</span><span id="wSig"></span></div>
   <div class="row kv"><span class="grow">Address</span><span id="wAddr"></span></div>
  </div>
  <button class="link" style="font-size:15px;padding:0 12px" onclick="openJoin()">Change network</button>
 </div>
 <div id="wOff" class="grp" style="gap:22px">
  <div class="card" style="padding:16px;gap:6px"><div style="font-size:17px" id="wOffTitle">Not connected</div>
   <div style="font-size:14px;color:var(--mut2)" id="wOffText"></div></div>
  <button class="btn" onclick="openJoin()">Connect to Wi-Fi</button>
 </div>
 <div id="join" class="grp hide" style="gap:12px">
  <div class="card field">
   <label for="jNet">Network</label><select id="jNet" onchange="jPick()"><option value="">Looking for networks…</option></select>
   <label for="jSsid" class="jo hide">Network name</label><input id="jSsid" class="jo hide" type="text" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false">
   <label for="jPw">Password</label><input id="jPw" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false">
  </div>
  <p id="jMsg" class="note"></p>
  <div style="display:flex;gap:8px"><button class="btn2" onclick="closeJoin()">Cancel</button><button class="btn" onclick="joinNet()">Connect</button></div>
 </div>
 <div class="grp">
  <div class="card"><div class="row" style="min-height:56px"><span class="grow">Setup network</span>
   <button id="apSw" class="switch" role="switch" aria-label="Setup network" onclick="toggleAp()"><span></span></button></div></div>
  <p class="note" id="apHelp"></p><p class="err hide" id="apErr"></p>
 </div>
</section>

 <section id="v-veh" class="grp hide" style="gap:22px">
 <button class="link" onclick="go('home')">‹ Transit Map</button><h1>Vehicles by route</h1>
 <p class="lead" id="vehFeed"></p>
 <div class="card" style="overflow-x:auto"><table style="width:100%;border-collapse:collapse;font-size:15px">
  <thead><tr style="color:var(--mut2);font-size:12px;text-transform:uppercase;letter-spacing:.3px"><th style="text-align:left;padding:10px 8px 6px 16px">Route</th><th style="padding:10px 6px 6px">On map</th><th style="padding:10px 6px 6px">Hidden</th><th style="padding:10px 6px 6px">Off route</th><th style="padding:10px 6px 6px">Stale</th><th style="padding:10px 16px 6px 6px">No GPS</th></tr></thead>
  <tbody id="vehRows"></tbody></table></div>
 <div class="grp"><h2 class="cap">Not on the map</h2><div class="card" style="padding:14px 16px;gap:6px;font-size:15px" id="vehOther"></div></div>
 <p class="note">On map: lit on an LED. Hidden: on the 837 loop with no direction, so it isn't guessed. Off route: farther from the drawn route than the map's snap distance (400 m unless the map file sets it). Stale: no update for 2 minutes. No GPS: in the feed without a position. A route with all zeros usually means the feed uses a different route ID than the LED map; check the list above.</p>
</section>

<section id="v-fw" class="grp hide" style="gap:22px">
 <button class="link" onclick="go('home')">‹ Transit Map</button><h1>Firmware</h1>
 <div class="card"><div class="row"><span class="grow">Installed</span><span class="val" id="fwVer"></span></div></div>
 <label class="btn2" for="fwFile">Choose update file…</label><input id="fwFile" type="file" accept=".bin" class="hide" onchange="upload('/api/update','fwFile','fwMsg')">
 <p class="note" id="fwMsg">Pick the transitmap-…-ota.bin file. The map restarts when the update finishes. Your Wi-Fi, colors and LED map are kept.</p>
</section>

<section id="v-adv" class="grp hide" style="gap:22px">
 <button class="link" onclick="go('home')">‹ Transit Map</button><h1>Advanced</h1>
 <div class="grp"><h2 class="cap">Brightness levels</h2><div class="card">
  <div class="row kv"><span class="grow">Low</span><input class="num" id="b0" type="number" min="0.8" max="100" step="0.1" inputmode="decimal"> %</div>
  <div class="row kv"><span class="grow">Medium</span><input class="num" id="b1" type="number" min="0.8" max="100" step="0.1" inputmode="decimal"> %</div>
  <div class="row kv"><span class="grow">High</span><input class="num" id="b2" type="number" min="0.8" max="100" step="0.1" inputmode="decimal"> %</div>
  <div class="row kv"><span class="grow">Vehicles in whole line view</span><input class="num" id="lvp" type="number" min="100" max="400" step="10" inputmode="numeric"> %</div>
  <p class="note" style="padding:0 16px 8px">The lowest level is 0.8%. Below that the colors distort. Vehicles in whole line view are this much brighter than the line (100% = same).</p>
  <div class="row kv"><span class="grow">Night dimming</span><input id="nOn" type="checkbox" style="width:22px;height:22px"></div>
  <div class="row kv"><span class="grow">From</span><input class="num" style="width:110px" id="nS" type="time"> to <input class="num" style="width:110px" id="nE" type="time"></div>
  <div class="row kv"><span class="grow">Night brightness</span><input class="num" id="nP" type="number" min="0" max="100" inputmode="numeric"> %</div>
  <div class="row kv"><span class="grow">Power limit</span><input class="num" style="width:80px" id="ma" type="number" min="200" max="3000" step="100" inputmode="numeric"> mA</div>
 </div></div>
 <div class="grp"><h2 class="cap">Simulation</h2><div class="card" id="simRows"></div>
  <p class="note">Vehicles per hour in each direction, and driving speed. 0 per hour leaves a route empty.</p></div>
 <div class="grp"><h2 class="cap">Feed</h2><div class="card">
  <div class="row kv"><span class="grow">Check for vehicles every</span><input class="num" id="poll" type="number" min="30" max="60" inputmode="numeric"> s</div>
  <div class="field" style="border-bottom:1px solid var(--line)"><label for="url">Feed address</label><input id="url" type="url" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" style="font-size:14px"></div>
  <div class="row kv"><span class="grow">Connect timeout</span><input class="num" id="cs" type="number" min="5" max="60" inputmode="numeric"> s</div>
  <div class="row kv"><span class="grow">Read timeout</span><input class="num" id="rs" type="number" min="5" max="60" inputmode="numeric"> s</div>
 </div></div>
 <p id="advMsg" class="note"></p>
 <button class="btn" onclick="saveAdv()">Save</button>
 <div class="grp"><h2 class="cap">LED map</h2><div class="card">
  <div class="row kv"><span class="grow">Loaded</span><span id="mapInfo" style="color:var(--mut2);text-align:right"></span></div>
  <a class="row kv" href="/ledmap.json" download style="text-decoration:none"><span class="grow" style="color:var(--acc)">Download map file</span></a>
  <label class="row kv" for="mapFile" style="cursor:pointer"><span class="grow" style="color:var(--acc)">Replace map file…</span></label>
 </div><input id="mapFile" type="file" accept=".json" class="hide" onchange="upload('/api/ledmap','mapFile','mapMsg')"><p class="note" id="mapMsg"></p></div>
 <div class="grp"><h2 class="cap">Details</h2><div class="card">
  <div class="row kv"><span class="grow">Last feed</span><span id="dFeed" style="color:var(--mut2);text-align:right"></span></div>
  <div class="row kv"><span class="grow">Free memory</span><span id="dMem" style="color:var(--mut2)"></span></div>
  <div class="row kv"><span class="grow">Running for</span><span id="dUp" style="color:var(--mut2)"></span></div>
  <div class="row kv"><span class="grow">Clock</span><span id="dTime" style="color:var(--mut2)"></span></div>
 </div><p class="note">Hold IO0 on the board for 10 seconds to erase Wi-Fi and settings (the LED map is kept).</p></div>
 <div class="card"><div class="row kv"><span class="grow">Chain test speed</span><input class="num" id="ctMs" type="number" min="20" max="2000" step="10" inputmode="numeric" onchange="saveChainMs()"> ms per LED</div></div>
 <button class="btn2" id="ctBtn" onclick="toggleChain()">Run chain test</button>
</section>

</main><script>)HTML" TM_JS R"HTML(
let S=null,ST=null,view='home',lastMode='live',joinOpen=false;
const MODES={live:'Live',simulated:'Simulated',chain_test:'Chain test'};
function go(v){view=v;for(const s of document.querySelectorAll('main>section'))s.classList.toggle('hide',s.id!=='v-'+v);window.scrollTo(0,0);
 if(v==='adv')drawAdv();if(v==='wifi'){closeJoin();drawWifi()}draw()}
function ago(ms){if(ms==null)return'never';const s=Math.round(ms/1000);return s<90?s+' s ago':Math.round(s/60)+' min ago'}
function pct(v){const p=v*100/255;return p<10?Math.round(p*10)/10:Math.round(p)}
async function save(b){S=await post('/api/settings',b);draw();return S}

function draw(){if(!S||!ST)return;const s=ST,f=s.feed,on=s.wifi.connected;
 let dot='',line='';
 if(S.mode==='live'){if(!on){dot='warn';line=s.wifi.saved?'Waiting for Wi-Fi':'No Wi-Fi set up'}
  else if(f.ok){dot='ok';line=f.vehicles+' vehicles · updated '+ago(f.since_change_ms)}
  else{dot=f.error?'bad':'warn';line=f.error||'Waiting for the first update'}}
 else if(S.mode==='simulated'){dot='ok';line=f.vehicles+' simulated vehicles'}else{dot='warn';line='Lighting each chain in turn'}
 $('dot').className='dot '+dot;txt('modeName',MODES[S.mode]||S.mode);txt('statusLine',line);
 $('levels').innerHTML=['Low','Medium','High'].map((l,i)=>`<button class="${S.level===i?'on':''}" onclick="save({level:${i}})">${l}</button>`).join('');
 $('styles').innerHTML=[['Vehicles only',false],['Whole line',true]].map(([l,v])=>`<button class="${!!S.line_view===v?'on':''}" onclick="save({line_view:${v}})">${l}</button>`).join('');
 $('modes').innerHTML=[['live','Live'],['simulated','Simulated']].map(([m,l])=>`<button class="${S.mode===m?'on':''}" onclick="save({mode:'${m}'})">${l}</button>`).join('');
 txt('modeHelp',S.mode==='live'?(on?'Vehicle locations are shown based on live GTFS data.':'Needs Wi-Fi. Connect the map to Wi-Fi first.')
  :S.mode==='simulated'?'Vehicle locations are simulated. Configure in Advanced Settings.':'Chain test is running. Stop it in Advanced.');
 txt('vehVal',f.vehicles+' on map');txt('wifiVal',on?s.wifi.ssid:(s.wifi.saved?'Not connected':'Not set up'));txt('fwVal',s.version);txt('fwVer',s.version);
 $('ctBtn').textContent=S.mode==='chain_test'?'Stop chain test':'Run chain test';
 if(view==='wifi'&&!joinOpen)drawWifi();if(view==='adv')drawDetails();if(view==='veh')drawVeh()}

function drawVeh(){const s=ST,f=s.feed;
 txt('vehFeed',S.mode==='simulated'?'Simulated vehicles.':S.mode==='live'?(f.ok?'Live feed, updated '+ago(f.since_change_ms)+'. '+f.vehicles+' vehicles on the board\'s routes.':'Live feed: '+(f.error||'waiting for the first update')):'Chain test is running.');
 $('vehRows').innerHTML=s.routes.map(r=>`<tr style="border-top:1px solid var(--line)"><td style="padding:10px 8px 10px 16px;white-space:nowrap"><span class="dot" style="display:inline-block;background:${r.color};margin-right:8px"></span><b>${esc(r.id)}</b></td>`+
  [r.lit,r.hidden,r.off_route,r.stale,r.no_position].map((v,i)=>`<td style="text-align:center;padding:10px 6px;${i==4?'padding-right:16px;':''}color:${v?'var(--fg)':'var(--mut2)'};font-weight:${i==0&&v?600:400}">${v??0}</td>`).join('')+'</tr>').join('');
 const ids=f.other_route_ids||[];
 $('vehOther').innerHTML=S.mode!=='live'?'<span style="color:var(--mut2)">Only shown for the live feed.</span>'
  :`<div>${f.other_routes} vehicles on routes the map doesn't show${f.no_route_id?' ('+f.no_route_id+' with no route ID)':''}.</div>`+
   (f.dropped?`<div style="color:var(--warn,#c60)">${f.dropped} more vehicles than the board holds (64) were skipped.</div>`:'')+
   (ids.length?`<div style="color:var(--mut2)">Route IDs: ${ids.map(esc).join(', ')}${ids.length>=24?', …':''}</div>`:'')}
function drawColors(){
 $('colors').innerHTML=ST.routes.map(r=>`<label class="row" style="cursor:pointer"><span style="font-size:15px;font-weight:600;width:40px">${esc(r.id)}</span><span class="grow" style="font-size:15px;color:var(--mut)">${esc(r.name)}</span><input type="color" id="c_${esc(r.id)}" value="${r.color}" aria-label="Color for route ${esc(r.id)}" onchange="saveColors()"></label>`).join('')}
async function saveColors(){const c={};for(const r of ST.routes){const e=$('c_'+r.id);c[r.id]=e.value.toLowerCase()!==r.brand.toLowerCase()?e.value:''}
 await save({colors:c});ST=await j('/api/status')}
async function resetColors(){const c={};for(const r of ST.routes)c[r.id]='';await save({colors:c});ST=await j('/api/status');drawColors()}

function drawWifi(){const w=ST.wifi,on=w.connected;$('wOn').classList.toggle('hide',!on||joinOpen);$('wOff').classList.toggle('hide',on||joinOpen);
 txt('wSsid',w.ssid);txt('wSig',(w.rssi>-60?'Good':w.rssi>-75?'Fair':'Weak')+' ('+w.rssi+' dBm)');txt('wAddr',w.local_name+' · '+w.ip);
 txt('wOffTitle',w.saved?'Not connected to '+w.saved:'Not connected');
 txt('wOffText',S.mode==='simulated'?'The map is showing simulated vehicles. Connect it to Wi-Fi any time to switch to live vehicles.':'Connect the map to Wi-Fi to show live vehicles.');
 const sw=$('apSw');sw.classList.toggle('on',w.hotspot);sw.setAttribute('aria-checked',w.hotspot);sw.disabled=w.hotspot&&(!on||w.hotspot_locked);
 txt('apHelp',w.hotspot_locked?w.hotspot_name+' is always on in this debug build.'
  :!on?'Stays on while the map has no Wi-Fi, so you can always get back to this page by joining '+w.hotspot_name+'.'
  :w.hotspot?w.hotspot_name+' is on. Phones can join it to reach this page without your Wi-Fi.'
  :'Off. Turn it on here or hold MODE for 3 seconds. It also turns on by itself if Wi-Fi is down for 5 minutes.')}
async function toggleAp(){const e=$('apErr');e.classList.add('hide');
 try{await post('/api/hotspot',{on:!ST.wifi.hotspot});ST.wifi.hotspot=!ST.wifi.hotspot;drawWifi()}catch(x){e.textContent=x.message;e.classList.remove('hide')}}
async function openJoin(){joinOpen=true;$('join').classList.remove('hide');drawWifi();$('jMsg').textContent='';
 try{const r=await scanList();$('jNet').innerHTML='<option value="">Choose…</option>'+r.map(n=>`<option>${esc(n.ssid)}</option>`).join('')+'<option value="*">Other network…</option>'}
 catch(e){$('jNet').innerHTML='<option value="*">Other network…</option>'}jPick()}
function jPick(){const o=$('jNet').value==='*';document.querySelectorAll('.jo').forEach(e=>e.classList.toggle('hide',!o))}
function closeJoin(){joinOpen=false;$('join').classList.add('hide');drawWifi()}
async function joinNet(){const ssid=$('jNet').value==='*'?$('jSsid').value.trim():$('jNet').value;if(!ssid){$('jMsg').textContent='Choose a network first.';return}
 try{await post('/api/wifi',{ssid,pass:$('jPw').value});if(document.activeElement)document.activeElement.blur();
  $('jMsg').textContent='Joining '+ssid+'… This takes about 10 seconds.';setTimeout(()=>{closeJoin();refresh()},12000)}catch(e){$('jMsg').textContent=e.message}}

function simOf(id){const r=S.sim&&S.sim.routes&&S.sim.routes[id];return r||{per_hour:4,mph:15}}
function drawAdv(){if(!S||!ST)return;for(let k=0;k<3;k++)$('b'+k).value=pct(S.brightness[k]);
 $('nOn').checked=S.night.enabled;$('nS').value=S.night.start;$('nE').value=S.night.end;$('nP').value=S.night.percent;$('ma').value=S.max_ma;$('lvp').value=S.line_vehicle_pct;
 $('simRows').innerHTML=ST.routes.map(r=>{const v=simOf(r.id);return `<div class="row kv"><span class="grow"><b>${esc(r.id)}</b> <span style="color:var(--mut2)">${esc(r.name)}</span></span><input class="num" style="width:52px" id="sh_${esc(r.id)}" type="number" min="0" max="30" step="0.5" inputmode="decimal" value="${v.per_hour}" aria-label="Vehicles per hour, route ${esc(r.id)}">/h <input class="num" style="width:52px" id="sm_${esc(r.id)}" type="number" min="0" max="80" inputmode="numeric" value="${v.mph}" aria-label="Speed, route ${esc(r.id)}">mph</div>`}).join('')
  +`<div class="row kv"><span class="grow">Speed-up</span><input class="num" id="ts" type="number" min="1" max="60" step="1" inputmode="numeric" value="${S.sim.time_scale}">× real time</div>`;
 $('ctMs').value=S.chain_ms;$('poll').value=S.feed.poll_s;$('url').value=S.feed.url;$('cs').value=S.feed.connect_s;$('rs').value=S.feed.read_s;
 $('advMsg').textContent='';drawDetails()}
function drawDetails(){const s=ST,f=s.feed;
 txt('mapInfo',s.map.error?'Not loaded: '+s.map.error:s.map.routes+' routes, '+s.map.placed+' of '+s.map.total+' LEDs');
 txt('dFeed',S.mode!=='live'?'Not in use':f.ok?'OK, '+(f.fetch_ms/1000).toFixed(1)+' s, '+ago(f.since_change_ms):(f.error||'none yet'));
 txt('dMem',Math.round(s.heap/1024)+' KB, largest block '+Math.round(s.heap_max/1024)+' KB');const m=Math.round(s.uptime_s/60);txt('dUp',m<60?m+' min':Math.floor(m/60)+' h '+(m%60)+' min');txt('dTime',s.time||'Not set yet')}
async function saveAdv(){const sim={};for(const r of ST.routes)sim[r.id]={per_hour:+$('sh_'+r.id).value,mph:+$('sm_'+r.id).value};
 try{await save({brightness:[0,1,2].map(k=>Math.max(2,Math.round(+$('b'+k).value*255/100))),
  night:{enabled:$('nOn').checked,start:$('nS').value,end:$('nE').value,percent:+$('nP').value},max_ma:+$('ma').value,line_vehicle_pct:+$('lvp').value,
  sim:{time_scale:+$('ts').value,routes:sim},
  feed:{poll_s:+$('poll').value,url:$('url').value,connect_s:+$('cs').value,read_s:+$('rs').value}});
  if(document.activeElement)document.activeElement.blur();drawAdv();$('advMsg').innerHTML='<span class="okt">Saved.</span>'}catch(e){$('advMsg').innerHTML='<span class="err" style="padding:0">'+esc(e.message)+'</span>'}}
async function saveChainMs(){const v=Math.min(2000,Math.max(20,Math.round(+$('ctMs').value||120)));$('ctMs').value=v;try{await save({chain_ms:v})}catch(e){}}
async function toggleChain(){if(S.mode==='chain_test')await save({mode:lastMode==='chain_test'?'live':lastMode});else{lastMode=S.mode;await save({mode:'chain_test'})}}
function upload(url,id,msg){const f=$(id).files[0];if(!f)return;const fd=new FormData();fd.append('file',f,f.name);$(msg).textContent='Uploading…';
 fetch(url,{method:'POST',body:fd}).then(r=>r.text().then(t=>{$(msg).textContent=t;if(r.ok)setTimeout(()=>location.reload(),8000)})).catch(()=>$(msg).textContent='Lost the connection to the board before it answered. Wait for it to reconnect, reload this page and check whether the change took.').finally(()=>$(id).value='')}

// Refresh only on screens without text fields (see the note at the top of web_page.h).
async function refresh(){if(document.hidden||view==='adv'||(view==='wifi'&&joinOpen))return;try{ST=await j('/api/status');draw()}catch(e){}}
(async()=>{try{S=await j('/api/settings');ST=await j('/api/status');if(S.mode!=='chain_test')lastMode=S.mode;drawColors();draw()}catch(e){txt('statusLine','Could not reach the map: '+e.message)}})();
setInterval(refresh,5000);
</script></body></html>)HTML";
