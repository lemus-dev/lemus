# How Lemus writes

Write it the way Chris would say it in a forum post, or in an email to someone who knows synths.
If it reads like a manual or a press release, start again. If it reads like a telegram, start
again too.

## The voice

- Write whole sentences, joined the way people talk: with and, but, so, though, because. Let
  their length vary. A run of short clipped lines ("10 pairs. Pick the hardware.") is as much a
  tell as padding.
- Lead with the point. The first sentence says what you want from the reader, or what they most
  need to know. Background and details come after, on another page, or nowhere.
- One or two ideas per sentence. Joining with "and" or "so" is fine, but once a sentence goes
  "X, but Y, so Z" it has become a chain, so split it or cut a link.
- Chris loves bullet points. A list of things (what we need, what changed, what's missing) goes
  in bullets, each a bold name and a hyphen, then one plain sentence saying what it is. One
  whole-sentence line before the list says what it's for.
- Everyday words. "Way brighter", "really close", "super accurate" beat "exhibits more
  high-frequency energy".
- Say what we think, and be straight about what isn't right yet: "We think it's really close,
  though the accent is still a bit off on low notes."
- We and you. "We can't include ROMs, so you'll need to bring your own."
- Numbers as numbers: 400 notes, within 0.3 dB. Don't dress them up.
- Leave things out. Most people want the main point, and edge cases go in the guide or nowhere.
- Give the reason when people wouldn't already know it, like why we don't ship ROMs or why the
  logos on the panel are blank. Skip it when anyone would guess.
- Asides go in brackets or after a spaced hyphen - like this. No em dashes, no semicolons.

## Don't

- Colon splits: "Each pair is one loop played twice: once by..., and once by..."
- Reasons in formal dress: "...is its maker's copyrighted work, so Lemus doesn't include it."
  Keep the reason and say it the way you'd say it out loud.
- Passive voice: "is fitted", "are matched", "is checked against". Say who does it.
- Telegram style: fragments, missing "the" and "we", one short line after another.
- Narrating how we got here: "when we searched online", "after looking into it". Give the result.
- Announcing what's coming: "there are two things", "here's what we need", "the following".
- Telling readers what they'd work out anyway, like who a request post is aimed at.
- Tips nobody asked for: "Headphones help."
- Explaining what's already on screen.
- Reassurance: "sends nothing about you". If privacy needs saying, one plain line, once.
- Lists of three for rhythm, and sentences that all come out the same length.
- Every caveat at once. If a sentence needs "until it has found any" or "and win over these",
  that detail lives somewhere else.
- Marketing words: faithful, authentic, meticulous, stunning, seamless.
- Exclamation marks on the site. (Fine in mail and forum posts.)

## Before and after

> Lemus is an independent project emulating synthesizers, drum machines and effects no longer in
> production. It documents their hardware and recreates it in software. Each machine is a free
> plugin.

Lemus makes free plugins of synths, drum machines and effects that aren't made any more. Each one
runs its original firmware if it has any, and we model its circuits from the schematics.

> A machine's firmware is its maker's copyrighted work, so Lemus doesn't include it. A machine
> with a CPU runs its original program from your own ROM image; MAME sets work as they are.

We can't include ROMs because the makers still own the copyright, so if a machine needs one
you'll have to bring your own. If you have it as a MAME zip, that works as it is without
unpacking.

> Once a day, while a plugin window is open, the plugin fetches one small file from lemusdev.org
> and sends nothing about you. When a new version is out, a window lists what changed: Download,
> Remind me later (again in a week) or Skip this version (until the next one).

The plugin checks for a new version once a day, and you can turn that off in About.

> Low in CUT OFF, both real units lose a band around 260–330 Hz on accented notes, up to 16 dB on
> one harmonic, that ro-mono-303 keeps. Plain notes match.

With CUT OFF turned down, our accented notes still come out a bit too full. Both real 303s lose a
chunk of sound around 300 Hz on those notes and ours doesn't, though plain notes match.

> Each pair is one loop played twice: once by a hardware unit, recorded, and once by ro-mono-303
> with the same notes and knob settings. Pick the hardware. A round is ten pairs.

Each pair is the same loop played on real hardware and on ro-mono-303, with the same notes and
knob settings. See if you can tell which one is the hardware - there are ten pairs in a round.

> The VS schematics and OS are already out there, but there are two things we couldn't find when
> we briefly searched online, so they'd have to come from owners.

To emulate the Prophet VS we'd need these from owners. We couldn't find them online in a brief
search.

- **I-625 documentation** - how the custom chip that steps through the wavetables works.
- **Dry recordings** - several real units with no chorus or effects.

For forum posts, Chris's own replies are the model: "Yeah, the circuit solver went crazy here,
and my tests didn't catch it. It should be fixed in 0.1.2. Let me know if it isn't!"

## Before it goes up

- Read it out loud. Would you say it like that to a friend who owns a 303?
- Cut what doesn't need saying, but keep the words that make it a proper sentence.
- Shorter must still be true. Check every claim against every machine it covers: a dropped
  "if" or "some" turns a fact into a wrong one.
