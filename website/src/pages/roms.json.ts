import type { APIRoute } from 'astro';
import { machines } from '../data/machines';
import roms from '../data/roms.json';

// What each released plugin needs from the user's ROM folder, for Lemus Plugin Manager
// (manager/RomCheck.h in the repo): its ROM images (name, size, SHA1, CRC, whether optional and
// what the machine lacks without it) and its artwork places. src/data/roms.json comes from the
// machines' own specs (tools/release/roms-json.sh); only released plugins are listed. Lemus ships
// no ROMs: nothing here says where to get one.

const released = new Set(machines.filter((m) => m.release).map((m) => m.slug));
const all = roms.plugins as Record<string, unknown>;

export const GET: APIRoute = () => {
  const plugins = Object.fromEntries(Object.entries(all).filter(([slug]) => released.has(slug)));
  return new Response(JSON.stringify({ plugins }) + '\n', {
    headers: { 'Content-Type': 'application/json' },
  });
};
