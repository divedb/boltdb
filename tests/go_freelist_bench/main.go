package main

import (
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"sort"
	"testing"
	"time"
)

// ============================================================
// Minimal freelist implementation (copied from boltdb/bolt)
// ============================================================

type pgid uint64
type txid uint64

type pgids []pgid

func (s pgids) Len() int           { return len(s) }
func (s pgids) Swap(i, j int)      { s[i], s[j] = s[j], s[i] }
func (s pgids) Less(i, j int) bool { return s[i] < s[j] }

type freelist struct {
	ids     []pgid
	pending map[txid][]pgid
}

func newFreelist() *freelist {
	return &freelist{
		pending: make(map[txid][]pgid),
	}
}

func (f *freelist) allocate(n int) pgid {
	if len(f.ids) == 0 {
		return 0
	}

	var initial, previd pgid
	for i, id := range f.ids {
		if id <= 1 {
			panic(fmt.Sprintf("invalid page allocation: %d", id))
		}

		if previd == 0 || id-previd != 1 {
			initial = id
		}

		if (id-initial)+1 == pgid(n) {
			if (i + 1) == n {
				f.ids = f.ids[i+1:]
			} else {
				copy(f.ids[i-n+1:], f.ids[i+1:])
				f.ids = f.ids[:len(f.ids)-n]
			}
			return initial
		}

		previd = id
	}
	return 0
}

func (f *freelist) release(txid txid) {
	for tid, ids := range f.pending {
		if tid <= txid {
			f.ids = append(f.ids, ids...)
			delete(f.pending, tid)
		}
	}
	sort.Sort(pgids(f.ids))
}

// ============================================================
// Random pgid generator (same seed as C++)
// ============================================================

func randomPgids(n int) []pgid {
	r := rand.New(rand.NewSource(42))
	ids := make(pgids, n)
	for i := range ids {
		ids[i] = pgid(r.Int63())
	}
	sort.Sort(ids)
	return ids
}

// ============================================================
// Benchmark results
// ============================================================

type BenchResult struct {
	Name    string  `json:"name"`
	Size    int     `json:"size"`
	Iters   int     `json:"iterations"`
	NsPerOp float64 `json:"ns_per_op"`
	MsPerOp float64 `json:"ms_per_op"`
}

func benchmarkFreelistRelease(size int) BenchResult {
	ids := randomPgids(size)
	pending := randomPgids(len(ids) / 400)

	result := testing.Benchmark(func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			// Copy ids to avoid mutation across iterations
			idsCopy := make([]pgid, len(ids))
			copy(idsCopy, ids)
			f := &freelist{ids: idsCopy, pending: map[txid][]pgid{1: pending}}
			f.release(1)
		}
	})

	nsPerOp := float64(result.T.Nanoseconds()) / float64(result.N)
	return BenchResult{
		Name:    fmt.Sprintf("BM_FreelistRelease/%d", size),
		Size:    size,
		Iters:   result.N,
		NsPerOp: nsPerOp,
		MsPerOp: nsPerOp / 1e6,
	}
}

func main() {
	sizes := []int{10000, 100000, 1000000, 10000000}
	results := make([]BenchResult, 0, len(sizes))

	for _, size := range sizes {
		fmt.Fprintf(os.Stderr, "Running Go benchmark: FreelistRelease/%d ...\n", size)
		start := time.Now()
		r := benchmarkFreelistRelease(size)
		elapsed := time.Since(start)
		fmt.Fprintf(os.Stderr, "  Done in %v  (%d iters, %.3f ms/op)\n", elapsed, r.Iters, r.MsPerOp)
		results = append(results, r)
	}

	// Output JSON to stdout for Python consumption.
	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	if err := enc.Encode(results); err != nil {
		fmt.Fprintf(os.Stderr, "Error encoding JSON: %v\n", err)
		os.Exit(1)
	}
}
