/*
 Copyright (c) 2015-2026 unfacd works

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#ifndef UFSRV_UFNET_CORE_JWT_JWT_H
#define UFSRV_UFNET_CORE_JWT_JWT_H

/**
 * @file jwt.h
 * @brief Compact JWS (JWT) minting and verification, and the JWKS document that
 *        publishes the corresponding public keys.
 *
 * The module knows nothing about who uses it: claims are a caller-built JSON
 * object and the signing key is a PEM file.  A consumer supplies both, and gets
 * back a compact token or a parsed claim set.
 *
 * `JwtMint` and `JwtVerify` use `RS256`. `JwtMintEd25519` and `JwtVerifyEd25519` use `EdDSA` with Ed25519 keys.  A key's `kid` is its RFC 7638
 * thumbprint unless the caller supplies one at load time, which is how a
 * foreign issuer's published key id is honoured.
 */

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <json-c/json.h>

#include <uflib/uflib_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum JwtStatus {
  JWT_STATUS_OK = 0,
  JWT_STATUS_INVALID_ARGUMENT,      ///< a NULL or empty argument where one is required
  JWT_STATUS_KEY_ERROR,             ///< the key could not be read, derived or used
  JWT_STATUS_MALFORMED,             ///< not three segments, bad base64url, or unusable JSON
  JWT_STATUS_UNSUPPORTED_ALGORITHM, ///< the header does not name RS256
  JWT_STATUS_UNKNOWN_KEY,           ///< the header's kid names no key in the set
  JWT_STATUS_BAD_SIGNATURE,         ///< the signature does not verify
  JWT_STATUS_MISSING_EXPIRY,        ///< no exp claim while the options require one
  JWT_STATUS_EXPIRED,               ///< exp is in the past
  JWT_STATUS_NOT_YET_VALID,         ///< nbf is in the future
  JWT_STATUS_BAD_ISSUER,            ///< iss does not match the expected issuer
  JWT_STATUS_BAD_AUDIENCE           ///< aud does not name the expected audience
} JwtStatus;

/**
 * @brief A set of signing and/or verification keys.
 *
 * Opaque: load keys with JwtKeysetAddPemFile() or JwtKeysetAddPem(), sign with
 * JwtMint(), verify with JwtVerify().  Read-only once loaded, so a loaded set
 * may be shared between threads.
 */
typedef struct JwtKeyset JwtKeyset;

/**
 * @brief Allocate an empty key set.
 * @return the set, or NULL when memory is exhausted.  Free with JwtKeysetFree().
 */
PUBLIC_API JwtKeyset *JwtKeysetNew(void);

/**
 * @brief Release a key set and every key it holds.
 * @param keyset the set; NULL is a no-op.
 */
PUBLIC_API void JwtKeysetFree(JwtKeyset *keyset);

/**
 * @brief Load a PEM key file into the set.
 *
 * A private key loads with its public half and can sign; a public key alone
 * loads for verification only.
 *
 * @param keyset  target set
 * @param path    PEM file path
 * @param key_id  `kid` to associate with the key; NULL derives the RFC 7638
 *                thumbprint, which is what a sender of our own tokens uses
 * @param err     optional buffer receiving a human-readable reason on failure
 * @param err_sz  size of @p err
 * @return JWT_STATUS_OK, or a status describing the failure
 */
PUBLIC_API JwtStatus JwtKeysetAddPemFile(JwtKeyset *keyset, const char *path, const char *key_id,
                                         char *err, size_t err_sz);

/**
 * @brief Load a PEM key held in memory into the set.
 *
 * @param keyset  target set
 * @param pem     PEM text
 * @param pem_sz  length of @p pem; 0 means NUL-terminated
 * @param key_id  `kid` to associate with the key, or NULL to derive it
 * @param err     optional buffer receiving a reason on failure
 * @param err_sz  size of @p err
 * @return JWT_STATUS_OK, or a status describing the failure
 */
PUBLIC_API JwtStatus JwtKeysetAddPem(JwtKeyset *keyset, const char *pem, size_t pem_sz,
                                     const char *key_id, char *err, size_t err_sz);

/**
 * @brief Number of keys in the set.
 */
PUBLIC_API size_t JwtKeysetCount(const JwtKeyset *keyset);

/**
 * @brief The `kid` of the key at @p index.
 * @return the identifier, or NULL when @p index is out of range.
 */
PUBLIC_API const char *JwtKeysetKidAt(const JwtKeyset *keyset, size_t index);

