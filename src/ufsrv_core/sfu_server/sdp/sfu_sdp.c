#include "sfu_sdp.h"

#include <arpa/inet.h>
#include <inttypes.h>
#include <json-c/json.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <uflib/buffer_descriptor/buffer_descriptor.h>

#define MID_EXTENSION "urn:ietf:params:rtp-hdrext:sdes:mid"
#define AUDIO_LEVEL_EXTENSION "urn:ietf:params:rtp-hdrext:ssrc-audio-level"

typedef struct {
  const char *mid;
  const char *ufrag;
  const char *password;
  const char *fingerprint;
  const char *setup;
  const char *direction;
  uint8_t mid_id;
  const char *mid_direction;
  uint8_t audio_level_id;
  const char *audio_level_direction;
  bool mux;
  bool bundle_only;
  bool udp_candidate;
  bool extmap_allow_mixed;
} Attributes;

typedef struct {
  Attributes attributes;
  const char *codecs[128];
  uint8_t formats[128];
  size_t format_count;
  SfuTrackKind kind;
  uint16_t port;
} Media;

static bool
sDecimal(const char *text, uint32_t limit, uint32_t *number)
{
  if (!text || !*text) return false;
  uint32_t value = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9') return false;
    const unsigned digit = (unsigned)(*text - '0');
    if (value > limit / 10 || (value == limit / 10 && digit > limit % 10)) return false;
    value = 10 * value + digit;
  }
  *number = value;
  return true;
}

static bool
sToken(const char *text, size_t minimum, size_t capacity, const char *alphabet)
{
  if (!text) return false;
  const size_t length = strnlen(text, capacity);
  return length >= minimum && length < capacity && strspn(text, alphabet) == length;
}

