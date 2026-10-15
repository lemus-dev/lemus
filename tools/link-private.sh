#!/bin/sh
# Links each machine's private material (manuals, schematics, papers, reference recordings and
# renders, traced brand marks: everything this repo must not contain) into machines/<id>/private,
# so the docs and tools find it at the paths they use. git ignores those links.
#
# The private folder is ../private next to this repo, or $LEMUS_PRIVATE.
set -e
cd "$(dirname "$0")/.."
for m in machines/*/; do
	id=$(basename "$m")
	if [ -n "$LEMUS_PRIVATE" ]; then
		target="$LEMUS_PRIVATE/$id"
		check="$target"
	else
		target="../../../private/$id"
		check="../private/$id"
	fi
	if [ -d "$check" ]; then
		ln -sfn "$target" "machines/$id/private"
		echo "machines/$id/private -> $target"
	fi
done
