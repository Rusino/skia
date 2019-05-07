/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTextWrapper.h"
#include "SkParagraphImpl.h"

bool SkTextWrapper::addLineUpToTheLastBreak() {
  if (fLastBreak.width() == 0 && !fLastBreak.end()->isHardBreak()) {
    // Ignore an empty line if it's not generated by hard line break
    fLastBreak.clean(fLastBreak.end());
    return true;
  }

  // TODO: Place the ellipsis to the left or to the right of the line
  //  in case there is only one text direction on the line
  //  (in addition to the current approach that always places it to the right)
  auto& line = fParent->addLine(
    SkVector::Make(0, fOffsetY), // offset
    SkVector::Make(fLastBreak.trimmedWidth(), fLastBreak.height()), // advance
    fLastBreak.trimmedText(fLineStart), // text
    fLastBreak.sizes()); // metrics
  ++fLineNumber;

  line.reorderVisualRuns();

  if (reachedLinesLimit() && fLastBreak.end() != fClusters.end() - 1 && !fEllipsis.empty()) {
    // We must be on the last line and not at the end of the text
    line.createEllipsis(fMaxWidth, fEllipsis, true);
  } else {
    // Cut the spaces at the beginning of the line if there was no hard line break before
    fLineStart = fLastBreak.end() + 1;
    if (!fLastBreak.end()->isHardBreak()) {
      while (fLineStart < fClusters.end() &&
          fLineStart->isWhitespaces()) { fLineStart += 1; }
    }
  }

  fWidth =  SkMaxScalar(fWidth, fLastBreak.trimmedWidth());
  fHeight += fLastBreak.height();
  fOffsetY += fLastBreak.height();

  fLastBreak.clean(fLineStart);

  return !reachedLinesLimit();
}

void SkTextWrapper::formatText(SkSpan<SkCluster> clusters,
                               SkScalar maxWidth,
                               size_t maxLines,
                               const std::string& ellipsis) {
  fClusters = clusters;
  fMaxWidth = maxWidth;
  fEllipsis = ellipsis;
  fLineStart = fClusters.begin();
  fLastBreak.clean(fLineStart);
  fLastPosition.clean(fLineStart);
  fOffsetY = 0;
  fWidth = 0;
  fHeight = 0;
  fMinIntrinsicWidth = 0;
  fLineNumber = 0;
  fMaxLines = maxLines;

  // Iterate through all the clusters in the text
  SkScalar wordLength = 0;
  for (auto& cluster : clusters) {
    if (!cluster.isWhitespaces()) {
      wordLength += cluster.width();

      if (fLastBreak.width() + fLastPosition.width() + cluster.trimmedWidth() > fMaxWidth) {
        // Cluster does not fit: add the line until the closest break
        if (!addLineUpToTheLastBreak()) break;
      }

      if (fLastPosition.width() + cluster.width() > fMaxWidth) {
        // Cluster does not fit yet: try to break the text by hyphen?
      }

      if (fLastPosition.width() + cluster.trimmedWidth() > fMaxWidth) {
        // Cluster does not fit yet: add the line with the rest of clusters
        // (this is an emergency break)
        SkASSERT(fLastBreak.width() == 0);
        fLastBreak.moveTo(fLastPosition);
        if (!addLineUpToTheLastBreak()) break;
      }

      if (cluster.trimmedWidth() > fMaxWidth) {
        // Cluster still does not fit: it's too long
        // (we are past emergency break; let's clip it)
        fLastBreak.moveTo(cluster);
        if (!addLineUpToTheLastBreak()) break;
        continue;
      }
    } else {
      fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, wordLength);
      wordLength = 0;
    }
    // The cluster fits the line
    fLastPosition.moveTo(cluster);

    if (cluster.canBreakLineAfter()) {
      fLastBreak.moveTo(fLastPosition);
    }
    if (cluster.isHardBreak()) {
      if (!addLineUpToTheLastBreak()) break;
      if (endOfText()) {
        fLastBreak.moveTo(cluster);
        fLineStart = &cluster;
        addLineUpToTheLastBreak();
      }
    }
  };
  // Make sure nothing left
  if (!endOfText() && !reachedLinesLimit()) {
    fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, wordLength);
    fLastBreak.moveTo(fLastPosition);
    addLineUpToTheLastBreak();
  }
}
