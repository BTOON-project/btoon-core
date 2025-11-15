#!/bin/bash
# Run performance benchmarks across all language implementations

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="$SCRIPT_DIR/../.."
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/benchmark-results}"

echo "BTOON Cross-Language Performance Benchmark"
echo "=========================================="
echo "Version: 0.0.1"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Test data sizes for benchmarking
SIZES=(100 1000 10000 100000)
ITERATIONS=100

# Generate test data
echo "Generating test data..."
"$ROOT_DIR/build/tests/interop/generate-test-data" "$OUTPUT_DIR/test-data"

# Function to run benchmark
run_benchmark() {
    local lang=$1
    local test_file=$2
    local output_file="$OUTPUT_DIR/${lang}_benchmark.json"
    
    echo "Benchmarking $lang..."
    
    case "$lang" in
        cpp)
            "$ROOT_DIR/build/tests/btoon_benchmark" \
                --input "$test_file" \
                --iterations "$ITERATIONS" \
                --output "$output_file"
            ;;
        python)
            python3 "$ROOT_DIR/../btoon-python/benchmark.py" \
                --input "$test_file" \
                --iterations "$ITERATIONS" \
                --output "$output_file"
            ;;
        javascript)
            node "$ROOT_DIR/../btoon-javascript/benchmark.js" \
                --input "$test_file" \
                --iterations "$ITERATIONS" \
                --output "$output_file"
            ;;
        go)
            "$ROOT_DIR/../btoon-go/btoon-benchmark" \
                --input "$test_file" \
                --iterations "$ITERATIONS" \
                --output "$output_file"
            ;;
        *)
            echo "Unknown language: $lang"
            return 1
            ;;
    esac
}

# Run benchmarks for each language
LANGUAGES=(cpp python javascript go)

for lang in "${LANGUAGES[@]}"; do
    if [ -d "$ROOT_DIR/../btoon-$lang" ] || [ "$lang" = "cpp" ]; then
        for size in "${SIZES[@]}"; do
            test_file="$OUTPUT_DIR/test-data/array_${size}.btoon"
            
            # Generate sized test data if needed
            if [ ! -f "$test_file" ]; then
                echo "Generating ${size}-element array test..."
                "$ROOT_DIR/build/tests/generate_sized_data" "$size" "$test_file"
            fi
            
            run_benchmark "$lang" "$test_file" || true
        done
    else
        echo "Skipping $lang (not found)"
    fi
done

echo ""
echo "Benchmark Results:"
echo "------------------"

# Parse and display results
for result_file in "$OUTPUT_DIR"/*_benchmark.json; do
    if [ -f "$result_file" ]; then
        lang=$(basename "$result_file" _benchmark.json)
        echo ""
        echo "$lang:"
        # Simple JSON parsing (production would use jq or similar)
        grep -E '"(encode|decode)_time"' "$result_file" | head -4
    fi
done

echo ""
echo "Full results saved to: $OUTPUT_DIR"
echo "Generate HTML report with: $SCRIPT_DIR/generate-report.py --output report.html"
