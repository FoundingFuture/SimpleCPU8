# Audio device design

The APU, an audio peripheral for SimpleCPU-8. This is a design Eddie asked for
while away. It captures the requirements he gave and picks a default for every
open question. Each default is marked so Eddie can veto it. The parts already
built under this design are marked as built.

The APU is a magic box, like the GPU. The CPU stays 8 bit and honest. It spends
one OUT to trigger a sound or start a tune, then moves on. The chip does the
mixing, the pitch shifting, and the sequencing.

## Contents

- [Requirements](#requirements)
- [The time model](#the-time-model)
- [Voices and tracks](#voices-and-tracks)
- [Instruments](#instruments)
- [The drum track](#the-drum-track)
- [ROM layout](#rom-layout)
- [Ports and commands](#ports-and-commands)
- [MIDI playback](#midi-playback)
- [Output and the UI](#output-and-the-ui)
- [Build status](#build-status)
- [Open questions](#open-questions)

## Requirements

Eddie's words, turned into a list.

- Play samples on demand when a game detects an event.
- Play music from a MIDI file.
- Eight tracks, with eight sample players on each track.
- Play samples at different pitches.
- Play a MIDI file with notes routed to the eight tracks.
- Polyphony on at least one track, preferably all.
- A drum track where each octave has its own sample.
- Banked access to ROM for sample data and the MIDI file.
- One tune at a time.

## The time model

This is the one real fork, so it comes first. The GPU frame counter ticks every
65536 cycles. It never uses wall-clock time. That keeps the same program drawing
the same pixels at every speed.

Audio is different. A tune has a tempo in seconds. A sound effect must play at
the right pitch the instant a game fires it. Tying the sample rate to cycles
would make music slow down at slow speeds and rise in pitch at MAX.

Default, open for veto: the APU plays in real time on its own clock, decoupled
from CPU cycles. The CPU triggers a sound and returns at once. The chip renders
the audio in wall-clock time. This is a peripheral doing its own work for free,
so the CPU's own clock stays honest. It is a deliberate difference from the GPU.
The GPU is a picture you scrub at any speed. The APU is a tape that plays now.

The engine renders in fixed-size blocks of output samples. Every voice and the
sequencer advance by the block length. So playback is sample-accurate and
independent of how fast or slow the CPU runs.

## Voices and tracks

A voice is one sounding sample. It holds a source range in ROM, a play position,
a pitch step, and a level. The position and step are fixed-point, so a voice can
play a sample faster or slower than its stored rate. That is pitch shifting by
resampling.

There are eight tracks. Each track owns eight voices, its players. A note on a
track claims a free player. With all players busy, the oldest is stolen. So a
track sounds up to eight notes at once. Eight tracks give up to 64 voices. That
covers polyphony on every track.

The mixer sums every active voice, applies each level, and clamps to the output
range. The sum uses signed integers with headroom, then maps back to 8 bit.

## Instruments

Each track carries one instrument. An instrument says how a note picks a sample
and a pitch. There are two modes.

Melodic mode holds one sample and a root note. A note-on plays that sample. The
pitch step is `2^((note - root) / 12)` times the base step. So the root note
plays the sample at its stored rate, and higher notes play it faster.

Keymap mode holds a table of samples across the keyboard. A note-on looks up the
table, plays the sample it finds, and shifts pitch from that entry's own root.
The drum track uses this mode.

## The drum track

Eddie asked for a drum track where each octave has its own sample. Keymap mode
serves it. The table has one entry per octave, so 11 entries cover MIDI notes 0
to 127. A note plays its octave's sample.

Default, open for veto: the note's semitone inside the octave shifts the sample
pitch from that entry's root. So one drum sample gives 12 pitches, and the 11
octaves give 11 different drums. A pure drum kit uses one pad per octave and
ignores the pitch spread. A tuned kit uses the 12 steps.

## ROM layout

The APU reads the same cartridge the GPU reads. It is 1MB, 16 banks of 64KB,
addressed by a bank byte and a 16 bit offset. The CPU cannot read the cartridge.
Only the peripherals can. Samples and the MIDI file live there through the
`.data` section, the same `.file('name')` path the GPU uses for images.

A sample is raw 8 bit unsigned PCM. Center is 128. A sample slot records its
bank, its start offset, its length, a root note, and a loop flag. The chip keeps
a table of sample slots. A program defines a slot by pointing the cartridge
latches at the data and issuing a define command.

The MIDI file is a standard MIDI file placed in the cartridge. A load command
parses it into an internal event list. Only one file is loaded at a time.

## Ports and commands

The APU claims ports 0x30 to 0x3F. It forwards the rest to its fallback, the
same chain idiom the GPU and the controller use. The names below are built-in
assembler constants, like the GPU port names.

The write ports are latches and one command port.

- APU_CART_LO, APU_CART_HI, APU_CART_BANK: the ROM address for the next define
  or load, exactly like the GPU cartridge latches.
- APU_TRACK: select the track, 0 to 7, for the next instrument or note command.
- APU_NOTE: the note number, 0 to 127, for the next note command.
- APU_ARG, APU_ARG2: scalar arguments. Velocity, length, root, mode, and slot
  id ride here, read by whichever command needs them. A command that reads
  APU_ARG as a mode tests it for one exact value, so unused values stay free.
- APU_SLOT: select the sample slot for a define or an instrument bind.
- APU_CMD: run a command. The value picks the command.

The commands.

- CMD_DEF_SAMPLE: define APU_SLOT from the cartridge latches. Length is the ARG
  pair. Root is APU_NOTE. It clears the slot's loop flag, because defining a
  sample defines the whole slot.
- CMD_SET_INSTRUMENT: bind the selected track to a slot and a mode.
- CMD_KEYMAP_ENTRY: set one octave entry of the selected track's keymap to a
  slot. The octave is APU_ARG.
- CMD_NOTE_ON: start a note on the selected track. Note is APU_NOTE, velocity is
  APU_ARG.
- CMD_NOTE_OFF: stop a note on the selected track, or all notes when APU_ARG is
  zero.
- CMD_TRIGGER: one-shot a slot at a note in one command, the sound-effect path.
  Slot is APU_SLOT, note is APU_NOTE.
- CMD_LOAD_MIDI: parse the MIDI file at the cartridge latches.
- CMD_PLAY: start the loaded tune from the beginning.
- CMD_STOP: stop the tune and silence its voices.
- CMD_STOP_ALL: silence every voice at once, the tune left alone.
- CMD_LOOP_SLOT: set the loop flag on APU_SLOT. APU_ARG 1 repeats the sample,
  0 plays it once. A looping voice never ends on its own, so a program stops it
  with CMD_NOTE_OFF, CMD_STOP, or CMD_STOP_ALL. A voice copies the flag when it
  starts, so the command never changes a note already sounding.

The read ports report state, the read-port idiom of GPU_FRAME.

- APU_STATUS: bit 0 is set while a tune plays. A program polls it to know when a
  tune ends.
- APU_VOICES: the count of active voices, for a meter or for debugging.

## MIDI playback

The loaded file drives the tracks on its own. A note-on in the file becomes a
CMD_NOTE_ON on a mapped track. A note-off becomes a note-off. The tempo comes
from the file's tempo events. Delta-times become sample counts at render time.

Default, open for veto: MIDI channel N maps to APU track N for channels 1 to 8.
Channel 10, the drum channel, maps to the track the program set as the drum
track. A program can set the channel-to-track map before playing.

The parser reads standard MIDI files of format 0 and format 1. It merges all
file tracks into one time-ordered event list. It reads note-on, note-off, tempo,
and program-change events. It ignores the rest for now.

## Output and the UI

The chip renders 8 bit unsigned mono PCM at a nominal 8 kHz. The core exposes a
pull method, like the GPU's compose-frame method. The UI worker calls it to keep
an audio buffer full. The main thread feeds that buffer to a Web Audio context,
which resamples 8 kHz up to the device rate.

Default, open for veto: mono output at 8 kHz. Stereo and a higher rate are later
options. The retro rate keeps ROM small and fits the stated 8 bit 8 kHz plan.

## Build status

Built parts carry tests in the core package. The list updates as the work lands.
See docs/changelog.md for the running history.

## Open questions

Answers Eddie may want to change.

- The real-time clock versus a cycle-locked clock. The default is real time.
- The keymap pitch spread on the drum track. The default shifts within an octave.
- Mono versus stereo, and 8 kHz versus a higher rate. The default is mono 8 kHz.
- Voice stealing order. The default steals the oldest voice on the track.
- The channel-to-track map for MIDI. The default is channel N to track N.
- Loop support in samples. Built as a whole-sample flag, CMD_LOOP_SLOT. A loop
  range inside a sample, with a separate attack, is still open.
- Ping-pong looping, where a sample plays forwards then backwards. Not built.
  CMD_LOOP_SLOT reads APU_ARG as a mode and tests it for 1, so the values above
  it are free to carry this.
