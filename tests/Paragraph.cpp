/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkPaint.h"
#include "SkPoint.h"
#include "SkSerialProcs.h"
#include "SkTextBlobPriv.h"
#include "SkTo.h"
#include "SkTypeface.h"

#include "SkParagraphBuilder.h"

#include "Test.h"
#include "sk_tool_utils.h"

class ParagraphTester {
public:
    // This unit test feeds an ParagraphTester various runs then checks to see if
    // the result contains the provided data and merges runs when appropriate.
    static void TestBuilder(skiatest::Reporter* reporter) {
    }

private:

};

