flash is a simple plaintext flashcard tool.

flash does not need databases, web stacks or any other fancy file format, it
uses plaintext deck files to describe flashcards. Every line represents one
flashcard in the form `question:::answer`.

The flashcards are displayed in a simple X11 window. The content of each card
is automatically scaled to fit the window and centered so you also don't have to
worry about alignment. Instead you can really concentrate on recall.


Dependencies

You need Xlib and Xft to build flash.

Demo

To get a little demo, just type

	make && ./flash example.deck

You can flip with `Space`, then mark remembered with `j` or failed with `k`,
quit early with `x`, or press `Escape` to exit without saving anything.


Usage

	flash [-r] DECK
	flash [-r] DECK ... IDENT

If one argument is given, it is treated as a single deck. If multiple arguments
are given, all but the last are treated as decks, they are shuffled together,
and the final argument is treated as the identifier shown in the window title.
Each deck is still saved back to itself. `-r` resets each deck back to its top
layer before studying it.

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
