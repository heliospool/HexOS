#include "coinbase_decoder.h"
#include "utils.h"
#include "segwit_addr.h"
#include "libbase58.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "mbedtls/sha256.h"

// Wrapper for SHA256 to match libbase58's expected signature
static bool my_sha256(void *digest, const void *data, size_t datasz) {
    mbedtls_sha256(data, datasz, digest, 0);
    return true;
}

static void ensure_base58_init(void) {
    if (b58_sha256_impl == NULL) {
        b58_sha256_impl = my_sha256;
    }
}

// CashAddr encoding for BCH P2PKH and P2SH addresses.
// Spec: https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/cashaddr.md
static const char CASHADDR_CHARSET[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

static uint64_t cashaddr_polymod(const uint8_t *v, size_t len) {
    uint64_t c = 1;
    for (size_t i = 0; i < len; i++) {
        uint8_t c0 = c >> 35;
        c = ((c & 0x07ffffffffULL) << 5) ^ v[i];
        if (c0 & 0x01) c ^= 0x98f2bc8e61ULL;
        if (c0 & 0x02) c ^= 0x79b76d99e2ULL;
        if (c0 & 0x04) c ^= 0xf33e5fb3c4ULL;
        if (c0 & 0x08) c ^= 0xae2eabe2a8ULL;
        if (c0 & 0x10) c ^= 0x1e4f43e470ULL;
    }
    return c ^ 1;
}

// Extract hash160 from a P2PKH or P2SH scriptPubKey.
// Returns true and fills hash160[20] if recognised, false otherwise.
static bool script_get_hash160(const uint8_t *script, size_t script_len, uint8_t hash160[20]) {
    // P2PKH: OP_DUP OP_HASH160 <20> OP_EQUALVERIFY OP_CHECKSIG
    if (script_len == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 &&
        script[2] == OP_PUSHDATA_20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        memcpy(hash160, script + 3, 20);
        return true;
    }
    // P2SH: OP_HASH160 <20> OP_EQUAL
    if (script_len == 23 && script[0] == OP_HASH160 && script[1] == OP_PUSHDATA_20 && script[22] == OP_EQUAL) {
        memcpy(hash160, script + 2, 20);
        return true;
    }
    return false;
}

// Decode a CashAddr string (with or without "bitcoincash:" prefix) to hash160.
static bool cashaddr_decode_hash160(const char *addr, uint8_t hash160[20]) {
    const char *payload = (strncasecmp(addr, "bitcoincash:", 12) == 0) ? addr + 12 : addr;
    size_t plen = strlen(payload);
    if (plen < 9) return false;

    // Strip 8 checksum chars — only decode the data portion
    size_t data_len = plen - 8;
    uint8_t data[40];
    for (size_t i = 0; i < data_len; i++) {
        const char *p = strchr(CASHADDR_CHARSET, tolower((unsigned char)payload[i]));
        if (!p) return false;
        data[i] = (uint8_t)(p - CASHADDR_CHARSET);
    }

    // Convert ALL data_len 5-bit values to bytes.
    // For a P2PKH address: 34 five-bit values → 21 bytes: [version_byte, h0..h19]
    // decoded[0] is the version byte, decoded[1..20] is the hash160.
    uint32_t acc = 0, bits = 0;
    size_t out_i = 0;
    uint8_t decoded[22];
    for (size_t i = 0; i < data_len; i++) {
        acc = (acc << 5) | data[i];
        bits += 5;
        while (bits >= 8 && out_i < sizeof(decoded)) {
            bits -= 8;
            decoded[out_i++] = (acc >> bits) & 0xff;
        }
    }
    if (out_i < 21) return false; // need version + 20 hash bytes
    memcpy(hash160, decoded + 1, 20); // skip version byte
    return true;
}

// Decode a legacy base58check BCH address (starts with '1' or '3') to hash160.
static bool base58_decode_hash160(const char *addr, uint8_t hash160[20]) {
    ensure_base58_init();
    uint8_t bin[25];
    size_t bin_len = sizeof(bin);
    if (!b58tobin(bin, &bin_len, addr, 0)) return false;
    // b58tobin packs result right-aligned; a standard address decodes to 25 bytes
    size_t offset = sizeof(bin) - bin_len; // usually 0 for 25-byte result
    if (bin_len < 20) return false;
    memcpy(hash160, bin + offset + 1, 20); // skip version byte
    return true;
}

// Encode a BCH address in CashAddr format (without "bitcoincash:" prefix).
static bool cashaddr_encode(char *output, size_t output_len,
                             uint8_t version_byte, const uint8_t *hash, size_t hash_len) {
    static const char *prefix = "bitcoincash";
    size_t prefix_len = 11; // strlen("bitcoincash")

    // Pack (version_byte || hash) from 8-bit to 5-bit groups
    uint8_t payload_8bit[33];
    payload_8bit[0] = version_byte;
    memcpy(payload_8bit + 1, hash, hash_len);
    size_t payload_8bit_len = 1 + hash_len; // 21 for 20-byte hash

    // 21 bytes -> ceil(21*8/5) = 34 five-bit values
    size_t payload_5bit_len = (payload_8bit_len * 8 + 4) / 5;
    uint8_t payload_5bit[40]; // enough for 34
    uint32_t acc = 0; uint32_t bits = 0; size_t out_i = 0;
    for (size_t i = 0; i < payload_8bit_len; i++) {
        acc = (acc << 8) | payload_8bit[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            payload_5bit[out_i++] = (acc >> bits) & 0x1f;
        }
    }
    if (bits > 0) payload_5bit[out_i++] = (acc << (5 - bits)) & 0x1f;
    payload_5bit_len = out_i;

    // Build polymod input: prefix_lowbits + 0x00 + payload_5bit + 8 zeros
    size_t pm_len = prefix_len + 1 + payload_5bit_len + 8;
    uint8_t pm_buf[80];
    if (pm_len > sizeof(pm_buf)) return false;
    for (size_t i = 0; i < prefix_len; i++) pm_buf[i] = prefix[i] & 0x1f;
    pm_buf[prefix_len] = 0;
    memcpy(pm_buf + prefix_len + 1, payload_5bit, payload_5bit_len);
    memset(pm_buf + prefix_len + 1 + payload_5bit_len, 0, 8);
    uint64_t mod = cashaddr_polymod(pm_buf, pm_len);

    // Encode: payload_5bit + 8-char checksum (bare, no "bitcoincash:" prefix)
    size_t encoded_len = payload_5bit_len + 8;
    if (output_len < encoded_len + 1) return false;
    for (size_t i = 0; i < payload_5bit_len; i++)
        output[i] = CASHADDR_CHARSET[payload_5bit[i]];
    for (int i = 7; i >= 0; i--) {
        output[payload_5bit_len + (7 - i)] = CASHADDR_CHARSET[(mod >> (5 * i)) & 0x1f];
    }
    output[encoded_len] = '\0';
    return true;
}

uint64_t coinbase_decode_varint(const uint8_t *data, int *offset) {
    uint8_t first_byte = data[*offset];
    (*offset)++;
    
    if (first_byte < 0xFD) {
        return first_byte;
    } else if (first_byte == 0xFD) {
        uint64_t value = data[*offset] | (data[*offset + 1] << 8);
        *offset += 2;
        return value;
    } else if (first_byte == 0xFE) {
        uint64_t value = data[*offset] | (data[*offset + 1] << 8) | 
                        (data[*offset + 2] << 16) | (data[*offset + 3] << 24);
        *offset += 4;
        return value;
    } else { // 0xFF
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value |= ((uint64_t)data[*offset + i]) << (i * 8);
        }
        *offset += 8;
        return value;
    }
}

