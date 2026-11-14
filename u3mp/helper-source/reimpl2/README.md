# Helper functions written again from descriptions

Work in progress. Each folder holds the new C++ for one group of helper functions. Every file here was written from
the group's description in [`../spec/`](../spec/) only (never from the old code), and its group passed the
old-versus-new test at -O1 and -O2 before it was added here. Nothing in this folder is built into the helper yet:
the files are merged into `u3x_fixes.cpp` in one step when all groups are done.

`CONVENTIONS.h` holds the shared small tools the files use (single-precision float steps, pointer test, game call).
Three groups that talk to the online service are not in this folder.
