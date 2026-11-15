/**
 * @file ufsrv_config_secret.h
 * @brief Runtime decryption of encrypted config-file secret values.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef UFSRVCORELIB_UFSRV_CONFIG_SECRET_H
#define UFSRVCORELIB_UFSRV_CONFIG_SECRET_H

#include "ufsrvcorelib_defs.h"
#include "ufsrv_core/system/ufsrv_secret_key_type.h"

/*!
 * Return the plaintext for a possibly-encrypted config-file value.
 *
 * If @p encrypted_value begins with @c "enc:v1:", it is decrypted using the
 * application-wide AES-256-GCM key held in the systemd credential store at
 * $CREDENTIALS_DIRECTORY/ufsrv_secret_key (64 hex characters = 32 bytes).
 * Otherwise the value is assumed to already be plaintext and is returned
 * verbatim.
 *
 * On success *plaintext_out points to a malloc'd, NUL-terminated buffer owned
 * by the caller (release with free()) and 0 is returned.
 *
 * @return 0 on success, non-zero on failure.
 */
/**
 * @brief Decrypts an AES-256-GCM encrypted configuration string.
 *
 * @details This function handles both encrypted and plaintext values transparently.
 *          If the input string does not start with the expected SECRET_PREFIX, it
 *          is assumed to be plaintext and is duplicated verbatim.
 *
 *          If the string is encrypted, it expects the format:
 *          `<SECRET_PREFIX><nonce_hex>:<ciphertext_hex>:<tag_hex>`
 *
 *          It parses the components, decodes them from hex, performs AES-256-GCM
 *          decryption, and allocates a new null-terminated string for the result.
 *
 * @param[in]  encrypted_value  The input string to decrypt. Can be plaintext or encrypted.
 * @param[in]  key              The 256-bit decryption key (array of SECRET_KEY_BYTES).
 *                              Read-only; never modified or zeroed by this function.
 *                              The caller retains ownership and must keep it valid for
 *                              the lifetime of any secrets it is used to decrypt.
 * @param[out] plaintext_out    Pointer to a char pointer where the decrypted string will
 *                              be allocated and stored.
 *
 * @return 0 on success.
 * @return -1 on failure (NULL arguments, empty string, malformed format, memory allocation
 *         failure, or decryption/tamper detection failure). Errors are logged to syslog.
 *
 * @note The caller is responsible for freeing the memory pointed to by *plaintext_out
 *       using free() when it is no longer needed.
 */
PUBLIC_API int SecretConfigDecrypt(const char *encrypted_value, const unsigned char key[SECRET_KEY_BYTES], char **plaintext_out);

/**
 * @brief Reads and decodes an AES-256 key from a systemd credential file.
 *
 * @details Looks for a file named SECRET_CREDENTIAL_ID inside the provided
 *          systemd credentials directory (typically sourced from the
 *          CREDENTIALS_DIRECTORY environment variable). The file is expected
 *          to contain the key as a hex-encoded string. It strips any trailing
 *          newlines and decodes the hex directly into the provided key buffer.
 *
 * @param[out] key                    Output buffer to store the decoded binary key.
 *                                    Must be at least SECRET_KEY_BYTES in size.
 * @param[in]  credentials_directory_and_filename  The absolute path and file name to where key store file is stored
 *
 * @return 0 on success.
 * @return -1 on failure (NULL/empty directory, file not found, empty file, or
 *         hex decode size mismatch). Errors are logged to syslog.
 *
 * @note No dynamic memory allocation is performed for the key itself; the caller
 *       provides the buffer.
 */
PUBLIC_API int SecretConfigReadKeyFromCredential(unsigned char key[SECRET_KEY_BYTES], const char *credentials_directory_and_filename);

#endif /* UFSRVCORELIB_UFSRV_CONFIG_SECRET_H */
