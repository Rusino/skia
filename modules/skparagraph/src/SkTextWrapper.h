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
        SkTextStretch()
                : fStartCluster(nullptr), fEndCluster(nullptr), fStart(0), fEnd(0), fWidth(0) {}
        explicit SkTextStretch(SkCluster* s, SkCluster* e)
                : fStartCluster(s)
                , fEndCluster(e)
                , fStart(0)
                , fEnd(e->endPos())
                , fMetrics()
                , fWidth(0) {
            for (auto c = s; c <= e; ++c) {
                if (c->run() != nullptr) fMetrics.add(c->run());
            }
        }

        inline SkScalar width() const { return fWidth; }
        inline SkCluster* startCluster() const { return fStartCluster; }
        inline SkCluster* endCluster() const { return fEndCluster; }
        inline SkCluster* breakCluster() const { return fBreakCluster; }
        inline SkLineMetrics& metrics() { return fMetrics; }
        inline size_t startPos() const { return fStart; }
        inline size_t endPos() const { return fEnd; }
        bool endOfCluster() { return fEnd == fEndCluster->endPos(); }
        bool endOfWord() {
            return endOfCluster() && (fEndCluster->isHardBreak() || fEndCluster->isSoftBreak());
        }

        void extend(SkTextStretch& stretch) {
            fMetrics.add(stretch.fMetrics);
            fEndCluster = stretch.endCluster();
            fEnd = stretch.endPos();
            fWidth += stretch.fWidth;
            stretch.clean();
        }

        void extend(SkCluster* cluster) {
            fEndCluster = cluster;
            fMetrics.add(cluster->run());
            fEnd = cluster->endPos();
            fWidth += cluster->width();
        }

        void extend(SkCluster* cluster, size_t pos) {
            fEndCluster = cluster;
            if (cluster->run() != nullptr) {
                fMetrics.add(cluster->run());
            }
            fEnd = pos;
        }

        void startFrom(SkCluster* cluster, size_t pos) {
            fStartCluster = cluster;
            fEndCluster = cluster;
            if (cluster->run() != nullptr) {
                fMetrics.add(cluster->run());
            }
            fStart = pos;
            fEnd = pos;
            fWidth = 0;
        }

        void nextPos() {
            if (fEnd == fEndCluster->endPos()) {
                ++fEndCluster;
                fEnd = 0;
            } else {
                fEnd = fEndCluster->endPos();
            }
        }

        void saveBreak() {
            fBreakCluster = fEndCluster;
            fBreak = fEnd;
        }

        void restoreBreak() {
            fEndCluster = fBreakCluster;
            fEnd = fBreak;
        }

        void trim() {
            fWidth -= (fEndCluster->width() - fEndCluster->trimmedWidth(fEnd));
        }

        void trim(SkCluster* cluster) {
            SkASSERT(fEndCluster == cluster);
            --fEndCluster;
            fWidth -= cluster->width();
            fEnd = fEndCluster->endPos();
        }

        void clean() {
            fStartCluster = nullptr;
            fEndCluster = nullptr;
            fStart = 0;
            fEnd = 0;
            fWidth = 0;
            fMetrics.clean();
        }

    private:
        SkCluster* fStartCluster;
        SkCluster* fEndCluster;
        SkCluster* fBreakCluster;
        size_t fStart;
        size_t fEnd;
        size_t fBreak;
        SkLineMetrics fMetrics;
        SkScalar fWidth;
    };

public:
    SkTextWrapper() { fLineNumber = 1; }

    using AddLineToParagraph = std::function<void(SkCluster* start,
                                                  SkCluster* end,
                                                  size_t startClip,
                                                  size_t endClip,
                                                  SkVector offset,
                                                  SkVector advance,
                                                  SkLineMetrics metrics,
                                                  bool addEllipsis)>;
    void breakTextIntoLines(SkParagraphImpl* parent,
                            SkSpan<SkCluster> span,
                            SkScalar maxWidth,
                            size_t maxLines,
                            const std::string& ellipsisStr,
                            const AddLineToParagraph& addLine);

    inline SkScalar height() const { return fHeight; }
    inline SkScalar intrinsicWidth() const { return fMinIntrinsicWidth; }

private:
    SkTextStretch fWords;
    SkTextStretch fClusters;
    SkTextStretch fClip;
    SkTextStretch fEndLine;
    size_t fLineNumber;
    bool fTooLongWord;
    bool fTooLongCluster;

    bool fHardLineBreak;

    SkScalar fHeight;
    SkScalar fMinIntrinsicWidth;

    void reset() {
        fWords.clean();
        fClusters.clean();
        fClip.clean();
        fTooLongCluster = false;
        fTooLongWord = false;
    }

    void lookAhead(SkScalar maxWidth, SkCluster* endOfClusters);
    void moveForward();
    void trimEndSpaces();
    void trimStartSpaces(SkCluster* endOfClusters);
};