// SPDX-FileCopyrightText: 2025 Alvar Laigna <https://alvarlaigna.com>
// SPDX-License-Identifier: MIT
/**
 * @file security_validator.h
 * @brief EXPERIMENTAL, not built or installed: heuristic pre-checks on encoded input.
 *
 * Moved out of include/btoon/validator.h in 0.3.0 (review 2026-11-13). The checks only
 * scanned bytes instead of parsing them: checkNestingDepth() counted every container
 * marker anywhere and never decremented, so a flat array of 200 empty maps, or a binary
 * full of 0x90 bytes, was "too deep", and checkSizeClaims() stopped at the first marker
 * other than a string or fixint. The decoder enforces depth, length and decompression
 * limits exactly (docs/SPEC.md §7), so Validator reports those as errors instead.
 */
#ifndef BTOON_SECURITY_VALIDATOR_H
#define BTOON_SECURITY_VALIDATOR_H

#include "btoon/btoon.h"

#include <span>
#include <string>
#include <vector>

namespace btoon {

/**
 * @brief Security-focused validator
 */
class SecurityValidator {
public:
    /**
     * @brief Check for potential security issues
     * @param data Raw BTOON data
     * @return List of security concerns found
     */
    static std::vector<std::string> checkSecurity(std::span<const uint8_t> data);
    
    /**
     * @brief Check for zip bomb patterns
     */
    static bool checkForZipBomb(std::span<const uint8_t> data);
    
    /**
     * @brief Check for excessive nesting (stack overflow risk)
     */
    static bool checkNestingDepth(std::span<const uint8_t> data, size_t maxDepth = 128);
    
    /**
     * @brief Check for unreasonable size claims
     */
    static bool checkSizeClaims(std::span<const uint8_t> data);
};

} // namespace btoon

#endif // BTOON_SECURITY_VALIDATOR_H
