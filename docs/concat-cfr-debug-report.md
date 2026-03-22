# Concat CFR/Fade Debug Report

Date: 2026-03-15

## Problem Summary
- In concat MP4 output, seam regions showed pacing artifacts:
  - VFR-like metadata anomalies (`min/max frame rate` spikes).
  - Visual impression of dropped/jittered frames.
- At first TB seam, subtitle `\fad` appeared cut (abrupt fade-in start).

## Root Causes (Confirmed)
- Final copy-join produced non-monotonic DTS at segment boundaries.
- In `seg2` filter graph, `trim(start=overlap)` was applied **after** subtitles, which cut the first part of subtitle fade-in.

## Runtime Evidence Highlights
- Plain copy-join had packet duration anomalies (`0.240000`, `0.000011`) and non-monotonic DTS warnings.
- Applying `setts` on join normalized packet durations to constant frame step.
- Signalstats comparison of first `seg2` frames showed old chain starts brighter than fixed chain, confirming fade-in truncation.

## Implemented Fixes

### 1) Subtitle fade fix (filter order)
- Old (problematic):
  - `setpts(+abs) -> subtitles -> setpts(-start) -> trim(overlap) -> setpts(-start)`
- New (fixed):
  - `trim(overlap) -> setpts(-start) -> setpts(+subtitleAnchor) -> subtitles -> setpts(-start)`
- Effect:
  - overlap is removed first;
  - subtitles are evaluated on corrected timeline, preserving visible fade-in progression at seam.

### 2) Timestamp normalization at final join
- Added `-bsf:v setts=...` next to `-c:v copy` for CFR inputs.
- This removes DTS discontinuities without full video re-encode.

### 3) Second seam stabilization (seg2->seg3)
- Added symmetric boundary correction before cutting `seg3`:
  - probe actual durations of `concat_seg1.ts` and `concat_seg2.ts`;
  - compute `cumulativeSeam = seg1Duration + seg2Duration`;
  - if `cumulativeSeam` is slightly after keyframe boundary, shift `seg3` start to `cumulativeSeam`.
- Purpose:
  - avoid backward-looking jump at second seam caused by tiny duration/timestamp mismatch near boundary.
- Scope:
  - works with current user-provided input file in manual mode (no source switching).

## CFR/VFR Safety Rule
- Do **not** hardcode `3600` globally.
- Apply `setts` only when input is likely CFR (`r_frame_rate` ~= `avg_frame_rate`).
- Build expression from actual fps rational:
  - `setts=pts=N*den/(num*TB):dts=N*den/(num*TB)`
- For VFR inputs, skip `setts` (passthrough/fallback path), otherwise cadence may be damaged.

## Validation Checklist
- `ffprobe` stream summary:
  - CFR case: `r_frame_rate` and `avg_frame_rate` should match.
- `ffprobe` packet durations:
  - no outliers like `0.240000`, `0.000011` in fixed CFR join.
- Visual seam checks:
  - first seam: no cut fade-in;
  - second seam: no fade-out regression.

## Seam1 Micro-Jump (Auto Mode) - Latest Iteration
Date: 2026-03-18

### Observed Symptom
- After reverting the subtitle-anchor regression, the first TB seam in auto mode still had a small perceived “jump forward”.

### Hypotheses Tested (with runtime evidence IDs)
1. **H21/H20** (Tail probing correctness)
   - Hypothesis: our “last seg1 frame” measurement was unreliable (seek/TS selection artifacts), causing wrong trim overlap.
   - Evidence:
     - `H21` mapped seg1 tail MD5 (last frame in 1s tail window) to `seg1TailSourceTs=1290.16`.
     - `H20` attempted tail MD5 from multiple `-sseof` windows (mostly empty for single-frame attempts, but `H21` was decisive).
   - Result: measurement instability was present in some probes, but `H21` established the correct tail anchor.

2. **H22** (True frame-gap exists: 1 frame)
   - Hypothesis: after earlier fixes, seam1 still had a content gap of approximately one frame (25fps => 0.04s).
   - Evidence (pre-fix run):
     - `H21`: `seg1TailSourceTs=1290.16`
     - `H15`: `seg2HeadSourceTs=1290.20`
     - Gap: `1290.20 - 1290.16 = 0.04s` (exactly 1 frame)
   - Fix:
     - Reduce `trim=start` overlap by exactly `frameStep` when `tailToHead ~= frameStep`.
     - `overlapUsed` moved from `0.28` to `0.24`.
   - Evidence (fix run):
     - `H22`: `appliedGapFix=true`, `overlapUsed=0.239999...`, `tailToHead=0.039999...`

3. **H23** (Post-trim mapping verifies continuity)
   - Hypothesis: after applying overlapUsed, seg2 head content should align to seg1 tail content.
   - Evidence:
     - `H23`: `seg2HeadSourceTsAdjusted=1290.16`
   - Result: content-level seam1 continuity was restored (no 1-frame gap on the measured mapping).

4. **H24** (Residual jump origin: content vs timing/PTS)
   - Hypothesis: remaining visible jump might be caused by a mismatch in final output MP4 around `tbStart` (either PTS shifting or encoder-context differences).
   - Evidence:
     - `H24` showed `outMd5 == srcMd5` for `offset=-0.04` (and some pre-seam offsets), but mismatches starting at `offset=0.0` and forward.
     - This indicates the seam is no longer a pure “missing/duplicated frame” gap; instead the first frames after `tbStart` in the encoded seg2 differ from the source’s corresponding frames (encoder GOP/context / small re-timing effects).
   - Limitation:
     - Exact seek probe variants sometimes returned empty hashes in `H24`, so conclusions about “exact vs fast seek” are based primarily on the non-exact md5 sampling.

### Outcome
- Fade-in cut regression: fixed earlier by filter reordering in seg2 (trim first, then setpts/subtitles).
- Seam1 1-frame gap: fixed in auto mode (H22/H23 proved).
- Residual micro-jump remains: evidence points away from “one missing frame” and toward “first-frame encoding context” or timing metadata behavior around `tbStart` rather than trim overlap alone.

### Join `setts`: full PTS+DTS vs dts-only (H24 / H27 / H28)
- **H27** (no `setts` on join): MD5 window around `tbStart` matched source more closely; MediaInfo CFR may regress.
- **H28** (`setts=pts=PTS:dts=N*den/(num*TB)`): better seam MD5 than full `setts` in some offsets, but **MediaInfo went back to VFR-like reporting** — unacceptable for this pipeline.
- **Production join** keeps **`buildSettsExprForFps` → `pts=N*den/(num*TB):dts=N*den/(num*TB)`** for CFR so metadata stays CFR; seam micro-jump vs re-encode remains a separate tradeoff.

### Next Experiment Ideas (instrumented, not implemented yet)
- Encode-context/GOP control for seg2:
  - Force stable GOP boundary around the seam:
    - constrain `-g` / `-keyint_min`
    - disable scenecut (`-sc_threshold 0`)
  - Reduce sensitivity to B-frame reordering:
    - cap `-bf` / `bframes` or encoder params controlling B adaptation.
- Increase “pre-roll” into seg2 re-encode:
  - encode a slightly wider region before the seam and then trim down (gives encoder reference frames more context).
- Compare PTS/DTS directly in final MP4:
  - run `ffprobe -show_packets -read_intervals` around the seam and inspect `pts_time`/`dts_time` monotonicity.

