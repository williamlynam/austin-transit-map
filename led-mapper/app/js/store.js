/* Autosave. The map in progress goes to localStorage; the GTFS extract can be
 * larger, so it goes to IndexedDB. Both are per-browser conveniences: the real
 * save is the ledmap.json you download. */
(function (root) {
  'use strict';
  const KEY = 'ledmapper.map.v1', UI = 'ledmapper.ui.v1';

  function get(k) { try { const s = localStorage.getItem(k); return s ? JSON.parse(s) : null; } catch (e) { return null; } }
  function set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); return true; } catch (e) { return false; } }

  function idb() {
    return new Promise((res, rej) => {
      let req;
      try { req = indexedDB.open('ledmapper', 1); } catch (e) { rej(e); return; }
      req.onupgradeneeded = () => req.result.createObjectStore('kv');
      req.onsuccess = () => res(req.result);
      req.onerror = () => rej(req.error);
    });
  }
  async function idbGet(k) {
    try {
      const db = await idb();
      return await new Promise(res => {
        const r = db.transaction('kv').objectStore('kv').get(k);
        r.onsuccess = () => res(r.result || null); r.onerror = () => res(null);
      });
    } catch (e) { return null; }
  }
  async function idbSet(k, v) {
    try {
      const db = await idb();
      await new Promise(res => {
        const tx = db.transaction('kv', 'readwrite');
        tx.objectStore('kv').put(v, k);
        tx.oncomplete = tx.onerror = () => res();
      });
    } catch (e) { /* not available, e.g. private window */ }
  }

  root.Store = {
    loadMap: () => get(KEY), saveMap: doc => set(KEY, doc),
    loadUi: () => get(UI) || {}, saveUi: ui => set(UI, ui),
    loadGtfs: () => idbGet('gtfs'), saveGtfs: g => idbSet('gtfs', g)
  };
})(window);
