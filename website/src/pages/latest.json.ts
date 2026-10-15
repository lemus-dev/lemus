import type { APIRoute } from 'astro';
import { machines } from '../data/machines';

// The plugins' update check reads this once a day (host/Updates.h in the repo): each released
// plugin's newest version and its page. A plugin missing from it shows no update.
export const GET: APIRoute = ({ site }) => {
  const plugins = Object.fromEntries(
    machines
      .filter((m) => m.release)
      .map((m) => [m.slug, { version: m.release!.version, url: new URL(`/${m.slug}`, site).href }]),
  );
  return new Response(JSON.stringify({ plugins }) + '\n', {
    headers: { 'Content-Type': 'application/json' },
  });
};
