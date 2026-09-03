# SampleTrap Version 4 Requirements

## Purpose

Add reliable note-held playback and direct MIDI control without compromising the zero-latency, low-CPU audio path.

## Functional requirements

1. SampleTrap accepts MIDI Note On and Note Off events directly.
2. Every pad starts with no MIDI note assigned.
3. MIDI Learn assigns the next Note On event to the selected pad.
4. A MIDI note can belong to only one pad; learning it elsewhere moves the assignment.
5. MIDI assignments persist with plugin state. Recorded audio remains memory-only.
6. Right-clicking a pad clears its MIDI assignment.
7. ONE SHOT mode retriggers on Note On and ignores Note Off for playback.
8. HOLD mode retriggers on Note On and stops on Note Off.
9. MIDI All Notes Off and All Sound Off release every held pad.
10. Record Arm always records from Note On until Note Off, independent of playback mode.
11. The existing UI, keyboard test controls, and host gate parameters remain available.

## Real-time requirements

1. MIDI events are handled at their sample offsets within the audio block.
2. No allocation, locks, file access, logging, or ValueTree mutation occurs on the audio thread.
3. Sample storage remains fixed at 16 pads by five seconds and is allocated only in `prepareToPlay`.
4. HOLD release uses a fixed 32-sample ramp to suppress discontinuity clicks without reported latency.
5. SampleTrap reports zero samples of plugin latency.

## Acceptance checks

1. Learning a note updates the pad label and survives closing/reopening the plugin state.
2. Relearning a note removes it from its previous pad.
3. At a 64-sample host buffer, Note On capture and playback begin at the MIDI event offset.
4. Record Arm stops capture on Note Off.
5. ONE SHOT continues after Note Off; HOLD stops after its 32-sample release.
6. No audible dry-input monitoring occurs unless Monitor Input is enabled.
