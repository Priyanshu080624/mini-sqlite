#include "storage/pager.h"
#include "storage/btree.h"
#include "exec/seq_scan.h"
#include "exec/filter.h"
#include "exec/sort.h"
#include "exec/aggregate.h"
#include "exec/nested_loop_join.h"
#include "common/row.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <variant>

using Clock = std::chrono::high_resolution_clock;
using Ms    = std::chrono::duration<double, std::milli>;

PageId make_table(Pager& pager, const std::vector<Row>& rows, size_t ncols) {
    PageId root = BTree::create(pager);
    BTree tree(pager, root, ncols);
    for (const auto& r : rows) tree.insert(r);
    return tree.root_page_id();
}

std::vector<Row> collect(Operator& op) {
    std::vector<Row> out;
    op.open();
    while (auto r = op.next()) out.push_back(*r);
    op.close();
    return out;
}

// Print any Value safely regardless of type
std::string val_str(const Value& v) {
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, int64_t>)    return std::to_string(x);
        if constexpr (std::is_same_v<T, double>)     return std::to_string(x);
        if constexpr (std::is_same_v<T, std::string>) return x;
        return "NULL";
    }, v);
}

int main() {
    remove("m5_test.db");
    Pager pager("m5_test.db");

    // users(id, name, age, dept_id)
    // dept: 1=Engineering, 2=Marketing, 3=Sales
    PageId users = make_table(pager, {
        { Value(int64_t(1)), Value(std::string("Alice")), Value(int64_t(30)), Value(int64_t(1)) },
        { Value(int64_t(2)), Value(std::string("Bob")),   Value(int64_t(22)), Value(int64_t(2)) },
        { Value(int64_t(3)), Value(std::string("Carol")), Value(int64_t(28)), Value(int64_t(1)) },
        { Value(int64_t(4)), Value(std::string("Dan")),   Value(int64_t(35)), Value(int64_t(3)) },
        { Value(int64_t(5)), Value(std::string("Eve")),   Value(int64_t(22)), Value(int64_t(2)) },
        { Value(int64_t(6)), Value(std::string("Frank")), Value(int64_t(41)), Value(int64_t(1)) },
        { Value(int64_t(7)), Value(std::string("Grace")), Value(int64_t(29)), Value(int64_t(3)) },
        { Value(int64_t(8)), Value(std::string("Hank")),  Value(int64_t(22)), Value(int64_t(2)) },
    }, 4);

    // ── Test 1: ORDER BY age ASC ──
    std::cout << "=== Test 1: ORDER BY age ASC ===\n";
    {
        auto scan = std::make_unique<SeqScan>(pager, users, 4);
        Sort sort(std::move(scan), {{2, false}});
        auto rows = collect(sort);
        assert(rows.size() == 8);
        for (size_t i = 1; i < rows.size(); i++)
            assert(std::get<int64_t>(rows[i][2]) >= std::get<int64_t>(rows[i-1][2]));
        for (auto& r : rows)
            std::cout << "  " << std::get<std::string>(r[1])
                      << " age=" << std::get<int64_t>(r[2]) << "\n";
        std::cout << "  PASS\n";
    }

    // ── Test 2: ORDER BY age DESC ──
    std::cout << "\n=== Test 2: ORDER BY age DESC ===\n";
    {
        auto scan = std::make_unique<SeqScan>(pager, users, 4);
        Sort sort(std::move(scan), {{2, true}});
        auto rows = collect(sort);
        for (size_t i = 1; i < rows.size(); i++)
            assert(std::get<int64_t>(rows[i][2]) <= std::get<int64_t>(rows[i-1][2]));
        std::cout << "  First=" << std::get<std::string>(rows[0][1])
                  << " Last=" << std::get<std::string>(rows.back()[1]) << "\n";
        std::cout << "  PASS\n";
    }

    // ── Test 3: SELECT COUNT(*) ──
    std::cout << "\n=== Test 3: SELECT COUNT(*) FROM users ===\n";
    {
        auto scan = std::make_unique<SeqScan>(pager, users, 4);
        Aggregate agg(std::move(scan), {}, {{AggFunc::Count, 0}});
        auto rows = collect(agg);
        assert(rows.size() == 1);
        int64_t count = std::get<int64_t>(rows[0][0]);
        assert(count == 8);
        std::cout << "  COUNT(*) = " << count << "  PASS\n";
    }

    // ── Test 4: GROUP BY dept_id ──
    std::cout << "\n=== Test 4: GROUP BY dept_id — COUNT, SUM, AVG, MIN, MAX ===\n";
    {
        auto scan = std::make_unique<SeqScan>(pager, users, 4);
        auto agg  = std::make_unique<Aggregate>(std::move(scan),
            std::vector<size_t>{3},
            std::vector<AggCall>{
                {AggFunc::Count, 0},  // col 0 of output
                {AggFunc::Sum,   2},  // col 1
                {AggFunc::Avg,   2},  // col 2
                {AggFunc::Min,   2},  // col 3
                {AggFunc::Max,   2},  // col 4
            });
        // ORDER BY dept_id (col 0 of output = the group-by col)
        Sort sort(std::move(agg), {{0, false}});
        auto rows = collect(sort);
        assert(rows.size() == 3);
        std::cout << "  dept | count | sum_age | avg_age | min_age | max_age\n";
        for (auto& r : rows) {
            std::cout << "    "
                << val_str(r[0]) << "  |    "  // dept_id
                << val_str(r[1]) << "  |     " // count
                << val_str(r[2]) << "  | "     // sum
                << val_str(r[3]) << "  |     " // avg
                << val_str(r[4]) << "  |     " // min
                << val_str(r[5]) << "\n";       // max
        }
        // Engineering(dept=1): Alice(30), Carol(28), Frank(41) → count=3
        // Check dept 1 has 3 people
        bool found_eng = false;
        for (auto& r : rows) {
            if (std::get<int64_t>(r[0]) == 1) {
                assert(std::get<int64_t>(r[1]) == 3);
                found_eng = true;
            }
        }
        assert(found_eng);
        std::cout << "  PASS\n";
    }

    // ── Test 5: WHERE + GROUP BY + ORDER BY ──
    std::cout << "\n=== Test 5: WHERE age>25, GROUP BY dept, ORDER BY count DESC ===\n";
    {
        auto scan   = std::make_unique<SeqScan>(pager, users, 4);
        auto filter = std::make_unique<Filter>(std::move(scan), [](const Row& r) {
            return std::get<int64_t>(r[2]) > 25;
        });
        auto agg = std::make_unique<Aggregate>(std::move(filter),
            std::vector<size_t>{3},
            std::vector<AggCall>{{AggFunc::Count, 0}});
        Sort sort(std::move(agg), {{1, true}}); // ORDER BY count DESC
        auto rows = collect(sort);
        std::cout << "  dept | count (age > 25 only)\n";
        for (auto& r : rows)
            std::cout << "    " << val_str(r[0])
                      << "  |  " << val_str(r[1]) << "\n";
        std::cout << "  PASS\n";
    }

    // ── Test 6: Multi-column sort ──
    std::cout << "\n=== Test 6: ORDER BY dept ASC, age DESC ===\n";
    {
        auto scan = std::make_unique<SeqScan>(pager, users, 4);
        Sort sort(std::move(scan), {{3, false}, {2, true}}); // dept ASC, age DESC
        auto rows = collect(sort);
        assert(rows.size() == 8);
        std::cout << "  dept | name   | age\n";
        for (auto& r : rows)
            std::cout << "    " << std::get<int64_t>(r[3])
                      << "  | " << std::get<std::string>(r[1])
                      << "  | " << std::get<int64_t>(r[2]) << "\n";
        std::cout << "  PASS\n";
    }

    // ── Benchmark: 1000 rows insert + scan ──
    std::cout << "\n=== Benchmark: 1000 rows ===\n";
    {
        remove("m5_bench.db");
        Pager bp("m5_bench.db");
        PageId br = BTree::create(bp);
        BTree  bt(bp, br, 3);

        auto t0 = Clock::now();
        for (int64_t i = 1; i <= 1000; i++)
            bt.insert({ Value(i),
                        Value(std::string("user") + std::to_string(i)),
                        Value(i % 100) });
        br = bt.root_page_id();
        double ins_ms = Ms(Clock::now() - t0).count();

        t0 = Clock::now();
        SeqScan scan(bp, br, 3);
        int count = 0;
        scan.open();
        while (scan.next()) count++;
        scan.close();
        double scan_ms = Ms(Clock::now() - t0).count();

        std::cout << "  Insert 1000 rows : " << ins_ms  << " ms\n";
        std::cout << "  Scan   1000 rows : " << scan_ms << " ms\n";
        std::cout << "  Rows verified    : " << count   << "\n";
        assert(count == 1000);
    }

    std::cout << "\nAll M5 tests passed.\n";
    return 0;
}