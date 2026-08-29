#include "storage/pager.h"
#include "storage/btree.h"
#include "storage/index_manager.h"
#include "exec/seq_scan.h"
#include "exec/filter.h"
#include "exec/index_scan.h"
#include "common/row.h"
#include <iostream>
#include <cassert>
#include <chrono>

using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

int main() {
    const int N = 500;
    remove("m3_test.db");

    std::cout << "=== M3: Building table with " << N << " rows ===\n";

    PageId table_root, idx_root;
    int64_t target_pk = N / 2;
    std::string target_name = "user" + std::to_string(target_pk);

    {
        Pager pager("m3_test.db");
        table_root = BTree::create(pager);
        idx_root   = IndexManager::create(pager);

        BTree table(pager, table_root, 3);
        auto t0 = Clock::now();

        for (int64_t i = 1; i <= N; i++) {
            std::string name = "user" + std::to_string(i);
            Row row = { Value(i), Value(name), Value(i % 100) };
            table.insert(row);
            // IMPORTANT: save updated root after each insert (splits change it)
            idx_root = IndexManager::insert_entry(pager, idx_root,
                                                  Value(name), i);
        }
        table_root = table.root_page_id();

        double ms = Ms(Clock::now() - t0).count();
        std::cout << "Inserted " << N << " rows + index in " << ms << "ms\n";
        std::cout << "Table root: " << table_root
                  << "  Index root: " << idx_root << "\n";
    }

    // ── Without index: SeqScan + Filter ──
    std::cout << "\n=== Without index: SeqScan + Filter ===\n";
    double seq_ms;
    {
        Pager pager("m3_test.db");
        auto t0 = Clock::now();
        auto scan = std::make_unique<SeqScan>(pager, table_root, 3);
        Filter filter(std::move(scan), [&](const Row& r) {
            return std::get<std::string>(r[1]) == target_name;
        });
        filter.open();
        std::optional<Row> found;
        while (auto row = filter.next()) found = row;
        filter.close();
        seq_ms = Ms(Clock::now() - t0).count();
        assert(found.has_value());
        std::cout << "Found: " << *found << "\n";
        std::cout << "Time:  " << seq_ms << "ms\n";
    }

    // ── With index: IndexScan ──
    std::cout << "\n=== With index: IndexScan ===\n";
    double idx_ms;
    {
        Pager pager("m3_test.db");
        auto t0 = Clock::now();
        IndexScan iscan(pager, table_root, idx_root, 3, Value(target_name));
        iscan.open();
        auto found = iscan.next();
        iscan.close();
        idx_ms = Ms(Clock::now() - t0).count();
        assert(found.has_value());
        std::cout << "Found: " << *found << "\n";
        std::cout << "Time:  " << idx_ms << "ms\n";
    }

    std::cout << "\n=== Benchmark summary ===\n";
    std::cout << "SeqScan+Filter : " << seq_ms << "ms\n";
    std::cout << "IndexScan      : " << idx_ms << "ms\n";
    if (idx_ms > 0)
        std::cout << "Speedup        : " << (seq_ms/idx_ms) << "x\n";
    std::cout << "\nM3 complete.\n";
    return 0;
}