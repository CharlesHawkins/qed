### QED ###
QED is a text editor written circa 1967 by L. Peter Deutsch and Butler W. Lampson at Berkley for the [SDS-930 computer](https://en.wikipedia.org/wiki/SDS_930). It was the inspiration for the unix text editor [ed](https://en.wikipedia.org/wiki/Ed_(text_editor)). 

This is a c implementation I made for fun, and isn't completely finished yet. I did not have access to any source code for the original in making this, and it is based primarily on the 1968 DARPA manual by Angluin and Deutsch, with gaps filled in by (in roughly descending order) the 1967 Communications of the ACM article "An Online Editor" by Deutsch and Lampson, the behavior of ed, and my own common sense.

To get into the spirit of things, I wrote it entirely using ed and, as it matured, qed itself (with less sometimes used to look at the code).

It requires no external libraries apart from ones that come with c. To compile it, use:

	cc qed.c -o qed

It does assume you have a Unicode-compatible terminal to render the up-arrow and left-arrow glyphs shown in the manual. I've tried to reproduce the experience of using QED as closely as I can in a modern unix environment, but a few changes from the original have been necessary:
* From the manual it appears that the machine this originally ran on was upper-case only. This version has no limitation about editing upper- and lower-case text.
* The manual also suggests that the environment the original ran on had a @CONTINUE feature that seems to have allowed the user to "un-quit" the last program that was running, I assume as long as nothing else had been run in the interim that might have overwritten the first program's memory. The original relied on this in place of any kind of "there are unsaved changes, are you sure you want to quit?" warning; instead it just let you know there were unsaved changes with WRITE OUT and then quit anyway, since you could just @CONTINUE qed if you did actually want to save. Since unix doesn't have anything like @CONTINUE, my plan is currently that if the user quits without saving, qed will create a save file, maybe ~/.qed_save, with some representation of the program state, and that launching qed again with -c will restore from the save file. I haven't implemented any of that yet, though.