void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len,
                                                coinbase_network_t network,
                                                char *output, size_t output_len) {
    if (script_len == 0 || output_len < 65) {
        snprintf(output, output_len, "unknown");
        return;
    }

    ensure_base58_init();

    // P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    if (script_len == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 &&
        script[2] == OP_PUSHDATA_20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        if (network == COINBASE_NETWORK_BCH) {
            // CashAddr P2PKH: version byte 0x00
            if (cashaddr_encode(output, output_len, 0x00, script + 3, 20)) return;
        } else {
            size_t b58sz = output_len;
            if (b58check_enc(output, &b58sz, 0x00, script + 3, 20)) return;
        }
        snprintf(output, output_len, "P2PKH:");
        bin2hex(script + 3, 20, output + 6, output_len - 6);
        return;
    }

    // P2SH: OP_HASH160 <20 bytes> OP_EQUAL
    if (script_len == 23 && script[0] == OP_HASH160 && script[1] == OP_PUSHDATA_20 && script[22] == OP_EQUAL) {
        if (network == COINBASE_NETWORK_BCH) {
            // CashAddr P2SH: version byte 0x08
            if (cashaddr_encode(output, output_len, 0x08, script + 2, 20)) return;
        } else {
            size_t b58sz = output_len;
            if (b58check_enc(output, &b58sz, 0x05, script + 2, 20)) return;
        }
        snprintf(output, output_len, "P2SH:");
        bin2hex(script + 2, 20, output + 5, output_len - 5);
        return;
    }

    // BCH does not use native segwit — fall through to hex for P2WPKH/P2WSH/P2TR on BCH
    if (network != COINBASE_NETWORK_BCH) {
        // P2WPKH: OP_0 <20 bytes>
        if (script_len == 22 && script[0] == OP_0 && script[1] == OP_PUSHDATA_20) {
            if (segwit_addr_encode(output, "bc", 0, script + 2, 20)) {
                return;
            }
            snprintf(output, output_len, "P2WPKH:");
            bin2hex(script + 2, 20, output + 7, output_len - 7);
            return;
        }

        // P2WSH: OP_0 <32 bytes>
        if (script_len == 34 && script[0] == OP_0 && script[1] == OP_PUSHDATA_32) {
            if (segwit_addr_encode(output, "bc", 0, script + 2, 32)) {
                return;
            }
            snprintf(output, output_len, "P2WSH:");
            bin2hex(script + 2, 32, output + 6, output_len - 6);
            return;
        }

        // P2TR: OP_1 <32 bytes>
        if (script_len == 34 && script[0] == OP_1 && script[1] == OP_PUSHDATA_32) {
            if (segwit_addr_encode(output, "bc", 1, script + 2, 32)) {
                return;
            }
            snprintf(output, output_len, "P2TR:");
            bin2hex(script + 2, 32, output + 5, output_len - 5);
            return;
        }
    }

    // OP_RETURN: OP_RETURN <data>
    if (script_len > 0 && script[0] == OP_RETURN) {
        snprintf(output, output_len, "OP_RETURN: ");
        size_t offset = 1;
        
        // Simple check for small pushdata to skip the length byte
        // If script[1] is the length of the remaining data
        if (script_len > 1 && script[1] > 0 && script[1] <= 0x4b && (size_t)script[1] + 2 == script_len) {
            offset = 2;
        }
        
        size_t out_idx = strlen(output);
        for (size_t i = offset; i < script_len && out_idx < output_len - 1; i++) {
            unsigned char c = script[i];
            output[out_idx++] = isprint(c) ? c : '.';
        }
        output[out_idx] = '\0';
        return;
    }
    
    // Unknown format - just show hex
    snprintf(output, output_len, "UNKNOWN:");
    size_t hex_len = script_len < 32 ? script_len : 32; // Limit to 32 bytes
    bin2hex(script, hex_len, output + 8, output_len - 8);
}

