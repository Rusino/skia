/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef StressCorpus_DEFINED
#define StressCorpus_DEFINED

#include <string>
#include <vector>

namespace skia::text_editor {

struct StressTestCase {
    const char* name;
    std::string text;
    bool expect_multiline{false};
};

inline const std::vector<StressTestCase>& GetCrossLayerStressCorpus() {
    static const std::vector<StressTestCase> kCorpus = {
        // 1. Zalgo stacked combining diacritics
        {
            "Latin_Zalgo_StackedMarks",
            "e\xcc\x81\xcc\x80\xcc\x83\xcc\x82\xcc\x88\xcc\x8a suffix",
            false
        },
        // 2. Arabic with multiple vowel marks (Harakat / Tashkeel)
        {
            "Arabic_Tashkeel_Vowels",
            "\xd8\xa8\xd9\x91\xd9\x8e \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a",
            false
        },
        // 3. Hebrew with Niqqud & Dagesh
        {
            "Hebrew_Niqqud_Marks",
            "\xd7\xa9\xd6\xb8\xd7\x81\xd7\x9c\xd6\xb5\xd7\x95\xd6\xb9\xd7\x9d",
            false
        },
        // 4. Multi-line with mixed newlines
        {
            "MultiLine_MixedControlBreaks",
            "Line One\nLine Two\r\nLine Three",
            true
        },
        // 5. Empty document buffer
        {
            "Empty_Buffer",
            "",
            false
        },
    };
    return kCorpus;
}

} // namespace skia::text_editor

#endif // StressCorpus_DEFINED
