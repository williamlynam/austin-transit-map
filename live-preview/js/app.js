/* Live Preview: feed -> Engine (the firmware's rules) -> board LEDs and the street map. */
(function () {
  'use strict';

  const $ = id => document.getElementById(id);
  const NS = 'http://www.w3.org/2000/svg';
  const svgEl = (name, attrs, parent) => {
    const e = document.createElementNS(NS, name);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  };
  const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const CAPMETRO_PB = 'https://data.texas.gov/download/eiei-9rpf/application%2Foctet-stream';
  const served = location.protocol === 'http:' || location.protocol === 'https:';

  // ---- settings (per browser) ----
  const UI_KEY = 'livepreview.ui.v1', MAP_KEY = 'livepreview.map.v1';
  const load = k => { try { return JSON.parse(localStorage.getItem(k)); } catch (e) { return null; } };
  const save = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* storage off */ } };
  const settings = Object.assign({ src: 'demo', url: served ? '/feed' : CAPMETRO_PB, pollS: 30, hyst: true, showOff: true, showAll: false }, load(UI_KEY) || {});
  // A URL saved while opened as a file means nothing once served by serve.py, and vice versa.
  if (served && settings.url === CAPMETRO_PB) settings.url = '/feed';
  if (!served && settings.url === '/feed') settings.url = CAPMETRO_PB;
  settings.pollS = Math.max(30, Math.min(60, +settings.pollS || 30)); // the firmware polls every 30–60 s
  const saveUi = () => save(UI_KEY, settings);

  // ---- state ----
  let ledmap = null, engine = null, demo = null, result = null, isSample = false;
  let lastRaw = null, timer = null, paused = false, selVid = null, selRef = null;
  let replay = { files: [], i: -1 };

  function toast(msg) {
    const t = $('toast'); t.textContent = msg; t.hidden = false;
    clearTimeout(toast.h); toast.h = setTimeout(() => { t.hidden = true; }, 3500);
  }

  // ---- colours ----
  const hexRgb = h => { const m = /^#?([0-9a-f]{6})$/i.exec(h || ''); const n = m ? parseInt(m[1], 16) : 0x888888; return [n >> 16, (n >> 8) & 255, n & 255]; };
  const scale = (c, f) => c.map(x => x * f);
  // One colour per route, whatever the direction.
  const routeRgb = route => hexRgb(colorOf(route));

  // ---- map loading ----
  function useMap(doc, sample) {
    let m;
    try { m = new LedMap(doc); } catch (e) { toast('Could not use that map: ' + e.message); return false; }
    ledmap = m; isSample = !!sample;
    engine = new Engine(ledmap, Geo, { hysteresis: settings.hyst });
    demo = null; result = null; selVid = null; selRef = null; $('ledInfo').hidden = true;
    if (!sample) save(MAP_KEY, doc);
    $('mapName').textContent = (doc.board || '') + (sample ? ' · sample map' : '') + (doc.updated ? ' · saved ' + doc.updated.slice(0, 10) : '');
    buildBoard(); buildMapLayers(); renderRoutes(); renderVehicles(); showBanner();
    restart();
    return true;
  }

  function showBanner() {
    const b = $('banner'), msgs = [];
    if (isSample) msgs.push('This is the synthetic sample map with made-up LED positions. Open your own ledmap.json from the LED Mapper to see the real board.');
    const unplaced = ledmap.routes.map(r => [r, ledmap.progress(r)]).filter(([, p]) => p.placed < p.total);
    if (unplaced.length) msgs.push('Some LEDs have no position yet, so buses near them snap to the nearest placed neighbour: ' + unplaced.map(([r, p]) => `${r.route_id} ${p.placed}/${p.total}`).join(', ') + '.');
    const undirected = ledmap.routes.filter(r => ledmap.paths(r).length > 1 && ledmap.paths(r).some(p => p.direction_id == null));
    if (undirected.length) msgs.push(`Directions aren't matched to GTFS for ${undirected.map(r => r.route_id).join(', ')}, so those buses use bearing to pick a side of the loop.`);
    b.textContent = msgs.join(' ');
    b.hidden = !msgs.length;
  }

  // ---- board ----
  const board = { svg: $('board'), nodes: new Map(), shown: new Map(), view: null, full: null, ring: null };

  function buildBoard() {
    const svg = board.svg, art = window.LEDMAP_BOARD;
    svg.textContent = '';
    const defs = svgEl('defs', {}, svg);
    const f = svgEl('filter', { id: 'blur', x: '-1', y: '-1', width: '3', height: '3' }, defs);
    svgEl('feGaussianBlur', { stdDeviation: '1.1' }, f);
    board.full = { x: art.x0, y: art.y0, w: art.x1 - art.x0, h: art.y1 - art.y0 };
    if (!board.view) board.view = Object.assign({}, board.full);
    svgEl('image', { href: art.image, x: art.x0, y: art.y0, width: board.full.w, height: board.full.h, preserveAspectRatio: 'none' }, svg);
    svgEl('rect', { x: art.x0, y: art.y0, width: board.full.w, height: board.full.h, class: 'dimmer' }, svg);
    const halos = svgEl('g', {}, svg), pxs = svgEl('g', {}, svg);
    board.nodes.clear(); board.shown.clear();
    for (const r of ledmap.routes) {
      for (const led of r.leds) {
        const halo = svgEl('circle', { cx: led.x_mm, cy: led.y_mm, r: 3.4, class: 'halo', filter: 'url(#blur)', opacity: 0 }, halos);
        const px = svgEl('circle', { cx: led.x_mm, cy: led.y_mm, r: 1.15, class: 'px', 'data-ref': led.ref }, pxs);
        const title = svgEl('title', {}, px);
        px.addEventListener('click', e => { e.stopPropagation(); pickLed(led.ref); });
        board.nodes.set(led.ref, { halo, px, title, led, route: r });
        board.shown.set(led.ref, [0, 0, 0]);
      }
    }
    board.ring = svgEl('circle', { r: 2.6, class: 'selring', visibility: 'hidden' }, svg);
    applyView();
  }

  function applyView() { const v = board.view; board.svg.setAttribute('viewBox', `${v.x} ${v.y} ${v.w} ${v.h}`); }

  (function bindPanZoom() {
    const svg = board.svg;
    const toBoard = (cx, cy) => {
      const r = svg.getBoundingClientRect(), v = board.view;
      const s = Math.max(v.w / r.width, v.h / r.height);
      const ox = (r.width * s - v.w) / 2, oy = (r.height * s - v.h) / 2;
      return [v.x - ox + (cx - r.left) * s, v.y - oy + (cy - r.top) * s, s];
    };
    svg.addEventListener('wheel', e => {
      e.preventDefault();
      const [bx, by] = toBoard(e.clientX, e.clientY), v = board.view;
      const w = Math.min(board.full.w * 1.2, Math.max(8, v.w * Math.exp(e.deltaY * 0.0015))), k = w / v.w;
      board.view = { x: bx - (bx - v.x) * k, y: by - (by - v.y) * k, w, h: v.h * k };
      applyView();
    }, { passive: false });
    let drag = null;
    svg.addEventListener('pointerdown', e => {
      if (e.target.dataset && e.target.dataset.ref) return;
      drag = { x: e.clientX, y: e.clientY, v: Object.assign({}, board.view) };
      svg.setPointerCapture(e.pointerId);
    });
    svg.addEventListener('pointermove', e => {
      if (!drag) return;
      const s = toBoard(0, 0)[2];
      board.view = Object.assign({}, drag.v, { x: drag.v.x - (e.clientX - drag.x) * s, y: drag.v.y - (e.clientY - drag.y) * s });
      applyView();
    });
    const end = () => { drag = null; };
    svg.addEventListener('pointerup', end);
    svg.addEventListener('pointercancel', end);
    svg.addEventListener('dblclick', () => { board.view = Object.assign({}, board.full); applyView(); });
  })();

  /* §5: one vehicle = route colour; several from one route = brighter. On a
   * shared LED (D10/D23) the chain's own route wins: the train always shows on
   * top of a bus, and the bus shows only when no train is there. */
  const SINGLE = 0.7;
  function target(ref) {
    const occ = result && result.leds.get(ref);
    if (!occ || !occ.length) return [0, 0, 0];
    const owner = board.nodes.get(ref).route;
    const on = occ.some(v => v.route === owner) ? owner : occ[0].route;
    const n = occ.filter(v => v.route === on).length;
    return scale(routeRgb(on), n > 1 ? 1 : SINGLE);
  }

  // §5 motion: an LED changing colour fades over ~1.5 s rather than jumping.
  let lastFrame = 0;
  function frame(t) {
    requestAnimationFrame(frame);
    if (!ledmap || t - lastFrame < 33) return;
    const dt = Math.min(0.2, (t - lastFrame) / 1000); lastFrame = t;
    const step = 255 * dt / 1.5;
    for (const [ref, node] of board.nodes) {
      const tg = target(ref), cur = board.shown.get(ref);
      for (let i = 0; i < 3; i++) cur[i] += Math.max(-step, Math.min(step, tg[i] - cur[i]));
      const lvl = Math.max(cur[0], cur[1], cur[2]) / 255;
      const fill = cur.map(x => Math.round(24 + x * (231 / 255)));
      node.px.setAttribute('fill', `rgb(${fill})`);
      if (lvl > 0.02) {
        const hc = cur.map(x => Math.round(Math.min(255, x / Math.max(lvl, 0.01))));
        node.halo.setAttribute('fill', `rgb(${hc})`);
        node.halo.setAttribute('opacity', Math.min(1, lvl * 1.1).toFixed(2));
      } else node.halo.setAttribute('opacity', 0);
    }
  }
  requestAnimationFrame(frame);

  function updateBoardTitles() {
    for (const [ref, node] of board.nodes) {
      const occ = (result && result.leds.get(ref)) || [];
      node.title.textContent = `${ref} · ${node.route.route_id} ${node.route.chain}[${node.led.i}]` +
        (occ.length ? '\n' + occ.map(v => `${v.route.route_id} ${v.label || v.id}${v.dir != null ? ' dir ' + v.dir : ''}`).join('\n') : '');
    }
    placeRing();
  }

  function placeRing() {
    const v = selVid && result && result.vehicles.find(x => x.id === selVid);
    const ref = (v && v.ref) || selRef;
    const node = ref && board.nodes.get(ref);
    if (!node) { board.ring.setAttribute('visibility', 'hidden'); return; }
    board.ring.setAttribute('cx', node.led.x_mm); board.ring.setAttribute('cy', node.led.y_mm);
    board.ring.setAttribute('visibility', 'visible');
  }

  function showLedInfo(ref) {
    const node = board.nodes.get(ref), occ = (result && result.leds.get(ref)) || [];
    const borrowers = ledmap.routes.filter(r => r !== node.route && (r.shared || []).some(s => s.ref === ref));
    let html = `<b>${esc(ref)}</b> · ${esc(node.route.chain)}[${node.led.i}] · ${esc(node.route.route_id)} ${esc(node.route.name || '')}`;
    if (borrowers.length) html += `<br>Also shows ${borrowers.map(r => esc(r.route_id)).join(', ')}`;
    html += '<br>' + (occ.length ? occ.map(v => `${esc(v.route.route_id)} ${esc(v.label || v.id)} · dir ${v.dir == null ? '?' : v.dir} · ${Math.round(v.d)} m`).join('<br>') : 'Dark: no vehicle snaps here.');
    const info = $('ledInfo'); info.innerHTML = html; info.hidden = false;
    return occ;
  }

  function pickLed(ref) {
    selRef = ref; selVid = null;
    const occ = showLedInfo(ref);
    if (occ.length) selectVehicle(occ[0].id, true);
    else { placeRing(); renderVehicles(); updateMarkers(); }
  }

  function clearSelection() { selVid = null; selRef = null; $('ledInfo').hidden = true; placeRing(); }

  // ---- street map ----
  const lmap = L.map('map', { zoomControl: true }).setView([30.33, -97.72], 11);
  const esri = (svc, label) => L.tileLayer(`https://server.arcgisonline.com/ArcGIS/rest/services/${svc}/MapServer/tile/{z}/{y}/{x}`,
    { maxZoom: 19, attribution: 'Tiles &copy; Esri &mdash; ' + label });
  const bases = {
    'Light gray (Esri)': esri('Canvas/World_Light_Gray_Base', 'Esri, HERE, Garmin, OpenStreetMap contributors'),
    'Streets (Esri)': esri('World_Street_Map', 'Esri, HERE, Garmin, OpenStreetMap contributors'),
    'Satellite (Esri)': esri('World_Imagery', 'Esri, Maxar, Earthstar Geographics')
  };
  (bases[settings.base] || bases['Light gray (Esri)']).addTo(lmap);
  L.control.layers(bases, null, { position: 'topright' }).addTo(lmap);
  lmap.on('baselayerchange', e => { settings.base = e.name; saveUi(); });
  const routeLayer = L.layerGroup().addTo(lmap), linkLayer = L.layerGroup().addTo(lmap), vehLayer = L.layerGroup().addTo(lmap);
  const ledDots = new Map(); // `${route_id}|${ref}` -> circleMarker
  const markers = new Map(); // vehicle id -> marker

  function buildMapLayers() {
    routeLayer.clearLayers(); linkLayer.clearLayers(); vehLayer.clearLayers(); ledDots.clear(); markers.clear();
    const all = [];
    for (const r of ledmap.routes) {
      const c = colorOf(r);
      ledmap.paths(r).forEach((p, n) => {
        const pts = ledmap.points(r, p.seq).filter(e => e.lat != null && !e.skip).map(e => [e.lat, e.lon]);
        if (pts.length > 1) L.polyline(pts, { color: c, weight: 4, opacity: 0.55, dashArray: n ? '6 6' : null, interactive: false }).addTo(routeLayer);
      });
      for (const ref of ledmap.sequence(r)) {
        const e = ledmap.entry(r, ref);
        if (!e || e.lat == null) continue;
        all.push([e.lat, e.lon]);
        const dot = L.circleMarker([e.lat, e.lon], { radius: 3.5, color: '#fff', weight: 1, fillColor: c, fillOpacity: 1, className: 'led-dot' })
          .bindTooltip(`${ref}${e.label ? ' · ' + esc(e.label) : ''} <span style="color:#888">(${esc(r.route_id)})</span>`, { direction: 'top' })
          .on('click', () => pickLed(ref))
          .addTo(routeLayer);
        ledDots.set(r.route_id + '|' + ref, dot);
      }
    }
    if (all.length) lmap.fitBounds(L.latLngBounds(all).pad(0.05));
  }

  function vehIcon(v) {
    const cls = ['veh-icon', v.state === 'off route' ? 'off' : '', v.state === 'stale' ? 'stale' : '', v.state === 'hidden' ? 'hidden' : '', v.id === selVid ? 'sel' : ''].join(' ');
    const c = `rgb(${routeRgb(v.route).map(Math.round)})`;
    const arrow = v.bearing != null ? `<div class="arrow" style="--b:${v.bearing}deg"></div>` : '';
    return L.divIcon({ className: cls, iconSize: [22, 22], iconAnchor: [11, 11], html: `<div style="--c:${c}">${arrow}<div class="body"></div></div>` });
  }

  function describe(v) {
    const dir = v.dir == null ? 'no direction_id' : 'direction ' + v.dir;
    const parts = [`<b>${esc(v.route.route_id)}</b> ${esc(v.label || v.id)}`, `${dir}${v.bearing != null ? ', bearing ' + Math.round(v.bearing) + '°' : ''}`];
    if (v.state === 'lit') parts.push(`Lights <b>${esc(v.ref)}</b>${v.borrowed ? ` (on ${esc(v.chain)}[${v.index}])` : ''}, ${Math.round(v.d)} m from the path` + (v.pathName ? `<br>Path: ${esc(v.pathName)} (by ${esc(v.how)})` : '') + (v.held ? `<br>Held by hysteresis (raw snap ${esc(v.heldFrom)})` : ''));
    else if (v.state === 'hidden') parts.push(`Hidden: nearest LED ${esc(v.ref)} is on one side of the loop and the direction is unknown`);
    else if (v.state === 'off route') parts.push(`Off route: outside every segment's corridor (${ledmap.doc.max_snap_m || 400} m, or ${Math.round((ledmap.doc.snap_fraction != null ? ledmap.doc.snap_fraction : 0.3) * 100)}% of a long gap's length)`);
    else if (v.state === 'stale') parts.push(`Stale: position is ${Math.round(v.age)} s old`);
    else parts.push('No position');
    if (v.age != null && v.state !== 'stale') parts.push(`Position ${Math.round(v.age)} s old`);
    return parts.join('<br>');
  }

  function updateMarkers() {
    if (!result) return;
    linkLayer.clearLayers();
    const keep = new Set();
    for (const v of result.vehicles) {
      if (v.lat == null || v.state === 'no position') continue;
      if ((v.state === 'off route' || v.state === 'stale') && !settings.showOff) continue;
      keep.add(v.id);
      let m = markers.get(v.id);
      if (!m) {
        m = L.marker([v.lat, v.lon], { keyboard: false }).addTo(vehLayer);
        m.on('click', () => selectVehicle(m.vid, false));
        m.bindTooltip('', { direction: 'top', offset: [0, -10] });
        markers.set(v.id, m);
      }
      m.vid = v.id;
      m.setLatLng([v.lat, v.lon]).setIcon(vehIcon(v)).setZIndexOffset(v.id === selVid ? 1000 : v.state === 'lit' ? 100 : 0);
      m.setTooltipContent(describe(v));
      if (v.state === 'lit' || v.state === 'hidden') {
        const e = ledmap.entry(v.route, v.ref);
        if (e && e.lat != null) L.polyline([[v.lat, v.lon], [e.lat, e.lon]], { color: colorOf(v.route), weight: v.id === selVid ? 3 : 1.5, dashArray: '3 4', opacity: 0.9, interactive: false }).addTo(linkLayer);
      }
    }
    for (const [id, m] of markers) if (!keep.has(id)) { vehLayer.removeLayer(m); markers.delete(id); }
    // LED dots on the street map light up like the board.
    for (const [key, dot] of ledDots) {
      const [rid, ref] = key.split('|');
      const lit = (result.leds.get(ref) || []).some(v => v.route.route_id === rid);
      dot.setRadius(lit ? 7 : 3.5);
      dot.setStyle({ color: lit ? '#000' : '#fff', weight: lit ? 2 : 1 });
    }
  }

  function selectVehicle(id, fromBoard) {
    selVid = id;
    const v = result && result.vehicles.find(x => x.id === id);
    if (!fromBoard) { selRef = null; $('ledInfo').hidden = true; }
    if (v && v.lat != null) {
      const m = markers.get(id);
      if (!lmap.getBounds().pad(-0.1).contains([v.lat, v.lon])) lmap.panTo([v.lat, v.lon]);
      if (m) m.openTooltip();
    }
    placeRing(); renderVehicles(); updateMarkers();
  }

  // ---- side panel ----
  function renderRoutes() {
    const ul = $('routeList'); ul.textContent = '';
    for (const r of ledmap.routes) {
      const vs = result ? result.vehicles.filter(v => v.route === r) : [];
      const n = s => vs.filter(v => v.state === s).length;
      const p = ledmap.progress(r);
      const notes = [];
      if (result) {
        if (n('hidden')) notes.push(`${n('hidden')} hidden`);
        if (n('off route')) notes.push(`${n('off route')} off route`);
        if (n('stale')) notes.push(`${n('stale')} stale`);
      }
      if (p.placed < p.total) notes.push(`<span class="warn">${p.placed}/${p.total} placed</span>`);
      const li = document.createElement('li');
      li.innerHTML = `<span class="sw" style="background:${esc(colorOf(r))}"></span><span class="rname">${esc(r.route_id)} ${esc(r.name || '')}<small>${esc(r.chain)}${notes.length ? ' · ' + notes.join(', ') : ''}</small></span><span class="cnt" title="Vehicles lighting an LED">${result ? n('lit') : '–'}</span>`;
      ul.appendChild(li);
    }
  }

  const ORDER = { lit: 0, hidden: 1, 'off route': 2, stale: 3, 'no position': 4 };
  function renderVehicles() {
    const ul = $('vehList'); ul.textContent = '';
    if (!result) { $('vehCount').textContent = ''; ul.innerHTML = '<li class="empty">No feed yet.</li>'; return; }
    const vs = result.vehicles.filter(v => settings.showAll || v.state === 'lit')
      .sort((a, b) => ledmap.routes.indexOf(a.route) - ledmap.routes.indexOf(b.route) || ORDER[a.state] - ORDER[b.state] || String(a.label || a.id).localeCompare(String(b.label || b.id), undefined, { numeric: true }));
    $('vehCount').textContent = `${result.vehicles.filter(v => v.state === 'lit').length} lit of ${result.vehicles.length}`;
    if (!vs.length) { ul.innerHTML = `<li class="empty">${result.vehicles.length ? 'None lit right now.' : 'No vehicles on the board\'s routes in this feed.'}</li>`; return; }
    for (const v of vs) {
      const li = document.createElement('li');
      if (v.id === selVid) li.className = 'sel';
      else if (v.state !== 'lit') li.className = 'dim';
      const where = v.state === 'lit' ? `${esc(v.ref)}${v.borrowed ? '*' : ''} · ${Math.round(v.d)} m` : esc(v.state);
      li.innerHTML = `<span class="dot" style="background:rgb(${routeRgb(v.route).map(Math.round)})"></span><span class="rt">${esc(v.route.route_id)}</span><span class="id">${esc(v.label || v.id)}<span class="muted"> · ${v.dir == null ? 'dir ?' : 'dir ' + v.dir}</span></span><span class="where">${where}</span>`;
      li.title = describe(v).replace(/<br>/g, '\n').replace(/<[^>]+>/g, '');
      li.addEventListener('click', () => selectVehicle(v.id, false));
      ul.appendChild(li);
    }
  }

  function renderOther() {
    const sec = $('otherSection'), box = $('otherList');
    const entries = result ? [...result.otherRoutes.entries()].sort((a, b) => a[0].localeCompare(b[0], undefined, { numeric: true })) : [];
    sec.hidden = !entries.length;
    box.innerHTML = entries.map(([k, n]) => `<span>${esc(k)} ×${n}</span>`).join('');
  }

  const hhmmss = s => s == null ? '–' : new Date(s * 1000).toLocaleTimeString();
  function setStatus(html, kind) { const s = $('feedStatus'); s.innerHTML = html; s.className = 'status' + (kind ? ' ' + kind : ''); }

  // ---- feed handling ----
  function apply(feed, nowS, label) {
    engine.opts.hysteresis = settings.hyst;
    result = engine.update(feed, nowS);
    const onBoard = result.vehicles.length, lit = result.vehicles.filter(v => v.state === 'lit').length;
    const age = feed.t != null && nowS != null ? Math.round(nowS - feed.t) : null;
    setStatus(`${label}<br>Feed time <b>${hhmmss(feed.t)}</b>${age != null && settings.src === 'live' ? ` (${age} s old)` : ''}<br>` +
      `<b>${feed.vehicles.length}</b> vehicles, <b>${onBoard}</b> on the board's routes, <b>${lit}</b> lighting an LED` +
      (feed.format && feed.format !== 'demo' ? `<br><span class="muted">${feed.format}${lastRaw ? ', ' + (lastRaw.bytes.length / 1024).toFixed(1) + ' KB' : ''}</span>` : ''),
      age != null && settings.src === 'live' && age > 120 ? 'err' : 'ok');
    if (selRef && !$('ledInfo').hidden) showLedInfo(selRef);
    updateBoardTitles(); updateMarkers(); renderRoutes(); renderVehicles(); renderOther();
  }

  async function fetchLive() {
    const url = settings.url.trim();
    setStatus(`Fetching…`, '');
    let res;
    try {
      res = await fetch(url, { cache: 'no-store' });
    } catch (e) {
      const hint = served
        ? 'The page could not reach the proxy. Is serve.py still running?'
        : 'The browser blocked the request (CapMetro doesn\'t allow pages to read the feed directly) or there\'s no connection. Start <code>serve.py</code> and open <code>http://localhost:8737</code> instead.';
      setStatus(`Couldn't fetch the feed.<br>${hint}`, 'err');
      return;
    }
    if (!res.ok) {
      const body = (await res.text().catch(() => '')).slice(0, 300);
      setStatus(`The feed answered HTTP ${res.status}.${body ? '<br>' + esc(body) : ''}`, 'err');
      return;
    }
    const bytes = new Uint8Array(await res.arrayBuffer());
    lastRaw = { bytes, type: res.headers.get('content-type') || '' };
    let feed;
    try { feed = Feed.decodeAny(bytes); } catch (e) { setStatus(`Got ${bytes.length} bytes but couldn't read them as GTFS-realtime: ${esc(e.message)}`, 'err'); return; }
    apply(feed, Date.now() / 1000, `Live · fetched <b>${new Date().toLocaleTimeString()}</b>`);
  }

  function tickDemo() {
    if (!demo) demo = new Demo(ledmap, Geo, 5);
    lastRaw = null;
    const now = Date.now() / 1000;
    apply(demo.tick(now), now, 'Demo · made-up buses at 5× speed');
  }

  async function stepReplay(d) {
    const n = replay.files.length;
    if (!n) { setStatus('Choose saved feed files (.pb or .json) to replay. <code>serve.py --record</code> saves them as it runs.', ''); return; }
    replay.i = (replay.i + d + n) % n;
    const file = replay.files[replay.i];
    $('replayPos').textContent = `${replay.i + 1} / ${n}`;
    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      lastRaw = { bytes, type: '' };
      const feed = Feed.decodeAny(bytes);
      const now = feed.t != null ? feed.t : Math.max(0, ...feed.vehicles.map(v => v.ts || 0)) || null;
      apply(feed, now, `Replay · <b>${esc(file.name)}</b>`);
    } catch (e) { setStatus(`Couldn't read ${esc(file.name)}: ${esc(e.message)}`, 'err'); }
  }

  function restart() {
    clearInterval(timer); timer = null;
    if (!ledmap) return;
    engine.reset(); demo = null;
    if (result === null) clearSelection();
    $('liveSection').hidden = settings.src !== 'live';
    $('replaySection').hidden = settings.src !== 'replay';
    for (const b of $('sourceSeg').children) b.classList.toggle('on', b.dataset.src === settings.src);
    $('pauseBtn').classList.toggle('on', paused);
    $('pauseBtn').textContent = paused ? 'Resume' : 'Pause';
    if (settings.src === 'live') {
      fetchLive();
      if (!paused) timer = setInterval(fetchLive, settings.pollS * 1000);
    } else if (settings.src === 'demo') {
      tickDemo();
      if (!paused) timer = setInterval(tickDemo, 3000);
    } else {
      replay.i = -1; stepReplay(1);
      if (!paused && $('replayAuto').checked) timer = setInterval(() => stepReplay(1), 2000);
    }
  }

  // ---- controls ----
  for (const b of $('sourceSeg').children) b.addEventListener('click', () => { settings.src = b.dataset.src; saveUi(); result = null; restart(); });
  $('pauseBtn').addEventListener('click', () => { paused = !paused; restart(); });
  $('feedUrl').value = settings.url;
  $('feedUrl').addEventListener('change', () => { settings.url = $('feedUrl').value.trim() || (served ? '/feed' : CAPMETRO_PB); $('feedUrl').value = settings.url; saveUi(); restart(); });
  $('pollS').value = settings.pollS;
  $('pollS').addEventListener('change', () => { settings.pollS = Math.max(30, Math.min(60, +$('pollS').value || 30)); $('pollS').value = settings.pollS; saveUi(); restart(); });
  $('fetchBtn').addEventListener('click', fetchLive);
  $('saveFeedBtn').addEventListener('click', () => {
    if (!lastRaw) { toast('Nothing fetched yet.'); return; }
    const json = /json|text/.test(lastRaw.type) || lastRaw.bytes[0] === 0x7b;
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([lastRaw.bytes]));
    a.download = `vehiclepositions-${new Date().toISOString().replace(/[:.]/g, '-')}.${json ? 'json' : 'pb'}`;
    a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  });
  $('replayFiles').addEventListener('change', e => {
    replay.files = [...e.target.files].sort((a, b) => a.name.localeCompare(b.name));
    e.target.value = ''; restart();
  });
  $('replayPrev').addEventListener('click', () => stepReplay(-1));
  $('replayNext').addEventListener('click', () => stepReplay(1));
  $('replayAuto').addEventListener('change', restart);

  const opt = (id, key, fn) => { $(id).checked = settings[key]; $(id).addEventListener('change', () => { settings[key] = $(id).checked; saveUi(); if (fn) fn(); }); };
  opt('optHyst', 'hyst', () => { engine.opts.hysteresis = settings.hyst; engine.reset(); });
  opt('optOff', 'showOff', updateMarkers);
  opt('showAll', 'showAll', renderVehicles);

  $('mapFile').addEventListener('change', async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) return;
    try { if (useMap(JSON.parse(await f.text()), false)) toast(`Loaded ${f.name}`); }
    catch (err) { toast('That file isn\'t valid JSON: ' + err.message); }
  });
  $('sampleBtn').addEventListener('click', () => useMap(window.LEDMAP_SAMPLE, true));

  // Start with the last map opened here, else the sample.
  const saved = load(MAP_KEY);
  if (!(saved && useMap(saved, false))) useMap(window.LEDMAP_SAMPLE, true);
})();
