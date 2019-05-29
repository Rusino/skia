/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTextWrapper.h"
#include "SkParagraphImpl.h"

// Since we allow cluster clipping when they don't fit
// we have to work with stretches - parts of clusters
void SkTextWrapper::lookAhead(SkScalar maxWidth, SkCluster* endOfClusters) {
    SkTextStretch currentCluster = fStartLine;
    while (currentCluster.cluster() != endOfClusters) {
        if (fWords.width() + fClusters.width() + currentCluster.width() >= maxWidth) {
            if (currentCluster.cluster()->isWhitespaces()) {
                break;
            }
            if (currentCluster.width() > maxWidth) {
                // Break the cluster into parts
                fClip.add(currentCluster, maxWidth - (fWords.width() + fClusters.width()));
                fTooLongCluster = true;
                fTooLongWord = true;
                break;
            }

            // Walk further to see if there is a too long word, cluster or glyph
            SkScalar nextWordLength = fClusters.width();
            for (auto further = currentCluster.cluster(); further != endOfClusters; ++further) {
                if (further->isSoftBreak() || further->isHardBreak()) {
                    break;
                }
                nextWordLength += further->width();
            }
            if (nextWordLength > maxWidth) {
                // If the word is too long we can break it right now and hope it's enough
                fTooLongWord = true;
            }
            fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, nextWordLength);
            break;
        }

        fClusters.add(currentCluster);

        // Keep adding clusters/words
        if (currentCluster.endOfWord()) {
            fWords.add(fClusters);
            fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, fWords.width());
            fClusters.clean();
        }

        if ((fHardLineBreak = currentCluster.cluster()->isHardBreak())) {
            // Stop at the hard line break
            break;
        }

        currentCluster.next();
    }
}

void SkTextWrapper::moveForward() {
    fEndLine = fStartLine;
    do {
        if (fWords.width() > 0) {
            fWidth += fWords.width();
            fEndLine = SkTextStretch(fWords.cluster());
            fLineMetrics.add(fWords.metrics());
            fWords.clean();
        } else if (fClusters.width() > 0) {
            fWidth += fClusters.width();
            fEndLine = SkTextStretch(fClusters.cluster());
            fLineMetrics.add(fClusters.metrics());
            fTooLongWord = false;
            fClusters.clean();
        } else if (fClip.width() > 0) {
            fWidth += fClip.width();
            fEndLine = fClip;
            fLineMetrics.add(fClip.metrics());
            fTooLongWord = false;
            fTooLongCluster = false;
        } else {
            break;
        }
    } while (fTooLongWord || fTooLongCluster);
}

// Special case for start/end cluster since they can be clipped
SkTextWrapper::SkTextStretch SkTextWrapper::trimEndSpaces() {
    if (!fEndLine.cluster()->isWhitespaces()) {
        auto delta = SkTMax(0.0f, fEndLine.position() - fEndLine.cluster()->trimmedWidth());
        if (delta > 0) {
            fWidth -= delta;
            return fEndLine.shift(delta);
        }
        return fEndLine;
    }

    SkASSERT(fEndLine.cluster()->isWhitespaces());
    if (fEndLine.cluster() != fStartLine.cluster()) {
        fWidth -= fEndLine.width();
    }
    for (auto cluster = fEndLine.cluster() - 1; cluster > fStartLine.cluster(); --cluster) {
        if (!cluster->isWhitespaces()) {
            fWidth -= cluster->lastSpacing();
            return SkTextStretch(cluster);
        }
        fWidth -= cluster->width();
    }

    if (fStartLine.cluster()->isWhitespaces()) {
        fWidth -= fStartLine.width();
        return SkTextStretch(fStartLine.cluster(), 0, 0);
    } else if (fStartLine.cluster()->trimmedWidth() < fStartLine.position()) {
        auto delta = SkTMax(0.0f, fStartLine.position() - fStartLine.cluster()->trimmedWidth());
        if (delta > 0) {
            fWidth -= delta;
            return fStartLine.shift(delta);
        }
    }
    return fEndLine;
}

// Trim the beginning spaces in case of soft line break
void SkTextWrapper::trimStartSpaces(SkCluster* endOfClusters) {
    if (fHardLineBreak) {
        fStartLine = SkTextStretch(fEndLine.cluster() + 1, fEndLine.cluster() + 1 < endOfClusters);
        return;
    }

    for (auto cluster = fEndLine.cluster() + 1; cluster < endOfClusters; ++cluster) {
        if (!cluster->isWhitespaces()) {
            fStartLine = SkTextStretch(cluster);
            return;
        }
    }

    // There are only whitespaces until the end of the text
    fStartLine = SkTextStretch(endOfClusters, false);
}

void SkTextWrapper::breakTextIntoLines(
        SkParagraphImpl* parent,
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
                                 bool addEllipsis)>& addLine) {

    fWidth = 0;
    fHeight = 0;
    fStartLine = SkTextStretch(span.begin());
    while (fStartLine.cluster() != span.end()) {
        reset();

        lookAhead(maxWidth, span.end());
        moveForward();

        auto trimmedEndLine = trimEndSpaces();

        auto reachedTheEnd =
                maxLines != std::numeric_limits<size_t>::max() && fLineNumber >= maxLines;
        // TODO: perform ellipsis work here
        if (parent->strutEnabled()) {
            // Make sure font metrics are not less than the strut
            parent->strutMetrics().updateLineMetrics(fLineMetrics, parent->strutForceHeight());
        }
        addLine(fStartLine.cluster(),
                trimmedEndLine.cluster(),
                fStartLine.position(),
                trimmedEndLine.position(),
                SkVector::Make(0, fHeight),
                SkVector::Make(fWidth, fLineMetrics.height()),
                fLineMetrics,
                reachedTheEnd && fStartLine.cluster() != span.end() && !ellipsisStr.empty());

        // Start a new line
        trimStartSpaces(span.end());
        fHeight += fLineMetrics.height();

        if (reachedTheEnd) {
            break;
        }
        ++fLineNumber;
    }

    if (fHardLineBreak) {
        // Last character is a line break
        if (parent->strutEnabled()) {
            // Make sure font metrics are not less than the strut
            parent->strutMetrics().updateLineMetrics(fLineMetrics, parent->strutForceHeight());
        }
        addLine(fEndLine.cluster(),
                fEndLine.cluster(),
                fEndLine.position(),
                fEndLine.position(),
                SkVector::Make(0, fHeight),
                SkVector::Make(0, fLineMetrics.height()),
                fLineMetrics,
                false);
    }
}
