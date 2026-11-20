import type { APIRoute } from 'astro';
import { manager } from '../data/manager';
import { assetOf, isNewer, machines, type Release } from '../data/machines';

// The plugins' update check reads this once a day (host/Updates.h in the repo): each released
// plugin's newest version, its page and what its last versions changed (none past the release). A
// plugin missing from it shows no update.
//
// Lemus Plugin Manager (manager/Catalog.h) reads it too, so each entry also says what the machine
// is and where its installers are, with the SHA-256 the manager checks each download against.
// Those come from the GitHub release when the site is built; if GitHub can't be reached the entry
// has no "downloads" and the manager can't install it until the next build.

type Download = { url: string; sha256: string; size: number };

const digests = new Map<string, Map<string, { sha256: string; size: number }>>();

async function assetsOf(tag: string) {
  if (!digests.has(tag)) {
    const found = new Map<string, { sha256: string; size: number }>();
    try {
      const headers: Record<string, string> = { Accept: 'application/vnd.github+json', 'User-Agent': 'lemusdev.org build' };
      if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
      const res = await fetch(`https://api.github.com/repos/lemus-dev/lemus/releases/tags/${tag}`, { headers });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      for (const a of (await res.json()).assets ?? [])
        if (typeof a.digest === 'string' && a.digest.startsWith('sha256:'))
          found.set(a.name, { sha256: a.digest.slice(7), size: a.size });
    } catch (e) {
      console.warn(`latest.json: no installer digests for ${tag} (${e})`);
    }
    digests.set(tag, found);
  }
  return digests.get(tag)!;
}

async function downloadsOf(r: Release) {
  const assets = await assetsOf(r.tag);
  const out: Record<string, Download> = {};
  for (const [system, file] of [['mac', r.mac], ['windows', r.windows]] as const) {
    const a = assets.get(file);
    if (a) out[system] = { url: assetOf(r, file), ...a };
  }
  return Object.keys(out).length ? out : undefined;
}

export const GET: APIRoute = async ({ site }) => {
  const plugins: Record<string, unknown> = {};
  for (const m of machines.filter((m) => m.release)) {
    const r = m.release!;
    plugins[m.slug] = {
      version: r.version,
      url: new URL(`/${m.slug}`, site).href,
      changes: m.changes.filter((c) => !isNewer(c.version, r.version)).slice(0, 10),
      emulates: m.emulates,
      year: m.year,
      type: m.type,
      stage: r.stage,
      withoutRom: m.withoutRom,
      standalone: m.standalone ?? true,
      downloads: await downloadsOf(r),
    };
  }
  if (manager.release) {
    const r = manager.release;
    plugins[manager.slug] = {
      version: r.version,
      url: new URL(manager.page, site).href,
      changes: manager.changes.filter((c) => !isNewer(c.version, r.version)).slice(0, 10),
      downloads: await downloadsOf(r),
    };
  }
  return new Response(JSON.stringify({ plugins }) + '\n', {
    headers: { 'Content-Type': 'application/json' },
  });
};
