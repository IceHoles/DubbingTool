---
name: Fix concat splice glitches
overview: Fix video jumps and audio pops at segment splice points in the concat render mode by aligning segment boundaries to keyframes and ensuring clean audio transitions.
todos:
  - id: find-kf-before-tb
    content: Add second ffprobe keyframe search in concatFindKeyframe() to find last keyframe at or before tbStart; add m_concatKfBeforeTbStart member; always call concatFindKeyframe() even in 2-segment mode
    status: in_progress
  - id: align-seg1-end
    content: Change concatCutSegment1() to use m_concatKfBeforeTbStart as the -to cut point instead of tbStartStr
    status: pending
  - id: align-seg2-start
    content: Change concatRenderSegment2() to seek from m_concatKfBeforeTbStart, adjust setpts filter and duration calculation
    status: pending
  - id: reencode-audio-seg2
    content: Change segment 2 audio from -c:a copy to -c:a aac -b:a 192k for clean audio boundaries
    status: pending
  - id: ts-mux-flags
    content: Add -muxdelay 0 -muxpreload 0 to all 3 segment output commands
    status: pending
  - id: genpts-concat
    content: Add -fflags +genpts to concatJoinSegments() before the -f concat input
    status: pending
isProject: false
---

# Fix Concat Splice Glitches (Video Jumps and Audio Pops)

## Problem Analysis

The concat render splits the video into 2-3 segments:

- **Segment 1**: Before TB (stream copy)
- **Segment 2**: TB section (re-encode with hardsub)
- **Segment 3**: After TB (stream copy, optional)

There are several issues causing glitches at the **segment 1 to segment 2 boundary**:

### Root Cause 1: No keyframe alignment at the Seg1-Seg2 boundary

In [workflowmanager.cpp](src/core/workflowmanager.cpp), `concatCutSegment1()` (line 1643) uses:

```1663:1667:src/core/workflowmanager.cpp
             << "-to" << tbStartStr
             << "-map" << "0:v:0"
             << "-map" << "1:a:0"
             << "-c" << "copy"
             << "-avoid_negative_ts" << "make_non_negative";
```

With `-c copy`, FFmpeg cannot cut at an arbitrary frame -- it writes all packets with DTS <= `tbStart`. Due to B-frames, some packets have DTS before `tbStart` but PTS after it. These frames end up in segment 1 even though they display after `tbStart`. Then segment 2 starts encoding from `tbStart`:

```1697:1699:src/core/workflowmanager.cpp
    args << "-y"
         << "-ss" << tbStartStr
         << "-i" << m_finalMkvPath;
```

This causes an **overlap**: a few frames appear in both segments, creating a visual "jump back" at the splice point.

Note: The Seg2-Seg3 boundary already handles this correctly with `concatFindKeyframe()` (line 1568) which finds a keyframe after TB end.

### Root Cause 2: Audio discontinuity at boundaries

Audio (AAC) is stream-copied in all segments. AAC frames are ~23ms long and their boundaries don't align with video keyframes. When segment 1 cuts audio at `tbStart` and segment 2 copies audio from a `-ss` seek position, AAC frames at the boundary can overlap or leave a gap, causing audible **clicks/pops**.

### Root Cause 3: Missing MPEG-TS muxing delay control

Segments are output as `.ts` but without `-muxdelay 0 -muxpreload 0`. The TS muxer can add initial buffering delays, creating timestamp gaps between segments when concatenated.

## Solution

### 1. Find keyframe BEFORE TB start (the main fix)

Modify `concatFindKeyframe()` to **also** search for the last keyframe at or before `m_concatTbStartSeconds`. Add a second `ffprobe` call with interval `[tbStart-15, tbStart]`, iterate the results, and pick the last keyframe with `pts_time <= tbStart`. Store in a new member `m_concatKfBeforeTbStart`.

In [workflowmanager.h](src/core/workflowmanager.h):

```277:280:src/core/workflowmanager.h
    double m_concatTbStartSeconds = 0.0;
    double m_concatTbEndSeconds = 0.0;
    double m_concatKeyframeTime = 0.0;
    int m_concatSegmentCount = 0;
```

Add `double m_concatKfBeforeTbStart = 0.0;` here.

Also, **always call `concatFindKeyframe()**` (even in 2-segment mode), since we need the keyframe before TB start in both cases.

### 2. Align segment 1 end to the keyframe

In `concatCutSegment1()`, change the cut point from `tbStartStr` to `m_concatKfBeforeTbStart`:

```cpp
QString seg1EndStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);
// ...
args << "-to" << seg1EndStr;
```

This ensures the video stream copy cuts cleanly at a keyframe boundary with no B-frame tail.

### 3. Start segment 2 from the keyframe

In `concatRenderSegment2()`, seek to `m_concatKfBeforeTbStart` instead of `tbStartStr`:

```cpp
QString seg2StartStr = QString::number(m_concatKfBeforeTbStart, 'f', 3);
args << "-ss" << seg2StartStr;
```

Adjust the `setpts` subtitle filter to use `seg2StartStr`:

```cpp
QString vf = QString("setpts=PTS+%1/TB,subtitles=%2,setpts=PTS-STARTPTS")
                 .arg(seg2StartStr, signsPath);
```

Adjust duration for 3-segment mode:

```cpp
double segDuration = m_concatKeyframeTime - m_concatKfBeforeTbStart;
```

The few frames between `kfBeforeTbStart` and `tbStart` will be re-encoded without subtitles (subtitles only exist from `tbStart`), making a seamless invisible transition.

### 4. Re-encode audio in segment 2

Change segment 2's audio from `-c:a copy` to `-c:a aac` with matched bitrate. Since segment 2 is already re-encoding video, adding audio re-encode costs very little time but ensures clean audio at both boundaries:

```cpp
args << "-c:a" << "aac" << "-b:a" << "192k";
```

### 5. Add TS muxing flags to all segment outputs

Add `-muxdelay 0 -muxpreload 0` before the output path in `concatCutSegment1()`, `concatRenderSegment2()`, and `concatCutSegment3()` to prevent the TS muxer from adding initial buffering delays.

### 6. Add timestamp regeneration to concat join

In `concatJoinSegments()`, add `-fflags +genpts` before the `-i` flag to help the demuxer regenerate clean timestamps:

```cpp
args << "-y"
     << "-fflags" << "+genpts"
     << "-f" << "concat"
     << ...
```

## Files to modify

- [src/core/workflowmanager.h](src/core/workflowmanager.h) -- add `m_concatKfBeforeTbStart` member
- [src/core/workflowmanager.cpp](src/core/workflowmanager.cpp) -- all concat-related functions (lines 1493-1841)

