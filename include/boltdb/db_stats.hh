#pragma once

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

namespace boltdb {

// page 100 (leaf, full)
// ┌────────────────────────────────────┐
// │ k1 | k2 | k3 | k4 | k5             │
// └────────────────────────────────────┘
//           ↑
//       parent node points here
//
// After inserting k6 → Two possible outcomes
//
// 1. Split (classic B-tree split)
//
// page 100 (leaf)               page 101 (new leaf)
// ┌────────────────┐            ┌─────────────────────┐
// │ k1 | k2        │            │ k4 | k5 | k6        │
// └────────────────┘            └─────────────────────┘
//           ↑                             ↑
//           └──────────── parent ─────────┘
//                   parent node now:
//                   ┌──────────────────────────┐
//                   │ ... | k3 | ...           │
//                   └──────────────────────────┘
//                            ↑
//                     separator key
//
// 2. Spill (overflow / right-linked pages)
//
// page 100 (leaf)                  page 200 (overflow page)
// ┌──────────────────────┐         ┌─────────────────────┐
// │ k1 | k2 | k3         │  =====> │ k4 | k5 | k6        │
// └──────────────────────┘   next  └─────────────────────┘
//           ↑
//       parent still points only here
struct TxStats {
  using duration_t = std::chrono::nanoseconds;

  int64_t page_count{};               ///< The number of page allocations.
  int64_t page_alloc{};               ///< The total bytes allocated.
  int64_t cursor_count{};             ///< The number of cursors created.
  int64_t node_count{};               ///< The number of node allocations.
  int64_t node_deref{};               ///< The number of node dereferences.
  int64_t rebalance_count{0};         ///< The number of page rebalances.
  duration_t rebalance_time_ns{0ns};  ///< The time spent in rebalancing.
  int64_t split_count{};              ///< The number of page splits.
  int64_t spill_count{};              ///< The number of page spills.
  duration_t spill_time{0ns};         ///< The time spent in spilling.
  int64_t write_count{0};             ///< The number of page writes.
  duration_t write_time{0ns};         ///< The time spent in writing.
};

struct DatabaseStats {
  int64_t free_pages{};      ///< Number of free pages on the freelist.
  int64_t pending_pages{};   ///< Number of pending pages on the freelist.
  int64_t free_alloc{};      ///< Total bytes allocated in free pages.
  int64_t freelist_inuse{};  ///< Bytes used by the freelist.
  int64_t tx_n{};            ///< Total started read transactions.
  int64_t open_tx_n{};       ///< Number of open read transactions.
  TxStats tx_stats;
};

std::atomic<DatabaseStats*> g_stats{nullptr};

class PrometheusExporter {
 public:
  explicit PrometheusExporter(const std::string& address = "0.0.0.0:9100")
      : exposer_(address), registry_(std::make_shared<prometheus::Registry>()) {
    exposer_.RegisterCollectable(registry_);

    free_pages_ = &prometheus::BuildGauge()
                       .Name("db_freelist_free_pages")
                       .Help("Number of free pages on the freelist")
                       .Register(*registry_)
                       .Add({});
    pending_pages_ = &prometheus::BuildGauge()
                          .Name("db_freelist_pending_pages")
                          .Help("Number of pending pages on the freelist")
                          .Register(*registry_)
                          .Add({});
    free_alloc_ = &prometheus::BuildGauge()
                       .Name("db_freelist_free_bytes_total")
                       .Help("Total bytes allocated in free pages")
                       .Register(*registry_)
                       .Add({});
    freelist_inuse_ = &prometheus::BuildGauge()
                           .Name("db_freelist_bytes_used")
                           .Help("Bytes used by the freelist itself")
                           .Register(*registry_)
                           .Add({});
    tx_total_ = &prometheus::BuildCounter()
                     .Name("db_transactions_started_total")
                     .Help("Total number of started read transactions")
                     .Register(*registry_)
                     .Add({});
    tx_open_ = &prometheus::BuildGauge()
                    .Name("db_transactions_open")
                    .Help("Current number of open read transactions")
                    .Register(*registry_)
                    .Add({});

    // TxStats Counter / Gauge
    tx_page_count_ = &prometheus::BuildCounter()
                          .Name("db_tx_page_allocations_total")
                          .Help("Number of page allocations")
                          .Register(*registry_)
                          .Add({});
    tx_page_bytes_ = &prometheus::BuildCounter()
                          .Name("db_tx_page_bytes_allocated_total")
                          .Help("Total bytes allocated for pages")
                          .Register(*registry_)
                          .Add({});
    tx_cursor_count_ = &prometheus::BuildCounter()
                            .Name("db_tx_cursors_created_total")
                            .Help("Number of cursors created")
                            .Register(*registry_)
                            .Add({});
    tx_node_count_ = &prometheus::BuildCounter()
                          .Name("db_tx_nodes_allocated_total")
                          .Help("Number of node allocations")
                          .Register(*registry_)
                          .Add({});
    tx_node_deref_ = &prometheus::BuildCounter()
                          .Name("db_tx_node_dereferences_total")
                          .Help("Number of node dereferences")
                          .Register(*registry_)
                          .Add({});

    tx_rebalance_total_ = &prometheus::BuildCounter()
                               .Name("db_tx_rebalances_total")
                               .Help("Number of page rebalances")
                               .Register(*registry_)
                               .Add({});
    tx_rebalance_seconds_ =
        &prometheus::BuildCounter()
             .Name("db_tx_rebalance_seconds_total")
             .Help("Total time spent in rebalancing (seconds)")
             .Register(*registry_)
             .Add({});

    tx_split_total_ = &prometheus::BuildCounter()
                           .Name("db_tx_page_splits_total")
                           .Help("Number of page splits")
                           .Register(*registry_)
                           .Add({});
    tx_spill_total_ = &prometheus::BuildCounter()
                           .Name("db_tx_page_spills_total")
                           .Help("Number of page spills")
                           .Register(*registry_)
                           .Add({});
    tx_spill_seconds_ = &prometheus::BuildCounter()
                             .Name("db_tx_spill_seconds_total")
                             .Help("Total time spent in spilling (seconds)")
                             .Register(*registry_)
                             .Add({});

    tx_write_total_ = &prometheus::BuildCounter()
                           .Name("db_tx_page_writes_total")
                           .Help("Number of page writes")
                           .Register(*registry_)
                           .Add({});
    tx_write_seconds_ =
        &prometheus::BuildCounter()
             .Name("db_tx_write_seconds_total")
             .Help("Total time spent in writing pages (seconds)")
             .Register(*registry_)
             .Add({});
  }

