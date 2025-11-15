#include "btoon/delta_codec.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/btoon.h"

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace btoon {

std::vector<uint8_t> DeltaCodec::encode(const Array& data) {
    Encoder encoder;
    if (data.empty()) {
        return {encoder.getBuffer().begin(), encoder.getBuffer().end()};
    }

    std::vector<int64_t> values;
    values.reserve(data.size());
    for (const auto& value : data) {
        values.push_back(std::get<Int>(value));
    }

    size_t i = 0;
    Int last_value = 0;
#if defined(__AVX2__)
    for (; i + 3 < values.size(); i += 4) {
        __m256i current_values = _mm256_loadu_si256((__m256i*)&values[i]);
        __m256i last_values = _mm256_slli_si256(current_values, 8);
        __m256i prev_val_vec = _mm256_set_epi64x(0, 0, 0, last_value);
        last_values = _mm256_or_si256(last_values, prev_val_vec);
        __m256i deltas = _mm256_sub_epi64(current_values, last_values);
        
        int64_t temp[4];
        _mm256_storeu_si256((__m256i*)temp, deltas);
        
        for (int j = 0; j < 4; ++j) {
            encoder.encodeInt(temp[j]);
        }
        last_value = values[i + 3];
    }
#elif defined(__ARM_NEON)
    for (; i + 1 < values.size(); i += 2) {
        int64x2_t current_values = vld1q_s64(&values[i]);
        int64x2_t last_values = vextq_s64(vdupq_n_s64(last_value), current_values, 1);
        int64x2_t deltas = vsubq_s64(current_values, last_values);
        
        int64_t temp[2];
        vst1q_s64(temp, deltas);
        
        for (int j = 0; j < 2; ++j) {
            encoder.encodeInt(temp[j]);
        }
        last_value = values[i + 1];
    }
#endif
    for (; i < values.size(); ++i) {
        encoder.encodeInt(values[i] - last_value);
        last_value = values[i];
    }

    auto encoded_data = encoder.getBuffer();
    return {encoded_data.begin(), encoded_data.end()};
}

Array DeltaCodec::decode(const std::vector<uint8_t>& data) {
    Decoder decoder;
    Array result;
    if (data.empty()) {
        return result;
    }

    std::vector<int64_t> deltas;
    size_t pos = 0;
    while (pos < data.size()) {
        auto [value, bytes_read] = decoder.decode_and_get_pos({data.data() + pos, data.size() - pos});
        pos += bytes_read;
        deltas.push_back(std::get<Int>(value));
    }

    size_t i = 0;
    Int last_value = 0;
#if defined(__AVX2__)
    for (; i + 3 < deltas.size(); i += 4) {
        __m256i current_deltas = _mm256_loadu_si256((__m256i*)&deltas[i]);
        __m256i shifted1 = _mm256_slli_si256(current_deltas, 8);
        __m256i shifted2 = _mm256_slli_si256(current_deltas, 16);
        __m256i shifted3 = _mm256_slli_si256(current_deltas, 24);
        __m256i sum1 = _mm256_add_epi64(current_deltas, shifted1);
        __m256i sum2 = _mm256_add_epi64(sum1, shifted2);
        __m256i sum3 = _mm256_add_epi64(sum2, shifted3);
        __m256i last_value_vec = _mm256_set1_epi64x(last_value);
        __m256i current_values = _mm256_add_epi64(sum3, last_value_vec);
        
        int64_t temp[4];
        _mm256_storeu_si256((__m256i*)temp, current_values);
        
        for (int j = 0; j < 4; ++j) {
            result.push_back(temp[j]);
        }
        last_value = temp[3];
    }
#elif defined(__ARM_NEON)
    int64x2_t prefix_sum = vdupq_n_s64(0);
    for (; i + 1 < deltas.size(); i += 2) {
        int64x2_t current_deltas = vld1q_s64(&deltas[i]);
        prefix_sum = vaddq_s64(prefix_sum, current_deltas);
        int64x2_t last_value_vec = vdupq_n_s64(last_value);
        int64x2_t current_values = vaddq_s64(prefix_sum, last_value_vec);
        
        int64_t temp[2];
        vst1q_s64(temp, current_values);
        
        for (int j = 0; j < 2; ++j) {
            result.push_back(temp[j]);
        }
        last_value = temp[1];
    }
#endif
    for (; i < deltas.size(); ++i) {
        Int current_value = deltas[i] + last_value;
        result.push_back(current_value);
        last_value = current_value;
    }

    return result;
}

} // namespace btoon
