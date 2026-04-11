#ifndef COINBASE_DECODER_H
#define COINBASE_DECODER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "stratum_api.h"

#define MAX_ADDRESS_STRING_LEN 128
#define MAX_COINBASE_TX_OUTPUTS 6

// Bitcoin Script Opcodes
#define OP_0            0x00
#define OP_PUSHDATA_20  0x14  // Push next 20 bytes
#define OP_PUSHDATA_32  0x20  // Push next 32 bytes
#define OP_1            0x51
#define OP_RETURN       0x6a
#define OP_DUP          0x76
#define OP_EQUAL        0x87
#define OP_EQUALVERIFY  0x88
#define OP_HASH160      0xa9
#define OP_CHECKSIG     0xac

/**
 * @brief Decode Bitcoin varint from binary data
 * 
 * @param data Binary data containing the varint
 * @param offset Pointer to current offset, will be updated after reading
 * @return Decoded varint value
 */
uint64_t coinbase_decode_varint(const uint8_t *data, int *offset);

/**
 * @brief Coinbase decoding network selection
 */
typedef enum {
    COINBASE_NETWORK_DISABLED = 0,
    COINBASE_NETWORK_BTC      = 1,
    COINBASE_NETWORK_BCH      = 2,
    COINBASE_NETWORK_AUTO     = 3,  // detect from payout address format
} coinbase_network_t;

/**
 * @brief Decode Bitcoin address from scriptPubKey
 *
 * Supports P2PKH, P2SH, P2WPKH, P2WSH, and P2TR address types.
 * For BCH, P2PKH and P2SH use CashAddr encoding; segwit types fall through to hex.
 *
 * @param script ScriptPubKey binary data
 * @param script_len Length of scriptPubKey
 * @param network Network to encode address for (BTC or BCH)
 * @param output Output buffer for address string
 * @param output_len Size of output buffer (should be at least MAX_ADDRESS_STRING_LEN)
 */
void coinbase_decode_address_from_scriptpubkey(const uint8_t *script, size_t script_len,
                                                coinbase_network_t network,
                                                char *output, size_t output_len);

/**
 * @brief Check whether a scriptPubKey pays to the given user address.
 *        Compares by hash160 bytes for BCH (handles legacy, CashAddr with/without prefix).
 *        Compares by string prefix for BTC.
 *        Strips ".workername" suffix from user_address before comparing.
 */
bool coinbase_is_user_output(const uint8_t *script, size_t script_len,
                              coinbase_network_t network, const char *user_address);

/**
 * @brief Structure representing a decoded coinbase transaction output
 */
typedef struct {
    uint64_t value_satoshis;
    char address[MAX_ADDRESS_STRING_LEN];
    bool is_user_output;
} coinbase_output_t;

/**
 * @brief Decode a variable-length integer from binary data
 * 
 * @param data Binary data containing the varint
 * @param offset Pointer to current offset (will be updated)
 * @return Decoded integer value
 */
uint64_t coinbase_decode_varint(const uint8_t *data, int *offset);

/**
 * @brief Result structure for full mining notification processing
 */
typedef struct {
    double network_difficulty;
    uint32_t block_height;
    char *scriptsig; // Allocated, must be freed by caller
    coinbase_output_t outputs[MAX_COINBASE_TX_OUTPUTS];
    int output_count;
    uint64_t total_value_satoshis;
    uint64_t user_value_satoshis;
    bool decoding_enabled;
} mining_notification_result_t;

/**
 * @brief Process a mining notification to extract all relevant data
 * 
 * @param notification Pointer to the mining notification
 * @param extranonce1 Hex string of extranonce1
 * @param extranonce2_len Length of extranonce2 in bytes
 * @param user_address Payout address of the user
 * @param coinbase_network Network for address encoding (COINBASE_NETWORK_DISABLED/BTC/BCH)
 * @param result Pointer to store the results
 * @return esp_err_t
 */
esp_err_t coinbase_process_notification(const mining_notify *notification,
                                 const char *extranonce1,
                                 int extranonce2_len,
                                 const char *user_address,
                                 coinbase_network_t coinbase_network,
                                 mining_notification_result_t *result);

#endif // COINBASE_DECODER_H