/**
 * @brief Mint a compact JWS over @p claims.
 *
 * The header is built here (`alg`, `typ`, `kid`); the caller owns the claim
 * set, including `iat` and `exp`.  The key named by @p key_id must be in the
 * set and must carry a private half.
 *
 * @param keyset   loaded key set
 * @param key_id   which key signs; NULL selects the only key in the set
 * @param claims   claim set to sign
 * @param err      optional buffer receiving a reason on failure
 * @param err_sz   size of @p err
 * @return malloc'd `header.payload.signature` text, or NULL on failure
 */
PUBLIC_API char *JwtMint(const JwtKeyset *keyset, const char *key_id, const json_object *claims,
                         char *err, size_t err_sz);

/** Mint an EdDSA compact JWT with a PEM Ed25519 private key loaded into the key set.
 * Key selection and ownership follow JwtMint(). The caller supplies all claims. */
PUBLIC_API char *JwtMintEd25519(const JwtKeyset *keyset, const char *key_id, const json_object *claims,
                                char *err, size_t err_sz);

/** Mint an EdDSA JWT directly from a private PEM file. Free the returned token with free(). */
PUBLIC_API char *JwtMintEd25519FromPemFile(const char *path, const json_object *claims,
                                           char *err, size_t err_sz);

/** Mint an EdDSA JWT directly from private PEM bytes. A zero length means NUL-terminated PEM. */
PUBLIC_API char *JwtMintEd25519FromPem(const char *pem, size_t pem_sz, const json_object *claims,
                                       char *err, size_t err_sz);

/** Extract the unpadded base64url Ed25519 public key from a PEM key.
 * A zero length means NUL-terminated PEM. Free the returned string with free(). */
PUBLIC_API char *JwtEd25519PublicFromPem(const char *pem, size_t pem_sz, char *err, size_t err_sz);

/** Import a NUL-terminated, unpadded base64url Ed25519 public key from memory.
 * Accepts an optional final LF or CRLF. The key set owns the imported key. */
PUBLIC_API JwtStatus JwtKeysetAddEd25519Public(JwtKeyset *keyset, const char *public_key, const char *key_id,
                                               char *err, size_t err_sz);

/** Add a verification key from a file containing a raw, unpadded base64url Ed25519 public key.
 * An optional final LF or CRLF is accepted. A NULL key_id derives the JWK thumbprint. */
PUBLIC_API JwtStatus JwtKeysetAddEd25519PublicFile(JwtKeyset *keyset, const char *path, const char *key_id,
                                                    char *err, size_t err_sz);

/**
 * @brief Expectations applied when verifying.
 *
 * A NULL @p options for JwtVerify() means: require `exp`, accept any `iss` and
 * `aud`, and allow no clock skew.
 */
typedef struct JwtVerifyOptions {
  const char *issuer;           ///< exact `iss` required, or NULL to accept any
  const char *audience;         ///< `aud` must name this value, or NULL to accept any
  bool        require_expiry;   ///< reject a token carrying no `exp`
  unsigned    leeway_seconds;   ///< tolerance applied to `exp` and `nbf`
} JwtVerifyOptions;

/**
 * @brief Verify a compact JWS and return its claim set.
 *
 * Signature, algorithm, key selection, expiry and the optional issuer/audience
 * constraints are all checked; on JWT_STATUS_OK @p claims_out holds a new
 * reference the caller releases with `json_object_put()`.
 *
 * @param token      compact `header.payload.signature` text
 * @param keyset     keys to verify against
 * @param options    expectations, or NULL for the defaults described above
 * @param claims_out receives the claim set on success, or NULL when the caller
 *                   only wants the verdict
 * @return JWT_STATUS_OK or the first check that failed
 */
PUBLIC_API JwtStatus JwtVerify(const char *token, const JwtKeyset *keyset,
                               const JwtVerifyOptions *options, json_object **claims_out);

/** Verify an EdDSA compact JWT using only Ed25519 keys from the trusted set. */
PUBLIC_API JwtStatus JwtVerifyEd25519(const char *token, const JwtKeyset *keyset,
                                      const JwtVerifyOptions *options, json_object **claims_out);

/**
 * @brief Render the public half of every key as a JWKS document.
 * @return malloc'd JSON text, or NULL when the set is empty or memory is short.
 */
PUBLIC_API char *JwtKeysetRenderJwks(const JwtKeyset *keyset);

/**
 * @brief A stable, human-readable name for @p status.
 */
PUBLIC_API const char *JwtStatusDescription(JwtStatus status);

#ifdef __cplusplus
}
#endif

#endif /* UFSRV_UFNET_CORE_JWT_JWT_H */
