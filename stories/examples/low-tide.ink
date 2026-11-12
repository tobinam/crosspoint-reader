# lang: en
// Original example story for this personal firmware. CC0-1.0.
// Compile with inklecate, then the pinned eenk InkCPP compiler.
VAR has_lantern = false

The tide has gone out farther than anyone remembers. From your USB cradle on the harbour desk, a tiny radio whispers: "One light is still missing."
You take your coat and step outside.

* [Visit the lighthouse] -> lighthouse
* [Follow the footprints across the sand] -> sand

=== lighthouse ===
The keeper has fallen asleep beside a cold cup of tea. On the table sits a brass lantern, warm although it has no flame.

* [Borrow the lantern]
    ~ has_lantern = true
    You leave a note beneath the keeper's cup.
    -> sand
* [Leave it and walk down to the shore] -> sand

=== sand ===
The footprints end at a little boat resting on the sand. Inside, a fox in a blue scarf studies a chart of the stars.
"Are you here for the last crossing?" it asks.

* [Ask where the boat goes] -> crossing
* [Ask about the missing light] -> light
* [Head home before the tide returns] -> home

=== crossing ===
"To the place where unfinished stories wait," says the fox. "Most people take far too much luggage."
It offers you a seat.

* [Climb aboard] -> voyage
* [Ask about the missing light first] -> light

=== light ===
"Every harbour needs one person willing to carry a light for someone they have not met."
{has_lantern:
    The brass lantern glows through your coat. The fox smiles.
    * [Hang the lantern on the bow] -> lantern_ending
    * [Keep it and climb aboard] -> voyage
- else:
    Somewhere above the beach, the lighthouse window is dark.
    * [Return to the lighthouse] -> lighthouse
    * [Trust the stars and climb aboard] -> voyage
}

=== lantern_ending ===
You hang the lantern on the bow. Its light stretches across the wet sand, and a hundred small boats become visible on the horizon.
"There you are," says the fox. "We were waiting for you."
By morning, every boat has found the harbour. You return the lantern, still warm, and the keeper pours two cups of tea.
THE END - A light for strangers.
-> END

=== voyage ===
The boat lifts without a sound. Sea and sky exchange places, and the harbour becomes a constellation below you.
The fox hands you a blank chart. "Your turn to decide what happens next."
THE END - An unfinished map.
-> END

=== home ===
You reach home just as the kettle clicks off. Through the window, a tiny boat draws a silver line across the returning tide.
You put a second cup on the desk, just in case.
THE END - A place to return to.
-> END