bool coinbase_is_user_output(const uint8_t *script, size_t script_len,
                              coinbase_network_t network, const char *user_address)
{
    // Strip ".workername" suffix
    char user_addr_only[MAX_ADDRESS_STRING_LEN];
    strncpy(user_addr_only, user_address, MAX_ADDRESS_STRING_LEN - 1);
    user_addr_only[MAX_ADDRESS_STRING_LEN - 1] = '\0';
    char *dot = strrchr(user_addr_only, '.');
    if (dot) *dot = '\0';

    if (network == COINBASE_NETWORK_BCH) {
        uint8_t script_hash[20], user_hash[20];
        if (!script_get_hash160(script, script_len, script_hash)) return false;
        char first = user_addr_only[0];
        bool decoded = (first == '1' || first == '3')
                       ? base58_decode_hash160(user_addr_only, user_hash)
                       : cashaddr_decode_hash160(user_addr_only, user_hash);
        return decoded && (memcmp(script_hash, user_hash, 20) == 0);
    } else {
        // BTC: string prefix comparison against decoded address
        char output_address[MAX_ADDRESS_STRING_LEN];
        coinbase_decode_address_from_scriptpubkey(script, script_len, network, output_address, MAX_ADDRESS_STRING_LEN);
        return strncmp(user_addr_only, output_address, strlen(output_address)) == 0;
    }
}

