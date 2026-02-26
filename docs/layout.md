// ┌───────────────────────────────────────────────────────────────┐
// │                        Page Header (~32 bytes)                │
// │  flags: branch (0x01)   n: 4   overflow: 0   pgid: 42         │
// │  checksum: ...                                                │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │  branchPageElement[0]    (12 bytes)                           │
// │    pos:   3800   ksize:  8   pgid: 100                        │
// │                                                               │
// │  branchPageElement[1]    (12 bytes)                           │
// │    pos:   3780   ksize: 12   pgid:  85                        │
// │                                                               │
// │  branchPageElement[2]    (12 bytes)                           │
// │    pos:   3750   ksize:  7   pgid:  60                        │
// │                                                               │
// │  branchPageElement[3]    (12 bytes)                           │
// │    pos:   3720   ksize:  9   pgid:  45                        │
// │                                                               │
// │  ... (possible padding / alignment bytes)                     │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │                         free space                            │
// │                     (can be used for new elements             │
// │                      and new keys when inserting)             │
// │                                                               │
// ├───────────────────────────────────────────────────────────────┤
// │                                                               │
// │   keys are stored backwards from the end of the page          │
// │                                                               │
// │   "user:zzzz\0"           <- pos = 3800, length = 8           │
// │   "user:wwww extra"       <- pos = 3780, length = 12          │
// │   "user:tttt"             <- pos = 3750, length = 7           │
// │   "user:mmmm123"          <- pos = 3720, length = 9           │
// │                                                               │
// └───────────────────────────────────────────────────────────────┘