#include "btoon/security.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace btoon {

void Security::setSecretKey(const std::string& key) {
    secretKey_ = key;
}

std::vector<uint8_t> Security::sign(std::span<const uint8_t> data) const {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len;
    HMAC(EVP_sha256(), secretKey_.data(), secretKey_.size(),
         data.data(), data.size(), md, &md_len);
    return {md, md + md_len};
}

bool Security::verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature) const {
    if (secretKey_.empty()) {
        throw BtoonException("No secret key set for HMAC verification");
    }
    auto computed = sign(data);
    if (computed.size() != signature.size()) return false;
    // Use a constant-time comparison to prevent timing attacks
    int diff = 0;
    for (size_t i = 0; i < computed.size(); ++i) {
        diff |= computed[i] ^ signature[i];
    }
    return diff == 0;
}

void Security::setAllowedTypes(const std::set<size_t>& types) {
    allowedTypes_ = types;
    restrictTypes_ = !types.empty();
}

bool Security::isTypeAllowed(size_t type_index) const {
    if (!restrictTypes_) return true;
    return allowedTypes_.count(type_index) > 0;
}

} // namespace btoon
