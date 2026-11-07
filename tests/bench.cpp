// Micro-benchmark for btoon-core encode/decode (no external dependencies).
// Build with -DBUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release; run: btoon_bench [rows]
// Reports the best of 7 runs per case.
#include <btoon/btoon.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
using namespace btoon;
using clk = std::chrono::steady_clock;

static Value make_records(int n) {
  Array rows; rows.reserve(n);
  for (int i = 0; i < n; ++i) {
    Map m;
    m["id"] = Int(i);
    m["name"] = String("user_" + std::to_string(i));
    m["email"] = String("user" + std::to_string(i) + "@example.com");
    m["age"] = Int(18 + (i % 60));
    m["score"] = Float(i * 0.37);
    m["active"] = Bool(i % 3 == 0);
    rows.push_back(Value(std::move(m)));
  }
  return Value(std::move(rows));
}
static Value make_nested(int n) {
  Array items; items.reserve(n);
  for (int i = 0; i < n; ++i) {
    Map m;
    m["id"] = Int(i);
    m["tags"] = Array{String("a"), String("bb"), String("ccc")};
    m["meta"] = Map{{"created", Int(1700000000 + i)}, {"ok", Bool(true)}, {"note", String("lorem ipsum dolor sit amet")}};
    if (i % 2) m["extra"] = Int(i);  // makes rows non-uniform -> not tabular
    items.push_back(Value(std::move(m)));
  }
  return Value(std::move(items));
}
template <class F> static double best_ms(F&& f, int iters) {
  double best = 1e30;
  for (int k = 0; k < iters; ++k) {
    auto t0 = clk::now(); f(); auto t1 = clk::now();
    best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  return best;
}
int main(int argc, char** argv) {
  int n = argc > 1 ? std::stoi(argv[1]) : 10000;
  struct Case { const char* name; Value v; bool tab; };
  std::vector<Case> cases = {{"records/tabular", make_records(n), true},
                             {"records/plain", make_records(n), false},
                             {"nested/mixed", make_nested(n), true}};
  for (auto& c : cases) {
    EncodeOptions o; o.auto_tabular = c.tab;
    std::vector<uint8_t> enc;
    double e = best_ms([&] { enc = encode(c.v, o); }, 7);
    Value out;
    double d = best_ms([&] { out = decode(enc); }, 7);
    double mb = enc.size() / 1e6;
    std::printf("%-16s n=%d size=%8zu B  encode=%8.2f ms (%7.1f MB/s)  decode=%8.2f ms (%7.1f MB/s)\n",
                c.name, n, enc.size(), e, mb / (e / 1e3), d, mb / (d / 1e3));
  }
}
