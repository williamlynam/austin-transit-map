/* Board view: the PCB render with every LED drawn at its KiCad position (mm). */
(function (root) {
  'use strict';
  const NS = 'http://www.w3.org/2000/svg';
  const el = (name, attrs, parent) => {
    const e = document.createElementNS(NS, name);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  };

  class BoardView {
    constructor(svg, art, onPick) {
      this.svg = svg; this.art = art; this.onPick = onPick;
      this.full = { x: art.x0, y: art.y0, w: art.x1 - art.x0, h: art.y1 - art.y0 };
      this.view = Object.assign({}, this.full);
      el('image', { href: art.image, x: art.x0, y: art.y0, width: this.full.w, height: this.full.h, preserveAspectRatio: 'none', class: 'art' }, svg);
      this.dimmer = el('rect', { x: art.x0, y: art.y0, width: this.full.w, height: this.full.h, class: 'dimmer' }, svg);
      this.pathLayer = el('g', {}, svg);
      this.ledLayer = el('g', {}, svg);
      this.applyView();
      this.bindPanZoom();
    }

    applyView() {
      const v = this.view;
      this.svg.setAttribute('viewBox', `${v.x} ${v.y} ${v.w} ${v.h}`);
    }

    bindPanZoom() {
      const svg = this.svg;
      const toBoard = (cx, cy) => {
        const r = svg.getBoundingClientRect();
        const s = Math.max(this.view.w / r.width, this.view.h / r.height);
        const ox = (r.width * s - this.view.w) / 2, oy = (r.height * s - this.view.h) / 2;
        return [this.view.x - ox + (cx - r.left) * s, this.view.y - oy + (cy - r.top) * s, s];
      };
      svg.addEventListener('wheel', e => {
        e.preventDefault();
        const [bx, by] = toBoard(e.clientX, e.clientY);
        const f = Math.exp(e.deltaY * 0.0015);
        const w = Math.min(this.full.w * 1.2, Math.max(8, this.view.w * f));
        const k = w / this.view.w;
        this.view = { x: bx - (bx - this.view.x) * k, y: by - (by - this.view.y) * k, w, h: this.view.h * k };
        this.applyView();
      }, { passive: false });
      let drag = null;
      svg.addEventListener('pointerdown', e => {
        if (e.target.dataset && e.target.dataset.ref) return;
        drag = { x: e.clientX, y: e.clientY, v: Object.assign({}, this.view), moved: false };
        svg.setPointerCapture(e.pointerId);
      });
      svg.addEventListener('pointermove', e => {
        if (!drag) return;
        const s = toBoard(0, 0)[2];
        const dx = (e.clientX - drag.x) * s, dy = (e.clientY - drag.y) * s;
        if (Math.abs(dx) + Math.abs(dy) > 0.5) drag.moved = true;
        this.view = Object.assign({}, drag.v, { x: drag.v.x - dx, y: drag.v.y - dy });
        this.applyView();
      });
      const end = () => { drag = null; };
      svg.addEventListener('pointerup', end);
      svg.addEventListener('pointercancel', end);
      svg.addEventListener('dblclick', () => { this.view = Object.assign({}, this.full); this.applyView(); });
    }

    // Keep the current LED in view without changing the zoom.
    reveal(led) {
      const v = this.view, m = Math.min(v.w, v.h) * 0.1;
      if (led.x_mm < v.x + m || led.x_mm > v.x + v.w - m || led.y_mm < v.y + m || led.y_mm > v.y + v.h - m) {
        this.view = Object.assign({}, v, { x: led.x_mm - v.w / 2, y: led.y_mm - v.h / 2 });
        this.applyView();
      }
    }

    render(map, s) {
      const route = map.route(s.routeId);
      this.pathLayer.textContent = '';
      this.ledLayer.textContent = '';
      const inRoute = new Set(map.sequence(route));

      // Path lines for the selected route, one per direction.
      map.paths(route).forEach((p, n) => {
        const pts = p.seq.map(r => map.physical(r)).map(l => `${l.x_mm},${l.y_mm}`).join(' ');
        el('polyline', { points: pts, class: 'bpath' + (n ? ' alt' : ''), stroke: colorOf(route) }, this.pathLayer);
      });

      for (const r of map.routes) {
        for (const led of r.leds) {
          const mine = inRoute.has(led.ref);
          const placed = map.isPlaced(mine ? route : r, led.ref);
          const cls = ['led', mine ? 'mine' : 'other', placed ? 'placed' : 'empty'];
          const ent = map.entry(mine ? route : r, led.ref);
          if (ent && ent.skip) cls.push('skip');
          if (led.ref === s.currentRef) cls.push('current');
          if (led.ref === s.testRef) cls.push('lit');
          const g = el('g', { class: cls.join(' ') }, this.ledLayer);
          const ownerColor = colorOf(r);
          if (mine && r !== route) el('circle', { cx: led.x_mm, cy: led.y_mm, r: 1.75, class: 'shared-ring', stroke: colorOf(route) }, g);
          const c = el('circle', { cx: led.x_mm, cy: led.y_mm, r: 1.15, stroke: ownerColor, fill: placed ? ownerColor : 'rgba(0,0,0,.55)', 'data-ref': led.ref }, g);
          el('title', {}, c).textContent = `${led.ref} · ${r.route_id} ${r.chain}[${led.i}]` + (mine && r !== route ? ` · shared with ${route.route_id}` : '') + (placed ? '' : ' · not placed');
          c.addEventListener('click', e => { e.stopPropagation(); this.onPick(led.ref); });
        }
      }
      const cur = s.currentRef && map.physical(s.currentRef);
      if (cur) {
        el('circle', { cx: cur.x_mm, cy: cur.y_mm, r: 2.6, class: 'cursor' }, this.ledLayer);
        const t = el('text', { x: cur.x_mm + 3, y: cur.y_mm - 2.5, class: 'cursor-label' }, this.ledLayer);
        t.textContent = cur.ref;
      }
    }
  }

  root.BoardView = BoardView;
})(window);
