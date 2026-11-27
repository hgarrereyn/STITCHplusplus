# Launch film

A ~20 s launch video for STITCH++, drawn on a canvas as a pure function of time and rendered frame by frame.

- `stitch.html` — the film. Open it in a browser to preview (`?t=8` starts at 8 s).
- `render.cjs` — renders it to H.264 with Playwright + ffmpeg and dumps the sound cues: `node render.cjs out.mp4 60`
- `sound.py` — synthesizes the soundtrack from those cues: `python3 sound.py out.events.json sound.wav`
- `stills.cjs` — renders single frames for checking: `node stills.cjs <dir> 3.5 9 13.4`

Mux: `ffmpeg -i out.mp4 -i sound.wav -c:v copy -c:a aac -b:a 192k -shortest stitch-launch.mp4`
