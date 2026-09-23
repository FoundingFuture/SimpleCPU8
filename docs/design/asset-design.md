# Asset pipeline design

Uploading a file, seeing what is inside it, and dragging its parts into ROM.
Then clicking any of them to look at it. Today the upload works. The assembler
places blobs. Everything between those two ends is missing. This design fills
the gap. Eddie chose every fork below.

## Contents

- [Requirements](#requirements)
- [What exists today](#what-exists-today)
- [The asset model](#the-asset-model)
- [Palette classification](#palette-classification)
- [Scaling and the quantize path](#scaling-and-the-quantize-path)
- [Audio conversion](#audio-conversion)
- [The assets pane](#the-assets-pane)
- [Dragging into ROM](#dragging-into-rom)
- [Assembler changes](#assembler-changes)
- [Previews](#previews)
- [Project files](#project-files)
- [Module layout](#module-layout)
- [Testing](#testing)
- [Decisions taken](#decisions-taken)
- [The sample ceiling](#the-sample-ceiling)
- [Not delivered](#not-delivered)
- [What comes next](#what-comes-next)
- [Open questions](#open-questions)

## Requirements

Eddie's words, turned into a list.

- Upload a file and see it in a pane.
- Drag it from the pane into the ROM area, where it gets a representation.
- Give it a label.
- One upload can hold more than one thing, an image and a palette for example.
  Drag each one independently.
- Hide the palette when it matches the machine's own palette.
- When the palette holds the same colors under different indexes, rewrite the
  bitmap instead of shipping a second palette.
- Show a unique palette so a program can load it into the GPU.
- Click a representation in ROM and see it. Images draw, palettes draw,
  samples play. MIDI files do not play.
- Convert on upload. The machine has no JPEG decoder, so a JPEG becomes pixel
  data. Audio becomes 8 bit 8 kHz to cut the file size.

## What exists today

- `decodeImage` in `main.ts` turns a browser-decodable image into indexed
  pixels plus a 256-entry palette. Transparent pixels become index 0. Past 255
  opaque colors it quantizes to the machine palette. See the quantize path
  below. It threw when this design was written.
- `asm.ts` places `.file('n')`, `.image('n')` and `.palette('n')` into the
  cartridge, deduplicating identical blobs by FNV-1a.
- `gpu.test.ts:363` covers placement and dedup.
- `project.ts` carries asset bytes through the save and load zip.
- The pane is one strip of chips with a delete button, `index.html:46`.

So the JPEG requirement is already met. Missing: the pane, the drag, the
sub-items, palette classification, audio conversion, and previews. A program
also has no way to learn a blob's length.

## The asset model

One upload becomes one asset with one or more sub-items. A sub-item is the
unit you drag. The asset keeps its original bytes. Every sub-item payload is
derived from those bytes, so nothing is lost and conversion can be redone.

| Upload | Sub-items | Directive |
|---|---|---|
| Image, any format the browser decodes | pixels, and palette when unique | `.image`, `.palette` |
| Audio the browser decodes, not MIDI | sample | `.sample` |
| MIDI, `.mid` or `MThd` magic | file | `.file` |
| Anything else | file | `.file` |

MIDI stays raw because `CMD_LOAD_MIDI` parses a real standard MIDI file.
Converting it would break `parseSmf`.

Detection order matters. Check the MIDI magic before offering audio decoding.
A browser may decode a MIDI file into audio. That would silently produce a
sample nobody asked for.

## Palette classification

Build a reverse lookup from the default 3-3-2 palette, packed RGB to index.
The 3-3-2 channels take only these values: red and green from
0, 36, 73, 109, 146, 182, 219, 255 and blue from 0, 85, 170, 255.

For each color the image actually uses, look it up.

- Every color found: the image is on-palette. Rewrite its pixels to the 3-3-2
  indexes and offer the pixels sub-item alone. No palette to drag.
- Any color missing: the palette is unique. Offer both sub-items.

This is Eddie's rule widened one step. He described a palette holding the same
colors under different indexes. Almost no sprite uses all 256 entries, so that
exact reading would fire on nearly nothing. Testing each used color for
membership catches art already quantized to the machine, whatever its index
order. It collapses to Eddie's case when the sets do match.

One collision to know about. `decodeImage` gives transparent pixels index 0,
and 3-3-2 index 0 is black. An on-palette image that uses opaque black sends
it to index 0 as well, so black and transparent merge. That is the behavior
`decodeImage` already has.

This is the one case where remapping loses something, so it is the one case
the card mentions. It says so only when the image actually holds opaque black.
A remap with no merge stays quiet.

## Scaling and the quantize path

Added after the sections above were written. Both behaviors ship.

An image whose longer side passes 256 scales to the screen's size first. That
scaling is nearest-neighbour. No color appears that the upload did not already
hold. The count below therefore stays exact for art drawn on a palette.

Counting the distinct opaque colors then decides the rest. Under 256 they
become the image's own palette, unchanged. Past 255 an 8 bit index cannot hold
them. The same scaled box is redrawn with smoothing, and every color snaps to
its nearest entry in the machine palette. Smoothing downscales a photograph
far better. The in-between shades it invents cost nothing once every color is
snapping to the palette anyway. Only a quantized image takes that second
draw.

`quantizeToPalette` and `conversionNote` in `assets.ts` hold the decisions.
Only the two canvas draws live in `main.ts`.

Index 0 is transparent wherever a sprite is stamped, so a dark pixel that
snaps there disappears. That is not the merge above. Nothing collapsed, a
color landed on index 0. The two paths say so in their own words,
`BLACK_MERGE_NOTE` and `QUANTIZE_BLACK_NOTE`.

A row's note carries whatever the conversion changed: truncation, scaling,
quantizing, and either black case. A conversion that changed nothing stays
quiet.

## Audio conversion

`decodeAudioData` on an `OfflineAudioContext` reads the upload without needing
a user gesture. Render it through a second `OfflineAudioContext` built with one
channel at 8000 Hz, which downmixes and resamples in one pass. Then quantize
each float to a byte, `round((v * 0.5 + 0.5) * 255)`, clamped to 0..255.

The result is unsigned 8 bit mono at `AUDIO_RATE`, which is the format
`CMD_DEF_SAMPLE` expects.

A sample longer than 65535 bytes cannot have its length expressed in the APU's
`APU_ARG` pair. Conversion truncates there, and the card states the cut. See
the sample ceiling below.

## The assets pane

The right pane gains two tabs in edit mode, Manual and Assets. The manual keeps
its follow-typing behavior while its own tab is active.

Each asset is a card: name, and pixel size for an image or byte count for
anything else. Under it, one row per sub-item with its byte count, a drag
handle, and a preview button. A row that lost something in conversion carries
its note underneath.

```text
[Manual] [Assets]
+-----------------------------+
| cat.png              64x64  |
|  pixels   4096 B      drag  |
|  palette    51 B      drag  |
|                             |
| boom.wav           7382 B   |
|  sample   7382 B      drag  |
+-----------------------------+
```

Fixed row heights, so the pane does not reflow as assets load.

The card shipped without the thumbnail, the format label, and the color count
this section first drew. Audio shipped without its duration and rate. See
[Not delivered](#not-delivered).

## Dragging into ROM

The ROM area is the `.data` section of the source. A drop inserts a real line
of assembly. No new syntax: the source stays the portable truth, and a project
zip or a copy-paste carries everything.

```asm
.data
cat:    .image('cat.png')
catpal: .palette('cat.png')
boom:   .sample('boom.wav')
tune:   .file('song.mid')
```

The label has to be unique against the labels already in the source. That is
known at the drop, not at the pick-up. So the drag carries an asset and
sub-item identifier on a private MIME type. The line is built on drop.
A `text/plain` copy rides along with a best-effort label. Dragging into
another text editor then still produces something sane.

Reading the drop point inside a textarea uses `caretPositionFromPoint`, with
`caretRangeFromPoint` as the fallback for browsers that lack it.

Placement rules:

- Drop inside `.data`: insert at the drop caret, on its own line.
- Drop anywhere else: append to the end of `.data`. The pane's own drop
  button did not ship.
- No `.data` section yet: create one at the end of the source.

Labels come from the file name, lowercased, with every character outside
`a-z0-9_` replaced by `_` and a leading digit prefixed with `_`. A palette
sub-item appends `pal`. A collision with an existing label appends `2`, then `3`, counting up.
The line is ordinary text, so renaming afterwards is typing.

## Assembler changes

Two additions, both earning their place from the audio path.

`.sample('name')` places converted PCM. It sits beside `.file`, `.image` and
`.palette`, and keeps each directive to one honest meaning. `.file` still
places the bytes of the file.

`get_sizelo(label)` and `get_sizehi(label)` resolve to the low and high bytes of
the blob length at that label. Without them a program hard-codes a byte count
that goes stale the moment the asset is replaced.

```asm
boom:   .sample('boom.wav')

        OUT APU_ARG,  get_sizelo(boom)
        OUT APU_ARG2, get_sizehi(boom)
        OUT APU_CMD,  CMD_DEF_SAMPLE
```

The assembler records a blob length for every label that sits on an asset
directive. Both macros error on a label that names anything else, because a
`db` line has no single length to report.

`Assets` gains `samples?: Map<string, Uint8Array>`. The assembler keeps taking
decoded data and doing no decoding of its own. That is what lets it run in
Node and in the browser alike.

## Previews

Clicking a sub-item's button opens a centered modal, the pattern the palette
inspector already uses. Run mode hides the whole right panel, so previewing
is an edit-mode action.

- Pixels: the image at a flat 2x, nearest-neighbour. A 256 wide image previews
  at 512. Eddie asked for a fixed doubling of the display format, not a zoom
  chosen to fill the modal. It draws through the palette it will actually use.
  That is 3-3-2 on-palette, and the asset's own palette otherwise.
- Palette: a 16 by 16 grid of swatches, index and hex on hover.
- Sample: no modal opens. The button reads play and the sound plays at once.
  It goes through the existing Web Audio context, at the master gain the audio
  chip runs at. The click is the user gesture a browser requires. One preview
  plays at a time, so a second click stops the first.
- File: a hex dump of the first 256 bytes.

The waveform, and the track and tempo header for a MIDI file, did not ship.
See [Not delivered](#not-delivered).

## Project files

The zip keeps original bytes, never converted ones. Conversion is derived, so
it reruns on load. This keeps a project reproducible and lets a later change to
the conversion improve old projects.

Audio decoding is asynchronous, so project load becomes asynchronous too. The
loop stays sequential, in the zip's own order, and repaints after each asset
resolves. Card order follows insertion, so a project reloads the same way
every time. There is no pending card: a card appears once its sub-items are
ready.

Assets never reach autosave, which keeps text alone. A browser refresh
therefore empties the pane, and an `.image` line in the restored source then
fails to assemble. A project zip is the only way to carry assets across a
refresh.

## Module layout

Browser APIs and pure logic split, so the logic is testable headless.

- `packages/ui/src/assets.ts`, new. Palette classification and remapping, label
  derivation, directive line building, PCM quantization, MIDI sniffing,
  sub-item modelling. No DOM, no canvas, no audio context.
- `packages/ui/src/main.ts`. `createImageBitmap`, `decodeAudioData`, the pane,
  the drag handlers, the modal.
- `packages/core/src/asm.ts`. `.sample`, the two size macros, blob lengths.

## Testing

Test first, as always.

- `assets.test.ts`: an on-palette image remaps and offers no palette sub-item.
  An off-palette image offers both. Label derivation, including collisions,
  leading digits and awkward characters. Directive line building. Float to byte
  quantization at the rails and the midpoint. MIDI sniffing by magic, not by
  extension alone.
- `asm.test.ts`: `.sample` places bytes and dedups. `get_sizelo` and
  `get_sizehi` resolve, and error on a `db` label.
- `session.test.ts`: a program that defines a sample from a `.sample` blob and
  triggers it produces audio.
- `smoke.mjs`: upload a small generated PNG, see the card, drag a sub-item into
  `.data`, assemble, open a preview.

Coverage tests already pin derived documentation to the machine. The manual
page listing the `.data` directives needs the new entries, and so does the
macro list. The project notes need their asset section rewritten.

## Decisions taken

Eddie chose each of these.

- The ROM area is the `.data` section, and a drop inserts a real directive
  line. No `<<FILE: name>>` syntax, and no generated ROM pane.
- A palette counts as ours when every color the image uses exists in 3-3-2.
- Previews open in a modal overlay.
- Converted audio reaches ROM through a new `.sample` directive.
- Blob length comes from `get_sizelo` and `get_sizehi`.
- The pane is a tab beside the manual in the right pane.
- An on-palette card says nothing about the remap. It is lossless: the same
  colors, the same picture, different indexes into the machine's own palette.
  A conversion that changes nothing visible is not worth a line of UI.
- A conversion that does lose something says so. Truncation and the black
  merge below were the two cases at the time. Scaling and quantizing came
  later and report the same way.
- A sample past the ceiling is truncated, not refused.
- A preview draws an image against its own palette only. Auditioning one
  image against another's palette belongs to the sprite editor.

## The sample ceiling

`CMD_DEF_SAMPLE` builds its length from two byte latches, `apu.ts:205`:
`slot.length = (arg2 << 8) | arg`. The largest value is 65535, so a full 64KB
is one byte out of reach. Conversion truncates at 65535 bytes, which is 8.19
seconds at 8 kHz. The card states the cut so the loss is visible.

## Not delivered

Promises above that the browser half did not ship. They are listed rather than
deleted, so a later stage knows what was intended and what was dropped. None
is refused, and none is scheduled.

- The card thumbnail for an image.
- The format label and the color count on the card.
- The duration and sample rate on an audio card.
- The pane's own drop button, an alternative to dragging onto the editor.
- The waveform in a sample preview. A sample plays instead.
- The track and tempo header in a MIDI file preview.
- Previews in run mode. Run mode hides the right panel, so the pane and its
  modal are edit-mode only.
- A pending card while a project's assets decode. Cards appear as they
  resolve.

## What comes next

A sprite editor for bitmap editing against a selectable palette. It is not
part of this work. Two choices here keep the door open for it.

Palettes already stand on their own. A palette sub-item drags on its own, and
is classified apart from the image it arrived with. An editor can therefore
offer any loaded palette as the one to draw against.

The preview renderer takes an explicit palette argument rather than reading
one from the asset. Auditioning an image against a chosen palette is then the
same call with a different argument. The sprite editor reuses the renderer
instead of growing a second one.

## Open questions

None. Every fork above is decided.
