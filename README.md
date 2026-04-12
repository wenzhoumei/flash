flash is a simple pure plaintext flashcard tool, designed to be used alongside
LLMs to quickly generate some slopdecks and start cramming from them. Every
line represents one flashcard in the form `question:::answer`. Wrong answers
are appended in a stack-based in-place format for easy cramming.


Dependencies

You need Xlib and Xft to build flash.

Demo

To get a little demo, just type

	make && ./flash example.deck

You can flip with `Space`, then mark remembered with `j` or failed with `k`,
quit early with `x`, or press `Escape` to exit without saving anything.


Usage

	flash -h
	flash -p
	flash [-r] [-j | -k] DECK ...

`-h` prints usage. `-p` prints the compiled-in LLM deck-generation prompt.
`-r` resets each deck back to its top layer before studying it.

If one or more deck arguments are given, they are shuffled together for study.
Each deck is still saved back to itself.

By default, flash keeps only cards explicitly marked with `k` in the retry
layer. `-j` instead keeps every card that was not explicitly marked with `j`,
which includes wrong cards and cards left unseen when you exit early. The
default is configurable in `config.h`.

flash uses a stack-based in-place format. The active cards are always taken from
the bottom of the stack, meaning the cards directly above the last separator. A
separator looks like this:

	# SEP 2026-04-11T22:00:00 3/5

where the counters are `successes/attempts` for the layer above that separator.
If not all cards in that layer were attempted, flash writes
`successes/attempts/total` instead.

At the end of a run, flash rewrites the separator below the studied layer to
store that layer's metadata. If anything was missed or left unanswered, flash
then appends the remaining cards and a new trailing `# SEP`. If you clear the
entire bottom layer, flash removes it instead, unless it is the top layer of
the file.

In the default `-k` mode, "remaining cards" means only cards explicitly marked
with `k`. In `-j` mode, it means every card not explicitly marked with `j`.

A deck file could look like this:

	What does CPU stand for?:::Central Processing Unit
	2 + 2:::4
	Capital of France:::Paris

After one imperfect run, the same deck might look like this:

	What does CPU stand for?:::Central Processing Unit
	2 + 2:::4
	Capital of France:::Paris
	# SEP 2026-04-11T22:00:00 1/2/3
	2 + 2:::4
	Capital of France:::Paris
	# SEP

If you then want to discard every retry layer and go back to just the top one,
run:

	flash -r DECK

Development

flash was written as its own flashcard tool, but parts of the X11/Xft rendering
approach, the centered text presentation style, and the suckless-style build
layout were adapted from sent:

	http://tools.suckless.org/sent
