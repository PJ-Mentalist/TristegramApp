// Tristegram activity: what Tristegram users do right now (music, video,
// code, games). Clients send their own status every 30 seconds and read
// the statuses of the people they look at. Nothing is stored on disk,
// a status disappears 3 minutes after the last update.
const TTL = 3 * 60 * 1000;
const KINDS = new Set(['music', 'video', 'coding', 'game']);
const store = globalThis.__tgActivity || (globalThis.__tgActivity = new Map());
const keys = globalThis.__tgKeys || (globalThis.__tgKeys = new Map());

function clean(now) {
  for (const [id, value] of store) {
    if (now - value.ts > TTL) {
      store.delete(id);
    }
  }
}

function text(value, limit) {
  return (typeof value === 'string' ? value : '').slice(0, limit);
}

function body(req) {
  if (req.body && typeof req.body === 'object') {
    return req.body;
  }
  try {
    return JSON.parse(req.body || '{}');
  } catch (e) {
    return {};
  }
}

module.exports = async (req, res) => {
  res.setHeader('Cache-Control', 'no-store');
  const now = Date.now();
  clean(now);
  if (req.method === 'POST') {
    const data = body(req);
    const id = text(data.id, 24);
    const key = text(data.key, 64);
    if (!/^\d{1,20}$/.test(id) || key.length < 16) {
      return res.status(400).json({ error: 'bad request' });
    }
    // The first key sent for an id owns it, others can not overwrite it.
    const known = keys.get(id);
    if (known && known !== key) {
      return res.status(403).json({ error: 'wrong key' });
    }
    keys.set(id, key);
    if (!KINDS.has(data.kind)) {
      store.delete(id);
      return res.json({ ok: true });
    }
    store.set(id, {
      kind: data.kind,
      title: text(data.title, 120),
      subtitle: text(data.subtitle, 120),
      app: text(data.app, 40),
      ts: now,
    });
    return res.json({ ok: true });
  }
  const ids = String((req.query && req.query.ids) || '')
    .split(',')
    .filter((id) => /^\d{1,20}$/.test(id))
    .slice(0, 100);
  const result = {};
  for (const id of ids) {
    const value = store.get(id);
    if (value) {
      result[id] = {
        kind: value.kind,
        title: value.title,
        subtitle: value.subtitle,
        app: value.app,
        age: now - value.ts,
      };
    }
  }
  return res.json(result);
};
