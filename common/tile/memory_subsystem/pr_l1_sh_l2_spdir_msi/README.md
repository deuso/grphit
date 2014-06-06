#How to support DGD

## When read request comes:

* Hit a block entry first: handles it as usual.
* No block entry, region entry exsists
   + req id same as owner: add present bit
   + req id not owner: clear region entry present bit, create new block entry, add onwer and requester.
   + non-owner write request should invalidate owner and create new block entry.
* No block entry, no region entry: create a region entry.

## When eviction/wb comes:

* None hit: assert error
* Hit block entry first: handles it as usual. if sharing bit clean, invalidate entry
* Hit region entry
   + req id equals owner: clear present bit, if present bit clean, invalidate entry
   + req id ne owner: assert error
* Miss: assert error

## How to recollect block entries into a owned region
* Upon evicting, if there remains only one sharer of this block entry, the controller will search if a region entry exsists. If the region entry exsists and the owner of the region entry equals the block entry, the block entry is added to the present bit of the region entry, and the block entry is invalidated.
