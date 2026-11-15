/**
 * @file ufsrv_config_secret.c
 * @brief Runtime decryption of encrypted config-file secret values.
 *
 * A config-file secret value may be stored encrypted in the Lua config file
 * ("enc:v1:<hex_nonce>:<hex_ciphertext>:<hex_tag>").  The AES-256-GCM key is a
 * single application-wide key held in the systemd credential store
 * ($CREDENTIALS_DIRECTORY/ufsrv_secret_key) and is never written to a config file.
 *
 * Copyright (C) 2015-2026 unfacd works
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifdef HAVE_CONFIG_UFSRVCORELIB_H
#include <config_ufsrvcorelib.h>
#endif

#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "ufsrv_core/ufsrv_config/ufsrv_config_secret.h"
#include "ufsrv_core/system/ufsrv_secret_key_type.h"

static void
sCleanupFree(void *value_ptr)
{
  free(*(void **)value_ptr);
}

static int
sHexDigitValue(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int
sHexDecode(const char *hex, unsigned char *out, size_t out_cap)
{
  size_t n = strlen(hex);
  if (n == 0 || (n % 2) != 0) return -1;
  if (n / 2 > out_cap) return -1;

  for (size_t i = 0; i < n / 2; i++) {
    int hi = sHexDigitValue(hex[2 * i]);
    int lo = sHexDigitValue(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return (int)(n / 2);
}

/*!
 * AES-256-GCM authenticated decryption.  On success allocates a NUL-terminated
 * plaintext into @p plaintext_out (caller frees with free()).
 */
static int
sAes256GcmDecrypt(const unsigned char key[SECRET_KEY_BYTES],
                  const unsigned char *nonce, size_t nonce_len,
                  const unsigned char *ct, size_t ct_len,
                  const unsigned char *tag, size_t tag_len,
                  char **plaintext_out)
{
  EVP_CIPHER_CTX *ctx = NULL;
  unsigned char *buf = NULL;
  char *result = NULL;
  int out_len = 0, final_len = 0;
  int ret = -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx) goto cleanup;

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) goto cleanup;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)nonce_len, NULL) != 1) goto cleanup;
  if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto cleanup;

  buf = malloc(ct_len + 1);
  if (!buf) goto cleanup;

  if (EVP_DecryptUpdate(ctx, buf, &out_len, ct, (int)ct_len) != 1) goto cleanup;
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)tag_len, (void *)tag) != 1) goto cleanup;
  if (EVP_DecryptFinal_ex(ctx, buf + out_len, &final_len) != 1) goto cleanup; /* tag mismatch */

  out_len += final_len;
  buf[out_len] = '\0';
  result = (char *)buf;
  buf = NULL;
  ret = 0;

cleanup:
  if (buf) {
    OPENSSL_cleanse(buf, ct_len + 1);
    free(buf);
  }
  EVP_CIPHER_CTX_free(ctx);
  if (ret == 0) *plaintext_out = result;
  return ret;
}

/**
 *
unsigned char keyx[SECRET_KEY_BYTES];
  if (sReadKeyFromCredential(key, ufsrv_confi_key) != 0) return -1;
 */
PUBLIC_API int
SecretConfigDecrypt(const char *encrypted_value, const unsigned char key[SECRET_KEY_BYTES], char **plaintext_out)
{
  if (!encrypted_value || !*encrypted_value || !plaintext_out) return -1;

  /* not encrypted — return verbatim */
  if (strncmp(encrypted_value, SECRET_PREFIX, SECRET_PREFIX_LEN) != 0) {
    *plaintext_out = strdup(encrypted_value);
    return *plaintext_out ? 0 : -1;
  }

  const char *payload = encrypted_value + SECRET_PREFIX_LEN;

  /* split "<nonce>:<ciphertext>:<tag>" — the ciphertext is as long as the plaintext, so the working copy is sized from the value, not from a fixed buffer */
  __attribute__((cleanup(sCleanupFree))) char *buf = strdup(payload);
  __attribute__((cleanup(sCleanupFree))) unsigned char *ct = NULL;
  if (!buf) return -1;

  char *nonce_hex = buf;
  char *ct_hex   = strchr(nonce_hex, ':');
  if (!ct_hex) goto malformed;
  *ct_hex++ = '\0';
  char *tag_hex  = strchr(ct_hex, ':');
  if (!tag_hex) goto malformed;
  *tag_hex++ = '\0';
  if (strchr(tag_hex, ':')) goto malformed;

  unsigned char nonce[SECRET_NONCE_BYTES];
  unsigned char tag[SECRET_TAG_BYTES];
  if (sHexDecode(nonce_hex, nonce, sizeof(nonce)) != SECRET_NONCE_BYTES) goto malformed;
  if (sHexDecode(tag_hex, tag, sizeof(tag)) != SECRET_TAG_BYTES) goto malformed;

  size_t ct_hex_len = strlen(ct_hex);
  ct = malloc(ct_hex_len / 2);
  if (!ct) return -1;
  int ct_len = sHexDecode(ct_hex, ct, ct_hex_len / 2);
  if (ct_len < 0) goto malformed;

  int rc = sAes256GcmDecrypt(key, nonce, sizeof(nonce), ct, (size_t)ct_len, tag, sizeof(tag), plaintext_out);
  if (rc != 0 || !*plaintext_out) {
    syslog(LOG_ERR, ">>> %s: AES-256-GCM decryption failed (bad key or tampered ciphertext)", __func__);
    return -1;
  }

  return 0;

malformed:
  syslog(LOG_ERR, ">>> %s: malformed encrypted value (expected '%s<nonce>:<ciphertext>:<tag>')", __func__, SECRET_PREFIX);
  return -1;
}

PUBLIC_API int
SecretConfigReadKeyFromCredential(unsigned char key[SECRET_KEY_BYTES], const char *credentials_directory_and_filename)
{
  FILE *f = fopen(credentials_directory_and_filename, "r");
  if (!f) {
    syslog(LOG_ERR, ">>> %s: cannot open credential '%s': %m", __func__, credentials_directory_and_filename);
    return -1;
  }

  char hex[2 * SECRET_KEY_BYTES + 2] = {0};
  if (fgets(hex, sizeof(hex), f) == NULL) {
    syslog(LOG_ERR, ">>> %s: credential '%s' is empty", __func__, credentials_directory_and_filename);
    fclose(f);
    return -1;
  }
  fclose(f);

  /* strip trailing newline / whitespace */
  hex[strcspn(hex, "\r\n")] = '\0';

  int n = sHexDecode(hex, key, SECRET_KEY_BYTES);
  if (n != SECRET_KEY_BYTES) {
    syslog(LOG_ERR, ">>> %s: credential '%s' must be %d hex chars (%d bytes), got %d bytes", __func__, credentials_directory_and_filename, 2 * SECRET_KEY_BYTES, SECRET_KEY_BYTES, n);
    return -1;
  }

  return 0;
}