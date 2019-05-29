/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <string>
#include "SkLine.h"
#include "src/core/SkSpan.h"

class SkParagraphImpl;

class SkTextWrapper {
    class SkTextStretch {
    public:
        SkTextStretch() : fLastCluster(nullptr), fLength(0), fPos(0) {}
        explicit SkTextStretch(SkCluster* c, bool real = true)
                : fLastCluster(c), fLength(c->width()), fPos(c->width()), fMetrics() {
            if (real) fMetrics.add(c->run());
        }
        SkTextStretch(SkCluster* c, SkScalar l, SkScalar p, bool real = true)
                : fLastCluster(c), fLength(l), fPos(p), fMetrics() {
            if (real) fMetrics.add(c->run());
        }

        inline SkScalar width() const { return fLength; }
        inline SkLineMetrics metrics() const { return fMetrics; }
        inline SkCluster* cluster() const { return fLastCluster; }
        inline SkScalar position() const { return fPos; }
        bool endOfCluster() { return fPos == fLastCluster->width(); }
        bool endOfWord() {
            return endOfCluster() && (fLastCluster->isHardBreak() || fLastCluster->isSoftBreak());
        }

        void next() {
            fLastCluster += 1;
            fLength = fLastCluster->width();
            fPos = fLastCluster->width();
        }

        void clean() {
            fLastCluster = nullptr;
            fLength = 0;
            fPos = 0;
            fMetrics.clean();
        }

        void add(const SkTextStretch& s) { add(s, s.fLength); }

        void add(const SkTextStretch& s, SkScalar len) {
            fLastCluster = s.fLastCluster;
            fLength += len;
            fPos += s.fPos;
            fMetrics.add(s.fLastCluster->run());
        }

        SkTextStretch shift(SkScalar value) {
            return SkTextStretch(fLastCluster, fLength - value, fPos - value);
        }

    private:
        SkCluster* fLastCluster;
        SkScalar fLength;
        SkScalar fPos;
        SkLineMetrics fMetrics;
    };

public:
    SkTextWrapper() { fLineNumber = 1; }
    void breakTextIntoLines(SkParagraphImpl* parent,
                            SkSpan<SkCluster> span,
                            SkScalar maxWidth,
                            size_t maxLines,
                            const std::string& ellipsisStr,
                            const std::function<void(SkCluster* start,
                                                     SkCluster* end,
                                                     SkScalar startClip,
                                                     SkScalar endClip,
                                                     SkVector offset,
                                                     SkVector advance,
                                                     SkLineMetrics metrics,
                                                     bool addEllipsis)>& addLine);

    inline SkScalar height() const { return fHeight; }
    inline SkScalar intrinsicWidth() const { return fMinIntrinsicWidth; }

private:
    SkTextStretch fWords;
    SkTextStretch fClusters;
    SkTextStretch fClip;
    SkTextStretch fStartLine;
    SkTextStretch fEndLine;
    size_t fLineNumber;
    SkLineMetrics fLineMetrics;
    bool fTooLongWord;
    bool fTooLongCluster;

    bool fHardLineBreak;

    SkScalar fWidth;
    SkScalar fHeight;
    SkScalar fMinIntrinsicWidth;

    void reset() {
        fWords.clean();
        fClusters.clean();
        fClip.clean();
        fWidth = 0;
        fLineMetrics.clean();
        fTooLongCluster = false;
        fTooLongWord = false;
    }

    void lookAhead(SkScalar maxWidth, SkCluster* endOfClusters);
    void moveForward();
    SkTextStretch trimEndSpaces();
    void trimStartSpaces(SkCluster* endOfClusters);
};