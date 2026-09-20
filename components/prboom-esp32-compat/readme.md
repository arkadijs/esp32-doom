## Music and SFX

dbopl.c dbopl/*.c i2s_dac.c i_sound.c midifile.c — files form the music/SFX audio pipeline for the ESP32 port.
Two independent signal paths (OPL music and PCM sound effects) converge in i_sound.c's mixer.

```
game code (s_sound.c, mus2mid.c)
      │
      ▼
 i_sound.c            ← Doom's I_* sound API, PCM SFX mixer, I2S audio task
      │  render()            │
      ▼                      ▼
dbopl/oplplayer.c        i2s_dac.c (DMA out)
 (MIDI → OPL registers)
      │  OPL_WriteRegister() / OPL_Render_Samples()
      ▼
dbopl/opl.c              ← OPL2 port/register emulation, event-timing queue
      │  Chip__WriteReg() / Chip__GenerateBlock2()
      ▼
dbopl.c               ← DOSBox's actual OPL2/OPL3 FM synthesis chip model

midifile.c            ← used by oplplayer.c to walk MIDI events, independent layer
```

### i_sound.c — top of the stack, owns the mix

This is Doom's I_* sound HAL implementation and the only file that talks to FreeRTOS/I2S directly.

- SFX side (self-contained): LoadSfx() parses Doom's raw PCM lump format (8-byte header + samples) straight out of the WAD via W_CacheLumpNum. I_StartSound/I_StopSound/I_UpdateSoundParams write into a fixed array of 32 mix_channel_t slots (a fixed-point resampling step,
left/right gain, playback position) guarded by audio_mutex.
- Music side: it never touches OPL registers itself. It holds one pointer, music_player = &opl_synth_player (defined at the bottom of oplplayer.c), and calls through that vtable: music_player->init/play/pause/render/registersong/.... All the
I_PlaySong/I_RegisterSong/etc. functions are thin, mutex-protected forwarders to that struct.
- AudioTask (its own FreeRTOS task, pinned to core 1) is the actual real-time loop: for each 256-frame buffer it calls music_player->render(music, MIX_FRAMES) (→ oplplayer.c → opl.c → dbopl.c), mixes the 32 SFX channels sample-by-sample on top of that OPL output, clips
to 16-bit, and blocks on i2s_dac_write(). That blocking DMA write is what paces the whole loop to real time.

### dbopl/oplplayer.c — MIDI-to-OPL music driver

This is the adapted Doom GENMIDI player (originally Simon Howard's i_oplmusic.c). It never generates audio samples itself — it turns MIDI events into OPL register writes:

- I_OPL_InitMusic calls OPL_Init() (in opl.c) then loads the GENMIDI WAD lump (instrument definitions) via LoadInstrumentTable.
- I_OPL_RegisterSong hands raw MIDI bytes to midifile.c's MIDI_LoadFileMem, getting back a midi_file_t* used as the opaque "handle" the rest of Doom passes around.
- I_OPL_PlaySong → StartTrack/ScheduleTrack for each track: it reads the next event's delta-time via MIDI_GetDeltaTime, converts ticks→milliseconds, and calls OPL_SetCallback(ms, TrackTimerCallback, track) — this doesn't use a wall-clock timer, it schedules the callback
for a specific sample time in opl.c's queue.
- TrackTimerCallback calls MIDI_GetNextEvent (midifile.c) and dispatches to KeyOnEvent/KeyOffEvent/ControllerEvent/etc., which allocate one of 9 opl_voice_t slots and call OPL_WriteRegister() (frequency, volume, feedback, waveform registers) — this is the only way this
file touches opl.c.
- I_OPL_RenderSamples is just OPL_Render_Samples() — a pass-through into opl.c.

### dbopl/opl.c — OPL port/register emulation + event scheduler

This is the glue layer that makes an abstract "OPL chip with register ports and a timer" look real to oplplayer.c, while actually driving the DOSBox chip model:

- OPL_WriteRegister/OPL_WritePort implement the classic two-port (address/data) OPL protocol with the exact read-delay dance real AdLib code expects, then hand off in WriteRegister() to Chip__WriteReg(&opl_chip, reg_num, value) in dbopl.c — except registers 0x02–0x04,
which are software timers emulated locally (opl_timer_t) since dbopl doesn't model the OPL's own timers.
- opl_callback_queue_t (from opl_queue.c, a small binary min-heap capped at 64 entries) holds TrackTimerCallback events keyed by absolute sample count. OPL_SetCallback pushes into it; OPL_AdvanceTime pops and fires anything due.
- OPL_Render_Samples is the key timing loop: it never renders a whole buffer blindly. It looks at the queue, generates only as many samples as remain until the next scheduled MIDI event (FillBuffer), advances time, lets that event fire (which may issue more
Chip__WriteReg calls), then continues — so register state always changes at the exact sample boundary the MIDI file specifies.
- FillBuffer calls Chip__GenerateBlock2(&opl_chip, nsamples, mono) — mono because Doom's OPL driver only uses the 9-channel OPL2 register set — applies mus_opl_gain (boosted to 300 here since dbopl's raw output is quiet next to full-scale PCM SFX), clips, and duplicates
the mono sample to stereo.

### dbopl.c — the actual synthesis chip model

DOSBox's OPL2/OPL3 (Yamaha YMF262) software emulator, the lowest layer. It knows nothing about Doom, MIDI, or FreeRTOS — just:
- DBOPL_InitTables() — one-time global exponential/log wavetable setup (guarded by a doneTables static, called once regardless of how many times OPL_Init runs).
- Chip__Chip() / Chip__Setup(rate) — construct/reset the 18-channel Chip struct and precompute rate-dependent tables for the requested output rate (this is why it's cheap: unlike a cycle-accurate core, it synthesizes directly at 22050 Hz rather than oversampling and
decimating).
- Chip__WriteReg() — mutates operator/channel state per OPL register write.
- Chip__GenerateBlock2() — advances the LFO/envelope/phase state and sums each channel's FM output into the caller's buffer.

### midifile.c — MIDI parser, orthogonal to the OPL chain

Doesn't call or get called by opl.c/dbopl.c at all — its only consumer is oplplayer.c, which uses it purely as a sequential event reader: MIDI_LoadFileMem parses the standard MIDI chunk format into midi_track_iter_ts, and
MIDI_GetDeltaTime/MIDI_GetNextEvent/MIDI_RestartIterator let oplplayer.c walk events track-by-track without needing to understand the MIDI byte format itself. s_sound.c/mus2mid.c (not shown here) convert Doom's native MUS lumps into this same MIDI byte stream before it
ever reaches midifile.c.

### i2s_dac.c — bottom of the stack, no logic of its own

Purely an ESP-IDF I2S driver wrapper: i2s_dac_init() configures the STD I2S channel (pins, 16-bit stereo, I2S_DAC_SAMPLE_RATE=22050 Hz), i2s_dac_start() enables it, i2s_dac_write() blocks on i2s_channel_write(). It has no knowledge of OPL, mixing, or Doom at all —
AudioTask in i_sound.c is its only caller, once per 256-frame buffer.

### Concurrency

Everything from oplplayer.c down to dbopl.c is single-threaded and not reentrant-safe on its own — it's i_sound.c's audio_mutex that makes it safe to call from both AudioTask (rendering, on core 1) and the main Doom game task (on core 0, calling
I_PlaySong/I_RegisterSong/etc. as gameplay triggers music changes). Every entry point in i_sound.c takes that mutex before touching music_player.
