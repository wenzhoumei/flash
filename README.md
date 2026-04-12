flash is a simple pure plaintext flashcard tool designed for use alongside
LLMs to rapidly generate and cram from slopdecks.

Every line represents one flashcard in the form `question:::answer`. The simple
plaintext-only format allows quick generation and iteration, for both humans
and LLMs, while still being expressive enough for most use.

Failed cards are tracked in-place as a retry stack, for an effective
study-until-no-more-fails workflow.


Dependencies

You need Xlib and Xft to build flash.

Demo

To get a little demo, just type

	make && ./flash example.deck

You can flip with `Space`, then mark remembered with `j` or failed with `k`.
Press `n` to skip to the next card without marking it either way. Press `b` to
go back one card and undo its recorded state. Press `x` to save and exit
keeping only cards marked with `k`. Press `p` to save and exit keeping failed
cards plus unseen cards. Press `Escape` to exit without saving anything.


Usage

	flash -h
	flash -p
	flash [-r] DECK ...

`-h` prints usage. `-p` prints the compiled-in LLM deck-generation prompt.
`-r` resets each deck back to its top layer before studying it.

If one or more deck arguments are given, they are shuffled together for study.
Each deck is still saved back to itself.

The retry behavior depends on how you exit:

- `x` keeps only cards explicitly marked with `k`
- `p` keeps cards marked with `k` plus cards left unseen
- `Escape` exits without saving
- closing the window uses the compile-time `closemode` in `config.h`

flash uses a stack-based in-place format. The active cards are always taken from
the bottom of the stack, meaning the cards directly above the last separator. A
separator looks like this:

	# SEP 2026-04-11T22:00:00 3/5

where the counters are `successes/attempts` for the layer above that separator.
If not all cards in that layer were attempted, flash writes
`successes/attempts/total` instead.

At the end of a run, flash rewrites the separator below the studied layer to
store that layer's metadata. If there are cards to retry under the chosen exit
mode, flash appends those cards and a new trailing `# SEP`. If you clear the
entire bottom layer, flash removes it instead, unless it is the top layer of
the file.

In `x` mode, "remaining cards" means only cards explicitly marked with `k`.
In `p` mode, it means cards explicitly marked with `k` plus cards not yet seen.
If a deck is only skipped and no cards are answered, its file is left unchanged.

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
