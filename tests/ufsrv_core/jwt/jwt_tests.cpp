//
// tests/jwt/jwt_tests.cpp — compact JWS minting and verification
//
// Keys are generated in the test process: no key material is committed, and a
// fresh key per fixture means a stale token can never accidentally verify.
//

#include <gtest/gtest.h>

#include <ufsrv_core/jwt/jwt.h>

#include <json-c/json.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <string>

namespace {

std::string
PemFromKey(EVP_PKEY *pkey, bool is_private)
{
  BIO *bio = BIO_new(BIO_s_mem());
  char *data = nullptr;
  long  len;

  if (is_private) {
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  } else {
    PEM_write_bio_PUBKEY(bio, pkey);
  }

  len = BIO_get_mem_data(bio, &data);

  std::string pem(data, (size_t)len);

  BIO_free(bio);

  return pem;
}

EVP_PKEY *
GenerateKey(void)
{
  return EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", (size_t)2048);
}

std::string
GeneratePrivatePem(void)
{
  EVP_PKEY *pkey = GenerateKey();
  std::string pem = PemFromKey(pkey, true);

  EVP_PKEY_free(pkey);

  return pem;
}

std::string
GeneratePublicPem(void)
{
  EVP_PKEY *pkey = GenerateKey();
  std::string pem = PemFromKey(pkey, false);

  EVP_PKEY_free(pkey);

  return pem;
}

json_object *
MakeClaims(const char *issuer, long long expiry, const char *audience = "api.unfacd.io")
{
  json_object *claims = json_object_new_object();

  json_object_object_add(claims, "iss", json_object_new_string(issuer));
  json_object_object_add(claims, "sub", json_object_new_string("42"));
  json_object_object_add(claims, "exp", json_object_new_int64(expiry));

  if (audience != nullptr) {
    json_object_object_add(claims, "aud", json_object_new_string(audience));
  }

  return claims;
}

JwtVerifyOptions
Options(const char *issuer, const char *audience, bool require_expiry, unsigned leeway)
{
  JwtVerifyOptions options;

  options.issuer         = issuer;
  options.audience       = audience;
  options.require_expiry = require_expiry;
  options.leeway_seconds = leeway;

  return options;
}

std::string
ReplaceHeader(const std::string &token, const char *header_segment)
{
  size_t first = token.find('.');
  size_t last  = token.rfind('.');

  return std::string(header_segment) + token.substr(first, last - first) + token.substr(last);
}

} // namespace

class JwtTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    private_pem_ = GeneratePrivatePem();
    keyset_      = JwtKeysetNew();

    ASSERT_NE(keyset_, nullptr);
    ASSERT_EQ(JwtKeysetAddPem(keyset_, private_pem_.c_str(), private_pem_.size(), nullptr, err_, sizeof err_),
              JWT_STATUS_OK)
        << err_;
  }

  void TearDown() override
  {
    JwtKeysetFree(keyset_);
  }

  std::string Sign(json_object *claims, const char *key_id = nullptr)
  {
    char *token = JwtMint(keyset_, key_id, claims, err_, sizeof err_);

    EXPECT_NE(token, nullptr) << err_;

    std::string result = token != nullptr ? token : "";

    free(token);

    return result;
  }

  std::string private_pem_;
  JwtKeyset  *keyset_ = nullptr;
  char        err_[512] = {0};
};

TEST_F(JwtTest, RoundTripReturnsTheClaimsThatWereSigned)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);

  ASSERT_FALSE(token.empty());

  json_object *received = nullptr;

  ASSERT_EQ(JwtVerify(token.c_str(), keyset_, nullptr, &received), JWT_STATUS_OK);

  json_object *subject = nullptr;

  ASSERT_TRUE(json_object_object_get_ex(received, "sub", &subject));
  EXPECT_STREQ(json_object_get_string(subject), "42");

  json_object_put(received);
  json_object_put(claims);
}

TEST_F(JwtTest, TokenIsThreeBase64UrlSegments)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);

  EXPECT_EQ(std::count(token.begin(), token.end(), '.'), 2);
  EXPECT_EQ(token.find_first_of("=+/"), std::string::npos);

  json_object_put(claims);
}

TEST_F(JwtTest, TamperedPayloadIsRejected)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);

  ASSERT_GE(token.size(), 10u);

  std::string tampered = token;
  tampered[tampered.size() / 2] = tampered[tampered.size() / 2] == 'A' ? 'B' : 'A';

  json_object *received = nullptr;

  EXPECT_EQ(JwtVerify(tampered.c_str(), keyset_, nullptr, &received), JWT_STATUS_BAD_SIGNATURE);
  EXPECT_EQ(received, nullptr);

  json_object_put(claims);
}

