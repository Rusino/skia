How SkQP Generates Render Test Models
=====================================

We will, at regular intervals, generate new models from the [master branch of
Skia][1].  Here is how that process works:

1.  Get the positively triaged results from Gold:

        cd SKIA_SOURCE_DIRECTORY
        git fetch origin
        git checkout origin/master
        tools/skqp/get_gold_export_url.py HEAD~10 HEAD

    Open the resulting URL in a browser and download the resulting `meta.json` file.

        tools/skqp/sysopen.py $(tools/skqp/get_gold_export_url.py HEAD~10 HEAD)

2.  From a checkout of Skia's master branch, execute:

        cd SKIA_SOURCE_DIRECTORY
        git checkout origin/master
        tools/skqp/cut_release META_JSON_FILE

    This will create the following files:

        platform_tools/android/apps/skqp/src/main/assets/files.checksum
        platform_tools/android/apps/skqp/src/main/assets/skqp/rendertests.txt
        platform_tools/android/apps/skqp/src/main/assets/skqp/unittests.txt

    These three files can be commited to Skia to create a new commit.  Make
    `origin/skqp/dev` a parent of this commit (without merging it in), and
    push this new commit to `origin/skqp/dev`:

        git merge -s ours origin/skqp/dev -m "Cut SkQP $(date +%Y-%m-%d)"
        git add \
          platform_tools/android/apps/skqp/src/main/assets/files.checksum \
          platform_tools/android/apps/skqp/src/main/assets/skqp/rendertests.txt \
          platform_tools/android/apps/skqp/src/main/assets/skqp/unittests.txt
        git commit --amend --reuse-message=HEAD
        git push origin HEAD:refs/for/skqp/dev

    Review and submit the change:

        NUM=$(experimental/tools/gerrit-change-id-to-number HEAD)
        tools/skqp/sysopen.py https://review.skia.org/$NUM

`tools/skqp/cut_release`
------------------------

This tool will call `make_gmkb.go` to generate the `m{ax,in}.png` files for
each render test.  Additionaly, a `models.txt` file enumerates all of the
models.

Then it calls `jitter_gms` to see which render tests pass the jitter test.
`jitter_gms` respects the `bad_gms.txt` file by ignoring the render tests
enumerated in that file.  Tests which pass the jitter test are enumerated in
the file `good.txt`, those that fail in the `bad.txt` file.

Next, the `skqp/rendertests.txt` file is created.  This file lists the render
tests that will be executed by SkQP.  These are the union of the tests
enumerated in the `good.txt` and `bad.txt` files.  If the render test is found
in the `models.txt` file and the `good.txt` file, its per-test threshold is set
to 0 (a later CL can manually change this, if needed).  Otherwise, the
threshold is set to -1; this indicated that the rendertest will be executed (to
verify that the driver will not crash), but the output will not be compared
against the model.  Unnecessary models will be removed.

Next, all of the files that represent the models are uploaded to cloud storage.
A single checksum hash is kept in the  `files.checksum` file.  This is enough
to re-download those files later, but we don't have to fill the git repository
with a lot of binary data.

Finally, a list of the current gpu unit tests is created and stored in
`skqp/unittests.txt`.

[1]: https://skia.googlesource.com/skia/+log/master "Skia Master Branch"
[2]: https://gold.skia.org/search                   "Skia Gold Search"
