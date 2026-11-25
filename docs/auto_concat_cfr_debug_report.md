# Auto Concat CFR Debug Report

Date: 2026-03-15

## Scope
- Investigated auto concat flow for:
  - Source: `Episode 07/Sources/[HEVC] Threads_of_Fate_-_A_War_Untold_[07].mp4`
  - Hardsub ASS: `Episode 07/Sources/subtitles_processed_signs.ass`
- TB interval from log:
  - `tb_start=1290.000`
  - `tb_end=1323.000`
  - keyframe before TB: `1289.920`
  - keyframe after TB: `1323.920`
  - overlap trim used in seg2: `trim=start=0.280`

## Baseline findings
- Source summary (`source_video_summary.json`):
  - `r_frame_rate=25/1`
  - `avg_frame_rate=25/1`
  - packets mostly `0.040000`
- Existing auto result (`result_video_summary.json` + `result_packet_duration_dist.csv`):
  - stream rates still reported around 25 fps
  - packet duration anomalies exist: `0.080000`, `0.039989`, `0.000011`

## Reproduction of current auto flow
- Reproduced seg1/seg2/seg3 and final concat-copy join in:
  - `build/cfr_debug_auto/auto_repro/auto_like_join.mp4`
- Observed:
  - `avg_frame_rate=431025/17242` (not exact CFR ratio)
  - packet anomalies include `0.240000` and micro durations (`0.000011`)
  - debug join logs include `Non-monotonic DTS` warnings

## Tested join hypotheses
- Tested and did not fix issue:
  - `-fflags +genpts`
  - `-avoid_negative_ts make_zero`
  - `-video_track_timescale 90000`
- Those variants still produced:
  - non-monotonic DTS warnings
  - same anomalous packet durations

## Working variant (no full re-encode)
- Final join command:

```powershell
$ff="c:/Users/icehole/git/DubbingTool/build/tools/ffmpeg.exe"
$d="c:/Users/icehole/git/DubbingTool/build/cfr_debug_auto/auto_repro"
$mkv="C:/Users/icehole/Downloads/Dublyazhnaya/Threads of Fate A War Untold/Episode 07/Result/[DUB] Threads of Fate A War Untold - 07.mkv"

& $ff -y -f concat -safe 0 -i "$d/concat_list.txt" -i "$mkv" `
  -map 0:v:0 -map 1:a:0 `
  -c:v copy -bsf:v setts=pts=N*3600:dts=N*3600 `
  -c:a copy -movflags +faststart -shortest `
  "$d/join_setts.mp4"
```

## Verification
- `join_setts.summary.json`:
  - `r_frame_rate=25/1`
  - `avg_frame_rate=25/1`
- `join_setts.packet_duration_dist.csv`:
  - only `34482,0.040000`
- No non-monotonic DTS warning in this run output.

## Additional test: source MP4 run from Sources folder
- Test folder:
  - `Episode 07/Sources/cfr_mp4_test`
- Method:
  - ffmpeg launched with working directory set to `Sources`
  - subtitle filter uses relative filename:
    - `subtitles=filename='subtitles_processed_signs.ass'`
  - segment layout kept same (`seg1 copy`, `seg2 x265+hardsub`, `seg3 copy`)
- Results:
  - `join_plain.summary.json`: `avg_frame_rate=431025/17242`
  - `join_plain.packet_duration_dist.csv`: `0.240000` and `0.000011` anomalies present
  - `join_plain` run logs include `Non-monotonic DTS`
  - `join_setts.summary.json`: `avg_frame_rate=25/1`
  - `join_setts.packet_duration_dist.csv`: only `34482,0.040000`
- Conclusion:
  - Fix also reproduces on MP4 input path and removes packet-duration anomalies in this test case.

## Technical conclusion
- Root cause in this case is timestamp discontinuity/non-monotonic DTS on copy-join boundaries.
- Container-only flags were insufficient.
- Normalizing copied video packet timestamps at join via bitstream filter `setts` resolved pacing metadata anomalies without full video re-encode.