TEST_F(JwtTest, TokenSignedByAnotherKeyIsRejected)
{
  json_object *claims   = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token    = Sign(claims);
  std::string  other    = GeneratePrivatePem();
  JwtKeyset   *other_ks = JwtKeysetNew();
  json_object *received = nullptr;

  ASSERT_EQ(JwtKeysetAddPem(other_ks, other.c_str(), other.size(), nullptr, nullptr, 0), JWT_STATUS_OK);
  EXPECT_EQ(JwtVerify(token.c_str(), other_ks, nullptr, &received), JWT_STATUS_UNKNOWN_KEY);

  JwtKeysetFree(other_ks);
  json_object_put(claims);
}

TEST_F(JwtTest, ExpiredTokenIsRejectedUnlessLeewayCoversIt)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) - 10);
  std::string  token  = Sign(claims);
  json_object *received = nullptr;

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, nullptr, &received), JWT_STATUS_EXPIRED);

  JwtVerifyOptions options = Options(nullptr, nullptr, true, 60);

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &options, &received), JWT_STATUS_OK);
  json_object_put(received);

  json_object_put(claims);
}

TEST_F(JwtTest, MissingExpiryIsRejectedWhenRequired)
{
  json_object *claims = json_object_new_object();
  std::string  token;
  json_object *received = nullptr;

  json_object_object_add(claims, "iss", json_object_new_string("https://api.unfacd.io"));
  token = Sign(claims);

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, nullptr, &received), JWT_STATUS_MISSING_EXPIRY);

  JwtVerifyOptions options = Options(nullptr, nullptr, false, 0);

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &options, &received), JWT_STATUS_OK);
  json_object_put(received);

  json_object_put(claims);
}

TEST_F(JwtTest, TokenNotYetValidIsRejected)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 600);
  json_object *received = nullptr;

  json_object_object_add(claims, "nbf", json_object_new_int64(time(nullptr) + 300));

  std::string token = Sign(claims);

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, nullptr, &received), JWT_STATUS_NOT_YET_VALID);
  json_object_put(claims);
}

TEST_F(JwtTest, IssuerAndAudienceAreEnforcedWhenRequested)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);
  json_object *received = nullptr;

  JwtVerifyOptions good = Options("https://api.unfacd.io", "api.unfacd.io", true, 0);
  JwtVerifyOptions bad_issuer = good;
  JwtVerifyOptions bad_audience = good;

  bad_issuer.issuer     = "https://evil.example";
  bad_audience.audience = "other.example";

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &good, &received), JWT_STATUS_OK);
  json_object_put(received);
  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &bad_issuer, nullptr), JWT_STATUS_BAD_ISSUER);
  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &bad_audience, nullptr), JWT_STATUS_BAD_AUDIENCE);

  json_object_put(claims);
}

TEST_F(JwtTest, AudienceArrayIsMatched)
{
  json_object *claims   = MakeClaims("https://api.unfacd.io", time(nullptr) + 60, nullptr);
  json_object *audience = json_object_new_array();
  json_object *received = nullptr;

  json_object_array_add(audience, json_object_new_string("first.example"));
  json_object_array_add(audience, json_object_new_string("second.example"));
  json_object_object_add(claims, "aud", audience);

  std::string token = Sign(claims);

  JwtVerifyOptions options = Options(nullptr, "second.example", true, 0);

  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, &options, &received), JWT_STATUS_OK);
  json_object_put(received);

  json_object_put(claims);
}

TEST_F(JwtTest, MalformedTokensAreRejected)
{
  json_object *received = nullptr;

  EXPECT_EQ(JwtVerify("not-a-token", keyset_, nullptr, &received), JWT_STATUS_MALFORMED);
  EXPECT_EQ(JwtVerify("one.two", keyset_, nullptr, &received), JWT_STATUS_MALFORMED);
  EXPECT_EQ(JwtVerify("one.two.three.four", keyset_, nullptr, &received), JWT_STATUS_MALFORMED);
  EXPECT_EQ(JwtVerify("..", keyset_, nullptr, &received), JWT_STATUS_MALFORMED);
  EXPECT_EQ(JwtVerify("", keyset_, nullptr, &received), JWT_STATUS_MALFORMED);
}

TEST_F(JwtTest, ForeignAlgorithmIsRejected)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);

  std::string hs256 = ReplaceHeader(token, "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9");

  EXPECT_EQ(JwtVerify(hs256.c_str(), keyset_, nullptr, nullptr), JWT_STATUS_UNSUPPORTED_ALGORITHM);

  json_object_put(claims);
}