static bool
sIce(const char *text, size_t minimum)
{
  return sToken(text, minimum, SFU_SDP_TOKEN_BYTES, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
}

static bool
sMid(const char *text)
{
  return sToken(text, 1, SFU_SDP_MID_BYTES, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$%&'*+-.^_`{|}~");
}

static int
sHex(char digit)
{
  if (digit >= '0' && digit <= '9') return digit - '0';
  if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
  if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
  return -1;
}

static bool
sFingerprint(const char *text, uint8_t out[32])
{
  if (!text || strlen(text) != SFU_SDP_FINGERPRINT_BYTES - 1) return false;
  for (size_t i = 0; i < 32; ++i) {
    const int high = sHex(text[3 * i]), low = sHex(text[3 * i + 1]);
    if (high < 0 || low < 0 || (i < 31 && text[3 * i + 2] != ':')) return false;
    out[i] = (uint8_t)((high << 4) | low);
  }
  return true;
}

static bool
sPut(const char **target, const char *value)
{
  if (*target || !*value) return false;
  *target = value;
  return true;
}

static bool
sMediaLine(char *line, Media *media)
{
  char *save;
  const char *kind = strtok_r(line, " \t", &save);
  const char *port = strtok_r(NULL, " \t", &save);
  const char *profile = strtok_r(NULL, " \t", &save);
  uint32_t value;
  if (!kind || !sDecimal(port, UINT16_MAX, &value) || !profile || strcmp(profile, "UDP/TLS/RTP/SAVPF") != 0) {
    return false;
  }
  media->port = (uint16_t)value;
  if (strcmp(kind, "audio") == 0) media->kind = SFU_TRACK_AUDIO;
  else if (strcmp(kind, "video") == 0) media->kind = SFU_TRACK_VIDEO;
  else return false;

  bool seen[128] = {0};
  for (const char *pt = strtok_r(NULL, " \t", &save); pt; pt = strtok_r(NULL, " \t", &save)) {
    if (!sDecimal(pt, 127, &value) || seen[value]) return false;
    seen[value] = true;
    media->formats[media->format_count++] = (uint8_t)value;
  }
  return media->format_count != 0;
}

static bool
sUdpCandidate(char *line)
{
  char *save;
  const char *foundation = strtok_r(line, " \t", &save);
  const char *component = strtok_r(NULL, " \t", &save);
  const char *protocol = strtok_r(NULL, " \t", &save);
  const char *priority = strtok_r(NULL, " \t", &save);
  const char *address = strtok_r(NULL, " \t", &save);
  const char *port = strtok_r(NULL, " \t", &save);
  const char *type = strtok_r(NULL, " \t", &save);
  const char *kind = strtok_r(NULL, " \t", &save);
  uint32_t value;
  if (!sToken(foundation, 1, 33, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") ||
      !sDecimal(component, 256, &value) || value != 1 || !protocol || strcasecmp(protocol, "udp") != 0 ||
      !sDecimal(priority, UINT32_MAX, &value) || !value || !address ||
      !sDecimal(port, UINT16_MAX, &value) || !value || !type || strcmp(type, "typ") != 0 || !kind) return false;
  struct in_addr ipv4;
  struct in6_addr ipv6;
  if (inet_pton(AF_INET, address, &ipv4) != 1 && inet_pton(AF_INET6, address, &ipv6) != 1 &&
      !sToken(address, 1, 254, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-")) return false;
  return strcmp(kind, "host") == 0 || strcmp(kind, "srflx") == 0 ||
         strcmp(kind, "prflx") == 0 || strcmp(kind, "relay") == 0;
}

static bool
sExtmap(char *line, Attributes *attrs)
{
  char *save;
  char *id = strtok_r(line, " \t", &save);
  const char *uri = strtok_r(NULL, " \t", &save);
  if (!id || !uri) return false;
  const bool is_mid = strcmp(uri, MID_EXTENSION) == 0;
  if (!is_mid && strcmp(uri, AUDIO_LEVEL_EXTENSION) != 0) return true;

  char *direction = strchr(id, '/');
  if (direction) {
    *direction++ = 0;
    if (strcmp(direction, "sendrecv") != 0 && strcmp(direction, "sendonly") != 0 &&
        strcmp(direction, "recvonly") != 0 && strcmp(direction, "inactive") != 0) return false;
  }
  uint32_t number;
  uint8_t *extension_id = is_mid ? &attrs->mid_id : &attrs->audio_level_id;
  if (*extension_id || !sDecimal(id, UINT8_MAX, &number) || !number) return false;
  *extension_id = (uint8_t)number;
  if (is_mid) attrs->mid_direction = direction;
  else attrs->audio_level_direction = direction;
  return true;
}

static bool
sAttribute(char *line, Attributes *attrs, Media *media)
{
  if (strcmp(line, "ice-lite") == 0) return false;
  if (strncmp(line, "ice-ufrag:", 10) == 0) return sPut(&attrs->ufrag, line + 10);
  if (strncmp(line, "ice-pwd:", 8) == 0) return sPut(&attrs->password, line + 8);
  if (strncmp(line, "fingerprint:", 12) == 0) {
    if (strncasecmp(line + 12, "sha-256 ", 8) != 0) return false;
    return sPut(&attrs->fingerprint, line + 20);
  }
  if (strncmp(line, "setup:", 6) == 0) return sPut(&attrs->setup, line + 6);
  if (strcmp(line, "sendonly") == 0 || strcmp(line, "recvonly") == 0 ||
      strcmp(line, "sendrecv") == 0 || strcmp(line, "inactive") == 0) return sPut(&attrs->direction, line);
  if (strcmp(line, "extmap-allow-mixed") == 0) {
    attrs->extmap_allow_mixed = true;
    return true;
  }
  if (strncmp(line, "extmap:", 7) == 0) return sExtmap(line + 7, attrs);
  if (strncmp(line, "mid:", 4) == 0) return media && sPut(&attrs->mid, line + 4);
  if (strcmp(line, "rtcp-mux") == 0) attrs->mux = true;
  if (strcmp(line, "bundle-only") == 0) attrs->bundle_only = true;
  if (strncmp(line, "candidate:", 10) == 0 && sUdpCandidate(line + 10)) attrs->udp_candidate = true;

  if (media && strncmp(line, "rtpmap:", 7) == 0) {
    char *save;
    const char *pt = strtok_r(line + 7, " \t", &save);
    const char *encoding = strtok_r(NULL, " \t", &save);
    uint32_t number;
    if (!sDecimal(pt, 127, &number) || !encoding || strtok_r(NULL, " \t", &save)) return false;
    return sPut(&media->codecs[number], encoding);
  }
  return true;
}

static const char *
sInherit(const char *media, const char *session)
{
  return media ? media : session;
}

static bool
sPassiveAnswer(const char *setup)
{
  return setup && (strcmp(setup, "actpass") == 0 || strcmp(setup, "active") == 0);
}

static bool
sAgree(const char *value, const char *expected)
{
  return !value || strcmp(value, expected) == 0;
}

static bool
sOfferSection(const Media *media, const Attributes *session, bool tag, SfuPeerConfigDirection expected,
              SfuPeerConfigMediaV1 *section)
{
  const Attributes *attrs = &media->attributes;
  if (!attrs->mux || (!media->port && (!attrs->bundle_only || tag))) return false;
  const char *direction = sInherit(attrs->direction, session->direction);
  const char *expected_direction = expected == SFU_PEER_CONFIG_PUBLISH ? "sendonly" : "recvonly";
  if (!direction || (strcmp(direction, expected_direction) && strcmp(direction, "inactive"))) return false;

  const bool inactive = strcmp(direction, "inactive") == 0;
  const Attributes *extmap = attrs->mid_id ? attrs : session;
  const uint8_t mid_id = extmap->mid_id;
  const char *ext_direction = extmap->mid_direction;
  if (!inactive && (!mid_id || (ext_direction && strcmp(ext_direction, expected_direction) != 0 &&
                               strcmp(ext_direction, "sendrecv") != 0))) return false;
  const Attributes *level = attrs->audio_level_id ? attrs : session;
  const char *level_direction = level->audio_level_direction;
  const bool accept_level = !inactive && expected == SFU_PEER_CONFIG_PUBLISH && media->kind == SFU_TRACK_AUDIO &&
                            (!level_direction || strcmp(level_direction, "sendonly") == 0 ||
                             strcmp(level_direction, "sendrecv") == 0);
  if (accept_level && level->audio_level_id && level->audio_level_id == mid_id) return false;
  const char *codec = media->kind == SFU_TRACK_AUDIO ? "opus/48000/2" : "VP8/90000";
  for (size_t i = 0; i < media->format_count; ++i) {
    const uint8_t pt = media->formats[i];
    if ((pt >= 64 && pt <= 95) || !media->codecs[pt] || strcasecmp(codec, media->codecs[pt]) != 0) continue;
    *section = (SfuPeerConfigMediaV1){
      .mid = attrs->mid, .kind = media->kind, .payload_type = pt, .mid_extension_id = mid_id,
      .audio_level_extension_id = accept_level ? level->audio_level_id : 0,
      .audio_level_allow_mixed = accept_level && level->audio_level_id &&
                                 (attrs->extmap_allow_mixed || session->extmap_allow_mixed),
      .clock_rate = media->kind == SFU_TRACK_AUDIO ? 48000 : 90000,
      .direction = inactive ? SFU_PEER_CONFIG_INACTIVE : expected
    };
    return true;
  }
  return false;
}

bool
SfuSdpParseOffer(char *sdp, SfuPeerConfigDirection direction, SfuSdpOffer *offer)
{
  Media media[SFU_CONFIG_MEDIA_MAX] = {0};
  Attributes session = {0};
  size_t count = 0;
  Media *current = NULL;
  char *bundle = NULL;
  bool version = false, origin = false, name = false, time = false;
  char *save;
  for (char *line = strtok_r(sdp, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
    size_t length = strlen(line);
    if (length && line[length - 1] == '\r') line[--length] = 0;
    if (length < 2 || line[1] != '=') return false;
    for (size_t i = 0; i < length; ++i) {
      const unsigned char byte = (unsigned char)line[i];
      if ((byte < 32 && byte != '\t') || byte == 127) return false;
    }
    if (line[0] == 'm') {
      if (count == SFU_CONFIG_MEDIA_MAX) return false;
      current = &media[count++];
      if (!sMediaLine(line + 2, current)) return false;
    } else if (line[0] == 'a') {
      if (strncmp(line + 2, "group:BUNDLE ", 13) == 0) {
        if (current || bundle) return false;
        bundle = line + 15;
      } else if (!sAttribute(line + 2, current ? &current->attributes : &session, current)) {
        return false;
      }
    } else if (!current) {
      if (strcmp(line, "v=0") == 0) version = true;
      if (line[0] == 'o' && length > 2) origin = true;
      if (line[0] == 's') name = true;
      if (line[0] == 't' && length > 2) time = true;
    }
  }
  if (!version || !origin || !name || !time || !count || !bundle) return false;
  for (size_t i = 0; i < count; ++i) {
    if (!sMid(media[i].attributes.mid)) return false;
    for (size_t j = 0; j < i; ++j) {
      if (strcmp(media[i].attributes.mid, media[j].attributes.mid) == 0) return false;
    }
  }
  bool seen[SFU_CONFIG_MEDIA_MAX] = {0};
  size_t bundled = 0;
  for (const char *mid = strtok_r(bundle, " \t", &save); mid; mid = strtok_r(NULL, " \t", &save)) {
    size_t i = 0;
    while (i < count && strcmp(media[i].attributes.mid, mid) != 0) ++i;
    if (i == count || seen[i]) return false;
    seen[i] = true;
    offer->bundled[bundled++] = i;
  }
  if (bundled != count) return false;

  const Attributes *tag = &media[offer->bundled[0]].attributes;
  const char *ufrag = sInherit(tag->ufrag, session.ufrag);
  const char *password = sInherit(tag->password, session.password);
  const char *fingerprint = sInherit(tag->fingerprint, session.fingerprint);
  const char *setup = sInherit(tag->setup, session.setup);
  if (!tag->udp_candidate || !sIce(ufrag, 4) || !sIce(password, 22) ||
      !sFingerprint(fingerprint, offer->fingerprint) || !sPassiveAnswer(setup)) return false;

  for (size_t i = 0; i < count; ++i) {
    const Attributes *attrs = &media[i].attributes;
    const char *media_fingerprint = sInherit(attrs->fingerprint, session.fingerprint);
    const char *media_setup = sInherit(attrs->setup, session.setup);
    if (!sAgree(sInherit(attrs->ufrag, session.ufrag), ufrag) ||
        !sAgree(sInherit(attrs->password, session.password), password) ||
        (media_fingerprint && strcasecmp(media_fingerprint, fingerprint) != 0) ||
        (media_setup && !sPassiveAnswer(media_setup)) ||
        !sOfferSection(&media[i], &session, i == offer->bundled[0], direction, &offer->media[i])) return false;
  }
  offer->count = count;
  offer->session_extmap_allow_mixed = session.extmap_allow_mixed;
  offer->remote_ufrag = ufrag;
  offer->remote_password = password;
  return true;
}

bool
SfuSdpReadLocalTransport(SfuServer *server, size_t shard, SfuSdpLocalTransport *transport)
{
  BufferDescriptor document = {0};
  BufferDescriptorInit(&document, 512);
  SfuServerDescribeTransport(server, shard, &document);
  json_object *json = document.data ? json_tokener_parse(document.data) : NULL;
  BufferDescriptorRelease(&document);
  const char *address = json_object_get_string(json_object_object_get(json, "address"));
  const char *fingerprint = json_object_get_string(json_object_object_get(json, "dtls_fingerprint"));
  const int64_t port = json_object_get_int64(json_object_object_get(json, "port"));
  struct in_addr ipv4;
  struct in6_addr ipv6;
  const bool v4 = address && inet_pton(AF_INET, address, &ipv4) == 1 && ipv4.s_addr != INADDR_ANY;
  const bool v6 = address && inet_pton(AF_INET6, address, &ipv6) == 1 && !IN6_IS_ADDR_UNSPECIFIED(&ipv6);
  uint8_t bytes[32];
  const bool valid = (v4 || v6) && port > 0 && port <= UINT16_MAX && sFingerprint(fingerprint, bytes);
  if (valid) {
    strcpy(transport->address, address);
    strcpy(transport->fingerprint, fingerprint);
    transport->port = (uint16_t)port;
    transport->ipv6 = v6;
  }
  json_object_put(json);
  return valid;
}

bool
SfuSdpReadAcknowledgedIce(const char *document, SfuSdpLocalTransport *transport)
{
  json_object *json = document ? json_tokener_parse(document) : NULL;
  const char *ufrag = json_object_get_string(json_object_object_get(json, "ice_ufrag"));
  const char *password = json_object_get_string(json_object_object_get(json, "ice_pwd"));
  const bool valid = sIce(ufrag, 4) && sIce(password, 22);
  if (valid) {
    strcpy(transport->ice_ufrag, ufrag);
    strcpy(transport->ice_password, password);
  }
  json_object_put(json);
  return valid;
}

static bool
sAppend(char **output, size_t *remaining, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(*output, *remaining, format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= *remaining) return false;
  *output += length;
  *remaining -= (size_t)length;
  return true;
}

bool
SfuSdpWriteAnswer(const SfuSdpOffer *offer, const SfuSdpLocalTransport *local,
                  const SfuPeerConfigMediaV1 *media, const char *stream_id,
                  uint64_t session_origin, uint64_t revision, char *answer, size_t capacity)
{
  const char *family = local->ipv6 ? "IP6" : "IP4";
  const char *unspecified = local->ipv6 ? "::" : "0.0.0.0";
  if (!sAppend(&answer, &capacity, "v=0\r\no=- %" PRIu64 " %" PRIu64 " IN %s %s\r\ns=-\r\nt=0 0\r\na=ice-lite\r\na=group:BUNDLE",
                session_origin, revision, family, unspecified)) return false;
  for (size_t i = 0; i < offer->count; ++i) {
    if (!sAppend(&answer, &capacity, " %s", offer->media[offer->bundled[i]].mid)) return false;
  }
  if (!sAppend(&answer, &capacity, "\r\na=ice-ufrag:%s\r\na=ice-pwd:%s\r\na=fingerprint:sha-256 %s\r\na=setup:passive\r\n",
               local->ice_ufrag, local->ice_password, local->fingerprint)) return false;

  if (!sAppend(&answer, &capacity, "a=msid-semantic: WMS *\r\n")) return false;
  for (size_t i = 0; i < offer->count; ++i) {
    if (offer->session_extmap_allow_mixed && offer->media[i].audio_level_allow_mixed) {
      if (!sAppend(&answer, &capacity, "a=extmap-allow-mixed\r\n")) return false;
      break;
    }
  }
  for (size_t i = 0; i < offer->count; ++i) {
    const SfuPeerConfigMediaV1 *section = &offer->media[i];
    const bool audio = section->kind == SFU_TRACK_AUDIO;
    const bool inactive = section->direction == SFU_PEER_CONFIG_INACTIVE;
    const bool sending = section->direction == SFU_PEER_CONFIG_SUBSCRIBE;
    const char *direction = sending ? "sendonly" : "recvonly";
    if (!sAppend(&answer, &capacity,
                 "m=%s 9 UDP/TLS/RTP/SAVPF %u\r\nc=IN %s %s\r\na=mid:%s\r\na=%s\r\na=rtcp-mux\r\na=rtpmap:%u %s\r\n",
                 audio ? "audio" : "video", section->payload_type, family, unspecified,
                 section->mid, inactive ? "inactive" : direction, section->payload_type,
                 audio ? "opus/48000/2" : "VP8/90000")) return false;
    /* Retain the extension assignment while inactive. The media direction
     * suppresses traffic and a later offer can use MID when reactivated. */
    if (section->mid_extension_id &&
        !sAppend(&answer, &capacity, "a=extmap:%u%s " MID_EXTENSION "\r\n", section->mid_extension_id,
                 inactive ? "" : sending ? "/sendonly" : "/recvonly")) return false;
    if (!offer->session_extmap_allow_mixed && section->audio_level_allow_mixed &&
        !sAppend(&answer, &capacity, "a=extmap-allow-mixed\r\n")) return false;
    if (section->audio_level_extension_id &&
        !sAppend(&answer, &capacity, "a=extmap:%u/recvonly " AUDIO_LEVEL_EXTENSION " vad=off\r\n",
                 section->audio_level_extension_id)) return false;
    if (i == offer->bundled[0] &&
        !sAppend(&answer, &capacity, "a=candidate:1 1 udp 2130706431 %s %u typ host\r\na=end-of-candidates\r\n",
                 local->address, local->port)) return false;
    if (sending) {
      const uint32_t ssrc = media[i].outbound_ssrc;
      if (!ssrc || !sAppend(&answer, &capacity,
                            "a=msid:%s track%" PRIu32 "\r\na=ssrc:%" PRIu32 " cname:%s\r\n"
                            "a=ssrc:%" PRIu32 " msid:%s track%" PRIu32 "\r\n",
                            stream_id, ssrc, ssrc, stream_id, ssrc, stream_id, ssrc)) return false;
    }
  }
  return true;
}