esp_err_t coinbase_process_notification(const mining_notify *notification,
                                 const char *extranonce1,
                                 int extranonce2_len,
                                 const char *user_address,
                                 coinbase_network_t coinbase_network,
                                 mining_notification_result_t *result) {
    if (!notification || !extranonce1 || !result) return ESP_ERR_INVALID_ARG;

    // Initialize result
    result->total_value_satoshis = 0;
    result->user_value_satoshis = 0;
    result->decoding_enabled = (coinbase_network != COINBASE_NETWORK_DISABLED);

    // 1. Calculate difficulty
    result->network_difficulty = networkDifficulty(notification->target);

    // 2. Parse Coinbase 1 for ScriptSig info
    int coinbase_1_len = strlen(notification->coinbase_1) / 2;
    int coinbase_1_offset = 41; // Skip version (4), inputcount (1), prevhash (32), vout (4)
    
    if (coinbase_1_len < coinbase_1_offset) return ESP_ERR_INVALID_ARG;

    uint8_t scriptsig_len;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), &scriptsig_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset) return ESP_ERR_INVALID_ARG;
    
    uint8_t block_height_len;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), &block_height_len, 1);
    coinbase_1_offset++;

    if (coinbase_1_len < coinbase_1_offset || block_height_len == 0 || block_height_len > 4) return ESP_ERR_INVALID_ARG;

    result->block_height = 0;
    hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *)&result->block_height, block_height_len);
    coinbase_1_offset += block_height_len;

    // Calculate remaining scriptsig length (excluding block height part)
    int scriptsig_length = scriptsig_len - 1 - block_height_len;
    size_t extranonce1_len = strlen(extranonce1) / 2;
    
    // Check if scriptsig extends into coinbase_2 (meaning it covers the extranonces)
    // If so, subtract extranonce lengths to get just the miner tag length
    if (coinbase_1_len - coinbase_1_offset < scriptsig_length) {
        scriptsig_length -= (extranonce1_len + extranonce2_len);
    }
    
    // Extract miner tag if present
    if (scriptsig_length > 0) {
        char *tag = malloc(scriptsig_length + 1);
        if (tag) {
            int coinbase_1_tag_len = coinbase_1_len - coinbase_1_offset;
            if (coinbase_1_tag_len > scriptsig_length) {
                coinbase_1_tag_len = scriptsig_length;
            }

            hex2bin(notification->coinbase_1 + (coinbase_1_offset * 2), (uint8_t *)tag, coinbase_1_tag_len);

            int coinbase_2_tag_len = scriptsig_length - coinbase_1_tag_len;
            int coinbase_2_len = strlen(notification->coinbase_2) / 2;
            
            if (coinbase_2_len >= coinbase_2_tag_len) {
                if (coinbase_2_tag_len > 0) {
                    hex2bin(notification->coinbase_2, (uint8_t *)tag + coinbase_1_tag_len, coinbase_2_tag_len);
                }
                
                // Filter non-printable characters
                for (int i = 0; i < scriptsig_length; i++) {
                    if (!isprint((unsigned char)tag[i])) {
                        tag[i] = '.';
                    }                }
                tag[scriptsig_length] = '\0';
                result->scriptsig = tag;
            } else {
                free(tag);
                // Tag extraction failed due to length mismatch, but we can continue
            }
        }
    }

    // 3. Parse Coinbase 2 for Outputs
    // Calculate offset in coinbase_2 where outputs start
    // Re-calculate raw remainder length without subtracting extranonces
    int raw_scriptsig_remainder = (scriptsig_len - 1 - block_height_len) - (coinbase_1_len - coinbase_1_offset);
    
    int coinbase_2_offset = 0;
    if (raw_scriptsig_remainder > 0) {
        // Subtract extranonce lengths to see what's left for coinbase_2
        int remainder_in_coinbase_2 = raw_scriptsig_remainder - (extranonce1_len + extranonce2_len);
        if (remainder_in_coinbase_2 > 0) {
            coinbase_2_offset = remainder_in_coinbase_2;
        }
    }
    
    int coinbase_2_len = strlen(notification->coinbase_2) / 2;
    uint8_t *coinbase_2_bin = malloc(coinbase_2_len);
    if (!coinbase_2_bin) {
        return ESP_ERR_NO_MEM; // Memory error is fatal
    }
    
    hex2bin(notification->coinbase_2, coinbase_2_bin, coinbase_2_len);
    
    int offset = coinbase_2_offset;
    
    // Skip sequence (4 bytes)
    if (offset + 4 > coinbase_2_len) {
        free(coinbase_2_bin);
        return ESP_ERR_INVALID_ARG; // No room for outputs, but valid notification processed so far
    }
    offset += 4;
    
    // Decode output count
    if (offset >= coinbase_2_len) {
        free(coinbase_2_bin);
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t num_outputs = coinbase_decode_varint(coinbase_2_bin, &offset);
    result->output_count = 0;
    
    // Parse each output
    for (uint64_t i = 0; i < num_outputs && offset < coinbase_2_len; i++) {
        // Read value (8 bytes, little-endian)
        if (offset + 8 > coinbase_2_len) break;

        uint64_t value_satoshis = 0;
        for (int j = 0; j < 8; j++) {
            value_satoshis |= ((uint64_t)coinbase_2_bin[offset + j]) << (j * 8);
        }
        offset += 8;

        // Add to total value
        result->total_value_satoshis += value_satoshis;

        // Read scriptPubKey length
        if (offset >= coinbase_2_len) break;
        uint64_t script_len = coinbase_decode_varint(coinbase_2_bin, &offset);

        if (offset + script_len > coinbase_2_len) break;

        if (coinbase_network != COINBASE_NETWORK_DISABLED) {
            if (value_satoshis > 0) {
                char output_address[MAX_ADDRESS_STRING_LEN];
                coinbase_decode_address_from_scriptpubkey(coinbase_2_bin + offset, script_len, coinbase_network, output_address, MAX_ADDRESS_STRING_LEN);

                bool is_user_address = coinbase_is_user_output(coinbase_2_bin + offset, script_len, coinbase_network, user_address);

                if (is_user_address) result->user_value_satoshis += value_satoshis;

                if (i < MAX_COINBASE_TX_OUTPUTS) {
                    // For the user's own BCH output, display their configured address (any format they
                    // typed) rather than the decoded coinbase address which may be in a different format.
                    char user_addr_only[MAX_ADDRESS_STRING_LEN];
                    strncpy(user_addr_only, user_address, MAX_ADDRESS_STRING_LEN - 1);
                    user_addr_only[MAX_ADDRESS_STRING_LEN - 1] = '\0';
                    char *dot = strrchr(user_addr_only, '.');
                    if (dot) *dot = '\0';

                    const char *display_address = (is_user_address && coinbase_network == COINBASE_NETWORK_BCH)
                                                  ? user_addr_only : output_address;
                    strncpy(result->outputs[i].address, display_address, MAX_ADDRESS_STRING_LEN);
                    result->outputs[i].value_satoshis = value_satoshis;
                    result->outputs[i].is_user_output = is_user_address;
                    result->output_count++;
                }
            } else {
                if (i < MAX_COINBASE_TX_OUTPUTS) {
                    coinbase_decode_address_from_scriptpubkey(coinbase_2_bin + offset, script_len, coinbase_network, result->outputs[i].address, MAX_ADDRESS_STRING_LEN);
                    result->outputs[i].value_satoshis = 0;
                    result->outputs[i].is_user_output = false;
                    result->output_count++;
                }
            }
        }

        offset += script_len;
    }
    
    free(coinbase_2_bin);
    return ESP_OK;
}
