#include "btoon/rle_codec.h"
#include "btoon/encoder.h"
#include "btoon/decoder.h"
#include "btoon/btoon.h"

namespace btoon {

std::vector<uint8_t> RleCodec::encode(const Array& data) {
    Encoder encoder;
    if (data.empty()) {
        return {encoder.getBuffer().begin(), encoder.getBuffer().end()};
    }

    Value last_value = data[0];
    int run_length = 1;
    for (size_t i = 1; i < data.size(); ++i) {
        if (data[i] == last_value) {
            run_length++;
        } else {
            std::visit([&encoder](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, Nil>) {
                    encoder.encodeNil();
                } else if constexpr (std::is_same_v<T, Bool>) {
                    encoder.encodeBool(arg);
                } else if constexpr (std::is_same_v<T, Int>) {
                    encoder.encodeInt(arg);
                } else if constexpr (std::is_same_v<T, Uint>) {
                    encoder.encodeUint(arg);
                } else if constexpr (std::is_same_v<T, Float>) {
                    encoder.encodeFloat(arg);
                } else if constexpr (std::is_same_v<T, String>) {
                    encoder.encodeString(arg);
                } else if constexpr (std::is_same_v<T, StringView>) {
                    encoder.encodeString({arg.data(), arg.size()});
                } else if constexpr (std::is_same_v<T, Binary>) {
                    encoder.encodeBinary(arg);
                } else if constexpr (std::is_same_v<T, Extension>) {
                    encoder.encodeExtension(arg.type, arg.data);
                } else if constexpr (std::is_same_v<T, Timestamp>) {
                    encoder.encodeTimestamp(arg.seconds);
                } else if constexpr (std::is_same_v<T, Date>) {
                    encoder.encodeDate(arg.milliseconds);
                } else if constexpr (std::is_same_v<T, BigInt>) {
                    encoder.encodeBigInt(arg.bytes);
                }
            }, last_value);
            encoder.encodeInt(run_length);
            last_value = data[i];
            run_length = 1;
        }
    }
    std::visit([&encoder](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Nil>) {
            encoder.encodeNil();
        } else if constexpr (std::is_same_v<T, Bool>) {
            encoder.encodeBool(arg);
        } else if constexpr (std::is_same_v<T, Int>) {
            encoder.encodeInt(arg);
        } else if constexpr (std::is_same_v<T, Uint>) {
            encoder.encodeUint(arg);
        } else if constexpr (std::is_same_v<T, Float>) {
            encoder.encodeFloat(arg);
        } else if constexpr (std::is_same_v<T, String>) {
            encoder.encodeString(arg);
        } else if constexpr (std::is_same_v<T, StringView>) {
            encoder.encodeString({arg.data(), arg.size()});
        } else if constexpr (std::is_same_v<T, Binary>) {
            encoder.encodeBinary(arg);
        } else if constexpr (std::is_same_v<T, Extension>) {
            encoder.encodeExtension(arg.type, arg.data);
        } else if constexpr (std::is_same_v<T, Timestamp>) {
            encoder.encodeTimestamp(arg.seconds);
        } else if constexpr (std::is_same_v<T, Date>) {
            encoder.encodeDate(arg.milliseconds);
        } else if constexpr (std::is_same_v<T, BigInt>) {
            encoder.encodeBigInt(arg.bytes);
        }
    }, last_value);
    encoder.encodeInt(run_length);

    auto encoded_data = encoder.getBuffer();
    return {encoded_data.begin(), encoded_data.end()};
}

Array RleCodec::decode(const std::vector<uint8_t>& data) {
    Decoder decoder;
    Array result;
    size_t pos = 0;
    while (pos < data.size()) {
        auto [value, bytes_read_val] = decoder.decode_and_get_pos({data.data() + pos, data.size() - pos});
        pos += bytes_read_val;
        auto [run_length_value, bytes_read_len] = decoder.decode_and_get_pos({data.data() + pos, data.size() - pos});
        pos += bytes_read_len;
        int run_length = std::get<Int>(run_length_value);
        result.insert(result.end(), run_length, value);
    }

    return result;
}

} // namespace btoon