  void UpdateFromStats() {
    auto* stats = g_stats.load();

    if (!stats) return;

    // DatabaseStats
    free_pages_->Set(stats->free_pages);
    pending_pages_->Set(stats->pending_pages);
    free_alloc_->Set(stats->free_alloc);
    freelist_inuse_->Set(stats->freelist_inuse);
    tx_total_->Increment(stats->tx_n - tx_total_->Value());
    tx_open_->Set(stats->open_tx_n);

    const auto& tx = stats->tx_stats;

    tx_page_count_->Increment(tx.page_count - last_tx_.page_count);
    tx_page_bytes_->Increment(tx.page_alloc - last_tx_.page_alloc);
    tx_cursor_count_->Increment(tx.cursor_count - last_tx_.cursor_count);
    tx_node_count_->Increment(tx.node_count - last_tx_.node_count);
    tx_node_deref_->Increment(tx.node_deref - last_tx_.node_deref);

    tx_rebalance_total_->Increment(tx.rebalance_count -
                                   last_tx_.rebalance_count);
    tx_split_total_->Increment(tx.split_count - last_tx_.split_count);
    tx_spill_total_->Increment(tx.spill_count - last_tx_.spill_count);
    tx_write_total_->Increment(tx.write_count - last_tx_.write_count);

    auto rebalance_sec = std::chrono::duration<double>(
                             tx.rebalance_time_ns - last_tx_.rebalance_time_ns)
                             .count();
    auto spill_sec =
        std::chrono::duration<double>(tx.spill_time - last_tx_.spill_time)
            .count();
    auto write_sec =
        std::chrono::duration<double>(tx.write_time - last_tx_.write_time)
            .count();

    tx_rebalance_seconds_->Increment(rebalance_sec);
    tx_spill_seconds_->Increment(spill_sec);
    tx_write_seconds_->Increment(write_sec);
    last_tx_ = tx;
  }

 private:
  prometheus::Exposer exposer_;
  std::shared_ptr<prometheus::Registry> registry_;

  // Database gauges & counters
  prometheus::Gauge* free_pages_{};
  prometheus::Gauge* pending_pages_{};
  prometheus::Gauge* free_alloc_{};
  prometheus::Gauge* freelist_inuse_{};
  prometheus::Counter* tx_total_{};
  prometheus::Gauge* tx_open_{};

  // TxStats counters
  prometheus::Counter* tx_page_count_{};
  prometheus::Counter* tx_page_bytes_{};
  prometheus::Counter* tx_cursor_count_{};
  prometheus::Counter* tx_node_count_{};
  prometheus::Counter* tx_node_deref_{};
  prometheus::Counter* tx_rebalance_total_{};
  prometheus::Counter* tx_rebalance_seconds_{};
  prometheus::Counter* tx_split_total_{};
  prometheus::Counter* tx_spill_total_{};
  prometheus::Counter* tx_spill_seconds_{};
  prometheus::Counter* tx_write_total_{};
  prometheus::Counter* tx_write_seconds_{};

  TxStats last_tx_{};
};

}  // namespace boltdb
