/* LED Mapper: place each board LED on the real map, one route at a time. */
(function () {
  'use strict';

  const $ = id => document.getElementById(id);
  const esc = s => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  // ---------- state ----------
  let ledmap = loadInitialMap();
  let gtfs = null;
  const ui = Object.assign({ routeId: ledmap.routes[0].route_id, idx: 0, testDir: null }, Store.loadUi());
  if (!ledmap.route(ui.routeId)) { ui.routeId = ledmap.routes[0].route_id; ui.idx = 0; }
  let testMode = false, testRef = null;

  function loadInitialMap() {
    const saved = Store.loadMap();
    if (saved) {
      try { return new LedMap(saved); } catch (e) { console.warn('Autosave unreadable, starting from the seed', e); }
    }
    return new LedMap(window.LEDMAP_SEED);
  }

  const route = () => ledmap.route(ui.routeId);
  const seq = () => ledmap.sequence(route());
  const currentRef = () => seq()[Math.min(ui.idx, seq().length - 1)];

  const undoStack = [];
  function checkpoint() {
    undoStack.push(JSON.stringify(ledmap.doc));
    if (undoStack.length > 100) undoStack.shift();
  }
  function undo() {
    if (!undoStack.length) { toast('Nothing to undo'); return; }
    ledmap = new LedMap(JSON.parse(undoStack.pop()));
    persist(); renderAll(); toast('Undone');
  }

  function persist() { Store.saveMap(ledmap.doc); Store.saveUi({ routeId: ui.routeId, idx: ui.idx, testDir: ui.testDir, base: ui.base, tileUrl: ui.tileUrl, cartoKey: ui.cartoKey }); }

  function toast(msg, ms) {
    const t = $('toast');
    t.textContent = msg; t.hidden = false;
    clearTimeout(toast.h);
    toast.h = setTimeout(() => { t.hidden = true; }, ms || 2600);
  }

  // ---------- map ----------
  const map = L.map('map', { zoomControl: true, doubleClickZoom: false }).setView([30.33, -97.72], 11);
  // Esri's tiles accept requests from a page opened as a local file; CARTO and
  // OpenStreetMap refuse those. A custom tile URL (with your own key) can be set
  // under "Map tiles" in the sidebar.
  const esri = (svc, label) => L.tileLayer(`https://server.arcgisonline.com/ArcGIS/rest/services/${svc}/MapServer/tile/{z}/{y}/{x}`,
    { maxZoom: 19, attribution: 'Tiles &copy; Esri' + (label ? ' &mdash; ' + label : '') });
  const bases = {
    'Streets (Esri)': esri('World_Street_Map', 'Esri, HERE, Garmin, OpenStreetMap contributors'),
    'Light gray (Esri)': esri('Canvas/World_Light_Gray_Base', 'Esri, HERE, Garmin, OpenStreetMap contributors'),
    'Satellite (Esri)': esri('World_Imagery', 'Esri, Maxar, Earthstar Geographics')
  };
  const settings = Store.loadUi();
  const layersCtl = L.control.layers(null, null, { position: 'topright' }).addTo(map);
  let customLayer = null;
  function setCustomTiles(url) {
    if (customLayer) { layersCtl.removeLayer(customLayer); map.removeLayer(customLayer); delete bases['Custom']; customLayer = null; }
    if (url) {
      customLayer = L.tileLayer(url, { maxZoom: 20, subdomains: 'abcd', attribution: 'Custom tiles' });
      bases['Custom'] = customLayer;
      layersCtl.addBaseLayer(customLayer, 'Custom');
    }
  }
  for (const [name, layer] of Object.entries(bases)) layersCtl.addBaseLayer(layer, name);
  setCustomTiles(settings.tileUrl || '');

  // CARTO basemaps need a key (docs.carto.com/faqs/carto-basemaps).
  const cartoNames = ['Streets (CARTO)', 'Light (CARTO)', 'Dark (CARTO)'];
  function setCartoKey(key) {
    for (const n of cartoNames) if (bases[n]) { layersCtl.removeLayer(bases[n]); map.removeLayer(bases[n]); delete bases[n]; }
    if (!key) return;
    const k = encodeURIComponent(key);
    [['voyager', cartoNames[0]], ['light_all', cartoNames[1]], ['dark_all', cartoNames[2]]].forEach(([style, name]) => {
      bases[name] = L.tileLayer(`https://basemaps.cartocdn.com/rastertiles/${style}/{z}/{x}/{y}.png?key=${k}`,
        { maxZoom: 20, attribution: '&copy; OpenStreetMap contributors &copy; CARTO' });
      layersCtl.addBaseLayer(bases[name], name);
    });
  }
  setCartoKey(settings.cartoKey || '');
  (bases[settings.base] || bases[settings.cartoKey ? 'Streets (CARTO)' : 'Streets (Esri)']).addTo(map);
  map.on('baselayerchange', e => { ui.base = e.name; persist(); });
  L.control.scale({ imperial: true, metric: true }).addTo(map);

  const shapeLayer = L.layerGroup().addTo(map);
  const stopLayer = L.layerGroup().addTo(map);
  const pathLayer = L.layerGroup().addTo(map);
  const otherLedLayer = L.layerGroup().addTo(map);
  const ledLayer = L.layerGroup().addTo(map);
  const testLayer = L.layerGroup().addTo(map);
  let stopMarkers = [];

  function updateLabelZoom() {
    map.getContainer().classList.toggle('labels-off', map.getZoom() < 14);
  }
  map.on('zoomend', updateLabelZoom);
  updateLabelZoom();

  function drawGtfs() {
    shapeLayer.clearLayers(); stopLayer.clearLayers(); stopMarkers = [];
    if (!gtfs) return;
    const sel = ui.routeId;
    for (const r of ledmap.routes) {
      const g = gtfs.routes[r.route_id];
      if (!g) continue;
      const on = r.route_id === sel;
      for (const s of g.shapes) {
        L.polyline(s.pts, { color: colorOf(r), weight: on ? 5 : 3, opacity: on ? 0.75 : 0.28, interactive: false }).addTo(shapeLayer);
      }
    }
    for (const s of Object.values(gtfs.stops)) {
      const mine = s.routes.includes(sel);
      const owner = ledmap.route(mine ? sel : s.routes[0]);
      const m = L.circleMarker([s.lat, s.lon], {
        radius: s.rail ? 7 : (mine ? 5 : 3.5), color: mine ? '#111' : '#666', weight: mine ? 2 : 1,
        fillColor: owner ? colorOf(owner) : '#999', fillOpacity: mine ? 1 : 0.55
      });
      m.bindTooltip(esc(s.name) + `<span class="stop-routes">${s.routes.join(' · ')}</span>`, {
        permanent: true, direction: 'right', offset: [6, 0],
        className: 'stop-label' + (s.rail ? ' rail' : '') + (mine ? ' mine' : '')
      });
      m.stop = s;
      m.addTo(stopLayer);
      stopMarkers.push(m);
    }
  }

  function ledIcon(text, color, cls) {
    return L.divIcon({ className: 'led-icon ' + (cls || ''), html: `<span style="--c:${color}">${esc(text)}</span>`, iconSize: [26, 26], iconAnchor: [13, 13] });
  }

  function drawLeds() {
    ledLayer.clearLayers(); otherLedLayer.clearLayers(); pathLayer.clearLayers();
    const r = route(), cur = currentRef();
    const mine = new Set(seq());
    // Path lines through the placed LEDs, one per direction.
    ledmap.paths(r).forEach((p, n) => {
      const pts = ledmap.points(r, p.seq).filter(l => l.lat != null).map(l => [l.lat, l.lon]);
      if (pts.length > 1) L.polyline(pts, { color: '#111', weight: 2, opacity: 0.7, dashArray: n ? '6 6' : null, interactive: false }).addTo(pathLayer);
    });
    for (const other of ledmap.routes) {
      if (other === r) continue;
      for (const led of other.leds) {
        if (led.lat == null) continue;
        L.circleMarker([led.lat, led.lon], { radius: 3, color: colorOf(other), weight: 1, fillOpacity: 0.6, interactive: false }).addTo(otherLedLayer);
      }
    }
    seq().forEach((ref, k) => {
      const led = ledmap.entry(r, ref);
      if (led.lat == null) return;
      const owner = ledmap.ownerRoute(ref), phys = ledmap.physical(ref);
      const cls = (ref === cur ? 'current' : '') + (owner !== r ? ' shared' : '') + (led.skip ? ' skip' : '') + (ref === testRef ? ' lit' : '');
      const m = L.marker([led.lat, led.lon], { icon: ledIcon(ref.replace(/^D/, ''), colorOf(owner), cls), draggable: true, title: `${ref} (${owner.route_id} ${owner.chain}[${phys.i}])` + (led.label ? ' · ' + led.label : '') });
      m.on('click', () => select(k));
      m.on('dragend', e => {
        const p = e.target.getLatLng();
        const spot = snapClick(p, false, ref);
        checkpoint();
        ledmap.place(r, ref, spot.lat, spot.lon, spot.stop);
        persist(); renderAll();
        toast(`${ref} moved` + (spot.stop ? ` to ${spot.stop.name}` : ''));
      });
      m.addTo(ledLayer);
    });
  }

  // Where a click lands: a station of this route, the route line, or the raw point.
  // GTFS direction_ids an LED belongs to: null if it's on every path (any direction
  // is fine), else the direction_ids of the one-way paths it's on.
  function dirsOf(ref) {
    const paths = ledmap.paths(route());
    if (paths.length < 2) return null;
    const on = paths.filter(p => p.seq.includes(ref));
    if (on.length === paths.length || on.some(p => p.direction_id == null)) return null;
    return on.map(p => String(p.direction_id));
  }

  // Direction beats distance: an LED on one side of a couplet only snaps to that
  // direction's street and stops, even if the other street is closer to the click.
  function snapClick(latlng, raw, ref) {
    const out = { lat: latlng.lat, lon: latlng.lng, stop: null, how: 'exact' };
    if (raw || !$('snapChk').checked || !gtfs) return out;
    const dirs = dirsOf(ref || currentRef());
    const pt = map.latLngToContainerPoint(latlng);
    let best = null;
    for (const m of stopMarkers) {
      if (!m.stop.routes.includes(ui.routeId)) continue;
      const sd = m.stop.dirs && m.stop.dirs[ui.routeId];
      if (dirs && sd && !sd.some(d => dirs.includes(d))) continue;
      const d = pt.distanceTo(map.latLngToContainerPoint(m.getLatLng()));
      if (d < 16 && (!best || d < best.d)) best = { d, s: m.stop };
    }
    if (best) return { lat: best.s.lat, lon: best.s.lon, stop: best.s, how: 'station' };
    const g = gtfs.routes[ui.routeId];
    if (g && g.shapes.length) {
      const lines = g.shapes.filter(s => !dirs || dirs.includes(String(s.dir))).map(s => s.pts);
      const near = lines.length ? Geo.nearestOnLines(latlng.lat, latlng.lng, lines) : null;
      const mPerPx = 156543.03 * Math.cos(latlng.lat * Math.PI / 180) / Math.pow(2, map.getZoom());
      if (near && near.d / mPerPx < 25) return { lat: near.lat, lon: near.lon, stop: null, how: 'line' };
    }
    return out;
  }

  map.on('click', e => {
    if (testMode) return runTest(e.latlng);
    const ref = currentRef();
    const spot = snapClick(e.latlng, e.originalEvent && e.originalEvent.shiftKey);
    const owner = ledmap.ownerRoute(ref);
    checkpoint();
    ledmap.place(route(), ref, spot.lat, spot.lon, spot.stop);
    const where = spot.stop ? spot.stop.name : (spot.how === 'line' ? 'the route line' : 'the clicked point');
    const note = owner !== route() ? ` (${owner.route_id} LED, ${ui.routeId} position)` : '';
    if (ui.idx < seq().length - 1) {
      ui.idx++;
      toast(`${ref}${note} placed at ${where}. Now ${currentRef()}.`);
    } else {
      toast(`${ref} placed at ${where}. That was the last LED on ${ui.routeId}.`, 4000);
    }
    persist(); renderAll(); followCurrent();
  });

  // ---------- test mode ----------
  function runTest(latlng) {
    const r = route();
    const paths = ledmap.paths(r);
    const pi = ui.testDir === '' || ui.testDir == null || !paths[+ui.testDir] ? null : +ui.testDir;
    const hit = ledmap.snap(r, pi, latlng.lat, latlng.lng, Geo);
    testLayer.clearLayers();
    L.circleMarker(latlng, { radius: 8, color: '#000', weight: 3, fillColor: '#fff', fillOpacity: 1 }).addTo(testLayer);
    testRef = hit && !hit.hidden ? hit.ref : null;
    const dirTxt = pi != null ? (paths.length > 1 ? paths[pi].name : 'any direction') : 'direction unknown';
    $('testOut').innerHTML = hit && hit.hidden
      ? `A ${esc(r.route_id)} vehicle here with unknown direction would be hidden: it is nearest <b>${esc(hit.ref)}</b>, which is only on one direction's path.`
      : hit
      ? `A ${esc(r.route_id)} vehicle here (${esc(dirTxt)}) lights <b>${esc(hit.ref)}</b>, ${Math.round(hit.d)} m from the LED path.`
      : `No LED: outside the corridor (${ledmap.doc.max_snap_m || 400} m, or ${Math.round((ledmap.doc.snap_fraction != null ? ledmap.doc.snap_fraction : 0.3) * 100)}% of a long gap) of the ${esc(r.route_id)} ${esc(dirTxt)} LEDs, or too few are placed.`;
    if (hit) {
      const l = ledmap.entry(r, hit.ref);
      L.polyline([latlng, [l.lat, l.lon]], { color: '#000', dashArray: '3 4', weight: 2 }).addTo(testLayer);
    }
    renderAll();
  }

  // ---------- sidebar ----------
  function renderRoutes() {
    $('routeList').innerHTML = ledmap.routes.map(r => {
      const p = ledmap.progress(r);
      const done = p.placed === p.total;
      return `<li class="${r.route_id === ui.routeId ? 'sel' : ''}" data-id="${esc(r.route_id)}">
        <span class="sw" style="background:${colorOf(r)}"></span>
        <span class="rname"><b>${esc(r.route_id)}</b> ${esc(r.name || '')}<br><span class="muted">${esc(r.chain)} · IO${esc(r.gpio)}</span></span>
        <span class="prog ${done ? 'done' : ''}">${p.placed}/${p.total}</span></li>`;
    }).join('');
    for (const li of $('routeList').children) li.onclick = () => selectRoute(li.dataset.id);
  }

  function pathTags(ref) {
    const paths = ledmap.paths(route());
    if (paths.length < 2) return '';
    const inP = paths.filter(p => p.seq.includes(ref)).map(p => p.name);
    return inP.length === paths.length ? '' : `<span class="tag">${esc(inP.join('/') || 'no path')} only</span>`;
  }

  function renderCurrent() {
    const ref = currentRef(), r = route(), led = ledmap.entry(r, ref), phys = ledmap.physical(ref), owner = ledmap.ownerRoute(ref);
    $('curRef').innerHTML = `${esc(ref)} <span class="muted">${ui.idx + 1} of ${seq().length} on ${esc(r.route_id)}</span>`;
    const bits = [`${esc(owner.chain)} index ${phys.i}, IO${esc(owner.gpio)}`];
    if (owner !== r) bits.push(`<span class="tag shared">Shared crossing: a ${esc(owner.route_id)} LED. Place it where the ${esc(r.route_id)} crosses; the ${esc(owner.route_id)} keeps its own position.</span>`);
    const t = pathTags(ref); if (t) bits.push(t);
    bits.push(led.lat != null
      ? `Placed at ${led.lat.toFixed(5)}, ${led.lon.toFixed(5)}` + (led.label ? `<br>Station: <b>${esc(led.label)}</b>` : '')
      : '<span class="warn">Not placed yet. Click its spot on the map.</span>');
    $('curInfo').innerHTML = bits.join('<br>');
    $('skipChk').checked = !!led.skip;
  }

  function renderSeq() {
    const cur = currentRef();
    $('seqList').innerHTML = seq().map((ref, k) => {
      const led = ledmap.entry(route(), ref), owner = ledmap.ownerRoute(ref);
      const shared = owner !== route() ? `<span class="tag shared">${esc(owner.route_id)}</span>` : '';
      return `<li data-k="${k}" class="${ref === cur ? 'cur' : ''} ${led.lat != null ? 'ok' : ''}">
        <span class="dot"></span>${esc(ref)} ${shared}${pathTags(ref)}<span class="lbl">${esc(led.label || '')}</span></li>`;
    }).join('');
    for (const li of $('seqList').children) li.onclick = () => select(+li.dataset.k);
    const c = $('seqList').querySelector('.cur');
    if (c) c.scrollIntoView({ block: 'nearest' });
  }

  // Match each board path (Northbound/Southbound) to the GTFS direction_id it carries.
  function renderDirections() {
    const r = route(), paths = ledmap.paths(r);
    const sec = $('dirSection');
    if (paths.length < 2) { sec.hidden = true; return; }
    sec.hidden = false;
    const dirs = gtfs && gtfs.routes[r.route_id] ? gtfs.routes[r.route_id].dirs : {};
    const ids = [...new Set(['0', '1', ...Object.keys(dirs)])].sort();
    $('dirRows').innerHTML = paths.map((p, k) => `<label class="dirrow"><b>${esc(p.name)}</b> path is GTFS
        <select data-k="${k}"><option value="">choose…</option>${ids.map(id => `<option value="${esc(id)}" ${String(p.direction_id) === id ? 'selected' : ''}>direction_id ${esc(id)}${dirs[id] ? ' → ' + esc(dirs[id].headsign) : ''}</option>`).join('')}</select>
      </label>`).join('') + (gtfs ? '' : '<p class="hint">Load GTFS to see each direction\'s headsign.</p>');
    for (const s of $('dirRows').querySelectorAll('select')) {
      s.onchange = () => {
        const p = paths[+s.dataset.k];
        checkpoint();
        p.direction_id = s.value === '' ? null : +s.value;
        persist(); renderStatus(); renderTest();
        toast(`${p.name} → direction_id ${s.value || 'unset'}`);
      };
    }
  }

  function renderTest() {
    $('testSection').hidden = !testMode;
    $('testBtn').classList.toggle('on', testMode);
    document.body.classList.toggle('testing', testMode);
    const paths = ledmap.paths(route());
    if (paths.length === 1) ui.testDir = '0';
    else if (ui.testDir == null || (ui.testDir !== '' && !paths[+ui.testDir])) ui.testDir = '';
    $('testDir').innerHTML = paths.map((p, k) => `<option value="${k}" ${ui.testDir === String(k) ? 'selected' : ''}>${esc(paths.length > 1 ? p.name + (p.direction_id != null ? ' (direction_id ' + p.direction_id + ')' : '') : 'any direction')}</option>`).join('')
      + (paths.length > 1 ? `<option value="" ${ui.testDir === '' ? 'selected' : ''}>unknown direction</option>` : '');
  }

  function renderStatus() {
    const probs = ledmap.problems();
    const placed = ledmap.routes.reduce((a, r) => a + ledmap.progress(r).placed, 0);
    const total = ledmap.routes.reduce((a, r) => a + ledmap.progress(r).total, 0);
    $('status').innerHTML = `<b>${placed}/${total}</b> route positions placed (shared LEDs count on both routes).<br>`
      + (gtfs ? `GTFS loaded ${esc(new Date(gtfs.loadedAt).toLocaleString())}.` : '<span class="warn">No GTFS loaded: stations and route lines will not show.</span>')
      + (probs.length ? `<br>Before export: ${probs.map(esc).join('; ')}.` : '<br>Every LED is placed.')
      + `<br><a href="#" id="resetLink">Start over from the board seed</a>`;
    $('resetLink').onclick = e => {
      e.preventDefault();
      if (!confirm('Clear every placement and start from the board seed? Save ledmap.json first if you want to keep this work.')) return;
      checkpoint(); ledmap = new LedMap(window.LEDMAP_SEED); ui.idx = 0; persist(); renderAll();
    };
    $('boardName').textContent = ledmap.doc.board || '';
  }

  let board;
  function renderAll() {
    renderRoutes(); renderCurrent(); renderSeq(); renderDirections(); renderTest(); renderStatus(); renderFill();
    board.render(ledmap, { routeId: ui.routeId, currentRef: currentRef(), testRef });
    drawLeds();
  }

  // Show the current LED on the map; if it isn't placed yet, show the last placed one before it.
  function followCurrent() {
    const s = seq();
    for (let k = ui.idx; k >= 0; k--) {
      const l = ledmap.entry(route(), s[k]);
      if (l.lat != null) {
        if (!map.getBounds().pad(-0.15).contains([l.lat, l.lon])) map.panTo([l.lat, l.lon]);
        break;
      }
    }
    board.reveal(ledmap.physical(currentRef()));
  }

  function select(k) {
    ui.idx = Math.max(0, Math.min(seq().length - 1, k));
    persist(); renderAll(); followCurrent();
  }

  function selectRoute(id, ref) {
    ui.routeId = id;
    const s = seq();
    ui.idx = ref && s.includes(ref) ? s.indexOf(ref) : Math.max(0, s.findIndex(x => !ledmap.isPlaced(route(), x)));
    testRef = null; testLayer.clearLayers();
    drawGtfs(); persist(); renderAll(); followCurrent();
  }

  // Board click: select the LED, switching route if it isn't on this one.
  function pickFromBoard(ref) {
    if (seq().includes(ref)) return select(seq().indexOf(ref));
    selectRoute(ledmap.ownerRoute(ref).route_id, ref);
  }

  // ---------- space evenly ----------
  let fillFor = null;  // route + LED the From/To defaults were picked for

  // Nearest placed LEDs before and after the current one on a path that contains it.
  function gapAround(ref) {
    const r = route();
    for (const p of ledmap.paths(r)) {
      const k = p.seq.indexOf(ref);
      if (k < 0) continue;
      let a = null, b = null;
      for (let i = k - 1; i >= 0; i--) if (ledmap.isPlaced(r, p.seq[i])) { a = p.seq[i]; break; }
      for (let i = k + 1; i < p.seq.length; i++) if (ledmap.isPlaced(r, p.seq[i])) { b = p.seq[i]; break; }
      if (a && b) return [a, b];
    }
    return [null, null];
  }

  function renderFill() {
    const r = route(), placed = seq().filter(x => ledmap.isPlaced(r, x));
    const key = ui.routeId + '|' + currentRef();
    let from = $('fillFrom').value, to = $('fillTo').value;
    if (fillFor !== key) { [from, to] = gapAround(currentRef()); fillFor = key; }
    const opts = sel => placed.map(x => `<option ${x === sel ? 'selected' : ''}>${esc(x)}</option>`).join('');
    $('fillFrom').innerHTML = '<option value="">–</option>' + opts(from);
    $('fillTo').innerHTML = '<option value="">–</option>' + opts(to);
    describeFill();
  }

  function describeFill() {
    const a = $('fillFrom').value, b = $('fillTo').value, r = route();
    let msg = '', ok = false;
    if (!a || !b) msg = 'Pick two placed LEDs.';
    else {
      const span = ledmap.between(r, a, b);
      if (!span) msg = `${a} and ${b} aren't on one path in that order.`;
      else if (!span.refs.length) msg = `Nothing between ${a} and ${b}.`;
      else {
        const n = span.refs.length, redo = span.refs.filter(x => ledmap.isPlaced(r, x)).length;
        msg = `${n} LED${n > 1 ? 's' : ''}: ${span.refs[0]}${n > 1 ? '…' + span.refs[n - 1] : ''}` + (span.oneWay ? ` on the ${span.path.name} path` : '')
          + (redo ? `. ${redo} already placed will be moved.` : '.');
        ok = true;
      }
    }
    $('fillInfo').textContent = msg;
    $('fillBtn').disabled = !ok;
  }
  $('fillFrom').onchange = describeFill;
  $('fillTo').onchange = describeFill;

  $('fillBtn').onclick = () => {
    const r = route(), a = $('fillFrom').value, b = $('fillTo').value;
    const span = ledmap.between(r, a, b);
    if (!span || !span.refs.length) return;
    const A = ledmap.entry(r, a), B = ledmap.entry(r, b);
    const shapes = gtfs && gtfs.routes[r.route_id] ? gtfs.routes[r.route_id].shapes : [];
    if (span.oneWay && span.path.direction_id == null) {
      toast(`Match the ${span.path.name} path to a GTFS direction first (Directions, above), so the fill follows the right street.`, 6000);
      return;
    }
    const along = Geo.evenAlong([A.lat, A.lon], [B.lat, B.lon], span.refs.length, shapes,
      { forwardOnly: span.oneWay, onlyDir: span.oneWay ? span.path.direction_id : null });
    const pts = along ? along.points : Geo.evenStraight([A.lat, A.lon], [B.lat, B.lon], span.refs.length);
    checkpoint();
    span.refs.forEach((ref, k) => ledmap.place(r, ref, pts[k][0], pts[k][1], null));
    persist(); renderAll();
    toast(`Placed ${span.refs.length} LEDs between ${a} and ${b}` + (along ? ' along the route line.' : (shapes.length ? '. The route line doesn\'t pass near both ends, so they went on a straight line.' : '. No GTFS loaded, so they went on a straight line.')), 5000);
  };

  // ---------- buttons ----------
  $('undoBtn').onclick = undo;
  $('tileUrl').value = ui.tileUrl || '';
  $('cartoKey').value = ui.cartoKey || '';
  $('cartoApply').onclick = () => {
    const key = $('cartoKey').value.trim();
    const cur = Object.keys(bases).find(k => map.hasLayer(bases[k]));
    if (cur) map.removeLayer(bases[cur]);
    ui.cartoKey = key;
    setCartoKey(key);
    const pick = key ? 'Streets (CARTO)' : 'Streets (Esri)';
    bases[pick].addTo(map); ui.base = pick; persist();
    toast(key ? 'Using CARTO tiles with your key' : 'CARTO key removed, back to Esri streets');
  };
  $('tileApply').onclick = () => {
    const url = $('tileUrl').value.trim();
    if (url && !/\{z\}/.test(url)) { alert('A tile URL needs {z}, {x} and {y} in it.'); return; }
    ui.tileUrl = url;
    const cur = Object.keys(bases).find(k => map.hasLayer(bases[k]));
    if (cur) map.removeLayer(bases[cur]);
    setCustomTiles(url);
    const pick = url ? 'Custom' : 'Streets (Esri)';
    bases[pick].addTo(map); ui.base = pick; persist();
    toast(url ? 'Using your custom tiles' : 'Back to Esri streets');
  };
  $('prevBtn').onclick = () => select(ui.idx - 1);
  $('nextBtn').onclick = () => select(ui.idx + 1);
  $('clearBtn').onclick = () => { checkpoint(); ledmap.clear(route(), currentRef()); persist(); renderAll(); };
  $('nextUnplacedBtn').onclick = () => {
    const s = seq();
    for (let n = 1; n <= s.length; n++) {
      const k = (ui.idx + n) % s.length;
      if (!ledmap.isPlaced(route(), s[k])) return select(k);
    }
    toast(`Every LED on ${ui.routeId} is placed.`);
  };
  $('skipChk').onchange = e => { checkpoint(); ledmap.setSkip(route(), currentRef(), e.target.checked); persist(); renderAll(); };
  $('testBtn').onclick = () => {
    testMode = !testMode;
    if (!testMode) { testRef = null; testLayer.clearLayers(); }
    renderAll();
  };
  $('testDir').onchange = e => { ui.testDir = e.target.value; persist(); };

  document.addEventListener('keydown', e => {
    if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
    if (e.key === 'ArrowRight') { select(ui.idx + 1); e.preventDefault(); }
    else if (e.key === 'ArrowLeft') { select(ui.idx - 1); e.preventDefault(); }
    else if (e.key === 'Delete' || e.key === 'Backspace') { $('clearBtn').click(); e.preventDefault(); }
    else if (e.key === 'n' || e.key === 'N') $('nextUnplacedBtn').click();
    else if (e.key === 't' || e.key === 'T') $('testBtn').click();
    else if ((e.ctrlKey || e.metaKey) && (e.key === 'z' || e.key === 'Z')) { undo(); e.preventDefault(); }
  });

  $('saveBtn').onclick = () => {
    const probs = ledmap.problems();
    const doc = ledmap.toJSON();
    const blob = new Blob([JSON.stringify(doc, null, 1) + '\n'], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob); a.download = 'ledmap.json';
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
    toast(probs.length ? `Saved ledmap.json. Still to do: ${probs.join('; ')}` : 'Saved ledmap.json. Every LED is placed.', 5000);
  };

  $('mapFile').onchange = async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) return;
    try {
      const next = new LedMap(JSON.parse(await f.text()));
      checkpoint(); ledmap = next;
      if (!ledmap.route(ui.routeId)) ui.routeId = ledmap.routes[0].route_id;
      ui.idx = 0; persist(); drawGtfs(); renderAll(); fitPlaced();
      toast(`Opened ${f.name}`);
    } catch (err) { alert('Could not open that file: ' + err.message); }
  };

  $('gtfsFile').onchange = async e => {
    const f = e.target.files[0]; e.target.value = '';
    if (!f) return;
    try {
      const g = await Gtfs.loadGtfsZip(await f.arrayBuffer(), ledmap.routes.map(r => r.route_id), msg => { $('status').textContent = msg; });
      g.source = f.name;
      gtfs = g; await Store.saveGtfs(g);
      const missing = ledmap.routes.filter(r => !g.routes[r.route_id]).map(r => r.route_id);
      drawGtfs(); renderAll(); fitGtfs();
      toast(`Loaded ${Object.keys(g.routes).length} routes and ${Object.keys(g.stops).length} stops` + (missing.length ? `. Not in this feed: ${missing.join(', ')}` : ''), 5000);
    } catch (err) { console.error(err); alert('Could not read that GTFS zip: ' + err.message); renderStatus(); }
  };

  function fitGtfs() {
    const b = L.latLngBounds([]);
    for (const r of Object.values(gtfs.routes)) for (const s of r.shapes) b.extend(s.pts);
    if (b.isValid()) map.fitBounds(b, { padding: [20, 20] });
  }
  function fitPlaced() {
    const pts = [];
    for (const r of ledmap.routes) for (const l of r.leds) if (l.lat != null) pts.push([l.lat, l.lon]);
    if (pts.length) map.fitBounds(pts, { padding: [30, 30], maxZoom: 15 });
  }

  // ---------- start ----------
  board = new BoardView($('board'), window.LEDMAP_BOARD, pickFromBoard);
  renderAll();
  Store.loadGtfs().then(g => {
    if (g && g.routes) { gtfs = g; drawGtfs(); renderAll(); }
    if (gtfs) fitGtfs();
    if (ledmap.routes.some(r => r.leds.some(l => l.lat != null))) fitPlaced();
    followCurrent();
  });

  window.__ledmapper = { get map() { return ledmap; }, get gtfs() { return gtfs; }, leaflet: map, ui, snapClick };
})();
