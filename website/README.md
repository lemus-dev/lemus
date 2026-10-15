# lemusdev.org

The Lemus website: [Astro](https://astro.build), built to static files and served by a
Cloudflare Worker (static assets only) on `lemusdev.org`, with `www.lemusdev.org` redirected to
it.

    npm install
    npm run dev       # http://localhost:4321
    npm run deploy    # build and publish to lemusdev.org (needs `npx wrangler login` once)

- `src/data/machines.ts`: one entry per released plugin. The home page's plugin table, the nav and
  the download links come from it. Set a machine's `release` when its installers are uploaded.
- `src/pages/`: one file per page. A machine's page is `/<plugin name>`.
- `public/media/<plugin name>/`: its screenshots and audio clips (renders of the plugin only).
- `public/download/`: the installers (`dist/` from `tools/release`), served at `/download/<file>`. Not in git:
  copy them in before `npm run deploy`, or a deploy drops them. Workers serve files up to 25 MiB.

The plugins link to `/download`, `/support` and `/licences`, and read `/latest.json` (built from
`machines.ts`) for the update check (`host/Updates.h`), so those paths must keep working.
