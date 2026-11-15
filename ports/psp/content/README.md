# Locally supplied character assets

The original client content is not distributed in this fork. Before building
or running the PSP player checks, copy `character.rbxm`, `humanoidAnimate.rbxm`
and `humanoidHealth.rbxm` from your own desktop client's `content/fonts` folder
into this directory. These local copies are ignored by Git. The PSP runtime
loads the classic rig and animation/health scripts from them.

The PSP port remains experimental; see ../README.md and ../PROGRESS.md.
