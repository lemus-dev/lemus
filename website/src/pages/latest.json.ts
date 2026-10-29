import type { APIRoute } from 'astro';
import { isNewer, machines } from '../data/machines';

// The plugins' update check reads this once a day (host/Updates.h in the repo): each released
// plugin's newest version, its page and what its last versions changed (none past the release). A
// plugin missing from it shows no update.
export const GET: APIRoute = ({ site }) => {
  const plugins = Object.fromEntries(
    machines
      .filter((m) => m.release)
      .map((m) => [
        m.slug,
        {
          version: m.release!.version,
          url: new URL(`/${m.slug}`, site).href,
          changes: m.changes.filter((c) => !isNewer(c.version, m.release!.version)).slice(0, 10),
        },
      ]),
  );
  return new Response(JSON.stringify({ plugins }) + '\n', {
    headers: { 'Content-Type': 'application/json' },
  });
};