TEST_F(JwtTest, PublicOnlyKeyCannotMint)
{
  std::string    public_pem = GeneratePublicPem();
  JwtKeyset     *public_ks  = JwtKeysetNew();
  json_object   *claims     = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);

  ASSERT_EQ(JwtKeysetAddPem(public_ks, public_pem.c_str(), public_pem.size(), nullptr, nullptr, 0),
            JWT_STATUS_OK);

  EXPECT_EQ(JwtMint(public_ks, nullptr, claims, nullptr, 0), nullptr);

  JwtKeysetFree(public_ks);
  json_object_put(claims);
}

TEST_F(JwtTest, CallerSuppliedKeyIdIsCarriedInTheHeader)
{
  std::string  pem     = GeneratePrivatePem();
  JwtKeyset   *keyset  = JwtKeysetNew();
  json_object *claims  = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);

  ASSERT_EQ(JwtKeysetAddPem(keyset, pem.c_str(), pem.size(), "e4", nullptr, 0), JWT_STATUS_OK);
  ASSERT_STREQ(JwtKeysetKidAt(keyset, 0), "e4");

  char *token = JwtMint(keyset, "e4", claims, nullptr, 0);

  ASSERT_NE(token, nullptr);
  EXPECT_EQ(JwtVerify(token, keyset, nullptr, nullptr), JWT_STATUS_OK);

  JwtKeyset *derived = JwtKeysetNew();

  ASSERT_EQ(JwtKeysetAddPem(derived, pem.c_str(), pem.size(), nullptr, nullptr, 0), JWT_STATUS_OK);
  EXPECT_NE(std::string(JwtKeysetKidAt(derived, 0)), "e4");
  EXPECT_EQ(JwtVerify(token, derived, nullptr, nullptr), JWT_STATUS_UNKNOWN_KEY);

  free(token);
  JwtKeysetFree(derived);
  JwtKeysetFree(keyset);
  json_object_put(claims);
}

TEST_F(JwtTest, DuplicateKeyIdsAreRejected)
{
  std::string  pem = GeneratePrivatePem();
  JwtKeyset   *set = JwtKeysetNew();

  ASSERT_EQ(JwtKeysetAddPem(set, pem.c_str(), pem.size(), "e4", nullptr, 0), JWT_STATUS_OK);
  EXPECT_EQ(JwtKeysetAddPem(set, pem.c_str(), pem.size(), "e4", nullptr, 0), JWT_STATUS_KEY_ERROR);

  JwtKeysetFree(set);
}

TEST_F(JwtTest, JwksPublishesTheKeyThatSignedTheToken)
{
  char *document = JwtKeysetRenderJwks(keyset_);

  ASSERT_NE(document, nullptr);

  json_object *parsed = json_tokener_parse(document);
  json_object *keys   = nullptr;

  ASSERT_NE(parsed, nullptr);
  ASSERT_TRUE(json_object_object_get_ex(parsed, "keys", &keys));
  ASSERT_EQ(json_object_array_length(keys), 1u);

  json_object *entry = json_object_array_get_idx(keys, 0);
  json_object *field = nullptr;

  ASSERT_TRUE(json_object_object_get_ex(entry, "kid", &field));
  EXPECT_STREQ(json_object_get_string(field), JwtKeysetKidAt(keyset_, 0));

  ASSERT_TRUE(json_object_object_get_ex(entry, "alg", &field));
  EXPECT_STREQ(json_object_get_string(field), "RS256");

  ASSERT_TRUE(json_object_object_get_ex(entry, "kty", &field));
  EXPECT_STREQ(json_object_get_string(field), "RSA");

  ASSERT_TRUE(json_object_object_get_ex(entry, "n", &field));
  EXPECT_GT(strlen(json_object_get_string(field)), 200u);

  ASSERT_TRUE(json_object_object_get_ex(entry, "e", &field));
  EXPECT_STREQ(json_object_get_string(field), "AQAB");

  json_object_put(parsed);
  free(document);
}

TEST_F(JwtTest, VerificationRequiresATokenAndAKeySet)
{
  json_object *claims = MakeClaims("https://api.unfacd.io", time(nullptr) + 60);
  std::string  token  = Sign(claims);

  EXPECT_EQ(JwtVerify(token.c_str(), nullptr, nullptr, nullptr), JWT_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(JwtVerify(nullptr, keyset_, nullptr, nullptr), JWT_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(JwtVerify(token.c_str(), keyset_, nullptr, nullptr), JWT_STATUS_OK);

  json_object_put(claims);
}
