`old-default.hocket` was written by `json::toString(defaultSong(0x0123456789abcdef))`
compiled from ced1476dce087fef8b2c9881c989dd3dda76b004, before the file/path/code rename.
The document test loads the actual file through the library and compares its 64-bar render
with the unchanged text golden. It also exercises both library folders/extensions, saving
to the new extension and preserving the original bytes.

The MIDI goldens were derived deliberately from that build's 8-bar clip and 64-bar renderer
outputs: only type-3 track names, their length bytes and enclosing track chunk lengths change.
The clip label changes from `Hocket 123456789abcdef` to `Planchette 123456789abcdef`;
the renderer labels change from `Hocket ch 1` through `Hocket ch 4` to `Planchette ch 1`
through `Planchette ch 4`. Every remaining byte is identical. The old product names here
document backwards compatibility. The document test and CTest renderer check the complete
new MIDI bytes; the existing text golden continues to pin all notes/timing/velocities.

`old-edited.hocket` and `../golden/legacy-edited.txt` were written by that same old build,
using the document test's edited song (seed c0ffee): nondefault key/scale/range/harmony,
an additional polymetric group, voice overrides, locks, generator choices/parameters/sources,
probability/ratchets/fills, matrix rows and two Generates. Its expected 64-bar render differs
from `defaultSong` on the same seed, so reconstructing a default song and discarding edits fails.
The new build must load the file through the library and reproduce every golden byte.
