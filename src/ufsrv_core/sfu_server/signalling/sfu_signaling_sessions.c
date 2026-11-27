#include <ufsrv_core/sfu_server/signalling/sfu_signaling_sessions.h>
#include <ufsrv_core/sfu_server/signalling/sfu_signaling_grant.h>

#include "../sdp/sfu_sdp.h"
#include "../model/peer_type.h"

#include <uflib/buffer_descriptor/buffer_descriptor.h>

#include <openssl/rand.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { CONFERENCE_LIMIT = 128, SESSION_LIMIT = 256, RECORD_LIMIT = 1024, CLOSE_WINDOW_SECONDS = 600,
       SIGNALING_SHARD = 0 };

typedef struct {
  char id[SFU_GRANT_ID_MAX + 1];
  uint64_t room_id;
  size_t active;
} Conference;

typedef struct {
  char id[SFU_SIGNALING_SESSION_ID_LENGTH + 1];
  char mid[SFU_SDP_MID_BYTES];
  SfuTrackKind kind;
} Publication;

typedef struct {
  char receiving_mid[SFU_SDP_MID_BYTES];
  char publication_id[SFU_SIGNALING_SESSION_ID_LENGTH + 1];
  char peer[SFU_SIGNALING_SESSION_ID_LENGTH + 1];
  char mid[SFU_SDP_MID_BYTES];
  uint32_t ssrc;
} ResolvedSubscription;

typedef struct {
  char mid[SFU_SDP_MID_BYTES];
  SfuTrackKind kind;
} MediaIdentity;

typedef struct {
  uint64_t revision;
  char *offer;
  char *answer;
  char bundle_tag[SFU_SDP_MID_BYTES];
  char remote_ufrag[SFU_SDP_TOKEN_BYTES];
  char remote_password[SFU_SDP_TOKEN_BYTES];
  uint8_t fingerprint[32];
  size_t media_count;
  MediaIdentity media[SFU_CONFIG_MEDIA_MAX];
  size_t publication_count;
  size_t subscription_count;
  Publication publications[SFU_CONFIG_MEDIA_MAX];
  ResolvedSubscription subscriptions[SFU_PEER_SUBS_MAX];
} Revision;

typedef struct {
  size_t conference_index;
  SfuSignalingRole role;
  char conference_id[SFU_GRANT_ID_MAX + 1];
  size_t references;
  time_t closed_at;
  bool closed;
  bool closing;
  char subject[SFU_GRANT_ID_MAX + 1];
  char idempotency_key[SFU_GRANT_ID_MAX + 1];
  SfuSignalingSession session;
  pthread_mutex_t negotiation;
  uint64_t origin;
  Revision *committed;
  Revision *pending;
} Session;

struct SfuSignalingSessions {
  SfuGrantVerifier *verifier;
  pthread_mutex_t mutex;
  size_t active_count;
  size_t session_count;
  Conference conferences[CONFERENCE_LIMIT];
  Session entries[RECORD_LIMIT];
};

SfuSignalingSessions *
SfuSignalingSessionsCreate(const char *public_key, const char *issuer, const char *audience,
                           char *error, size_t error_size)
{
  SfuGrantVerifier *verifier = SfuGrantVerifierCreateFromPublicKey(public_key, issuer, audience, error, error_size);
  if (!verifier) return NULL;
  SfuSignalingSessions *sessions = calloc(1, sizeof *sessions);
  if (!sessions || pthread_mutex_init(&sessions->mutex, NULL) != 0) {
    if (error && error_size) snprintf(error, error_size, "could not initialize signaling sessions");
    free(sessions);
    SfuGrantVerifierDestroy(verifier);
    return NULL;
  }
  sessions->verifier = verifier;
  return sessions;
}

static void
sFreeRevision(Revision *revision)
{
  if (!revision) return;
  free(revision->offer);
  free(revision->answer);
  free(revision);
}

void
SfuSignalingSessionsDestroy(SfuSignalingSessions *sessions)
{
  if (!sessions) return;
  for (size_t i = 0; i < sessions->session_count; ++i) {
    Session *publisher = &sessions->entries[i];
    if (!publisher->session.session_id[0]) continue;
    pthread_mutex_destroy(&publisher->negotiation);
    sFreeRevision(publisher->committed);
    sFreeRevision(publisher->pending);
  }
  pthread_mutex_destroy(&sessions->mutex);
  SfuGrantVerifierDestroy(sessions->verifier);
  free(sessions);
}

static bool
sIdempotencyKeyValid(const char *key)
{
  if (!key) return false;
  const size_t len = strnlen(key, SFU_GRANT_ID_MAX + 1);
  return len > 0 && len <= SFU_GRANT_ID_MAX &&
         strspn(key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") == len;
}

static size_t
sConferenceIndex(const SfuSignalingSessions *sessions, const char *id)
{
  size_t index = 0;
  while (index < CONFERENCE_LIMIT && strcmp(sessions->conferences[index].id, id) != 0) ++index;
  return index;
}

static uint32_t
sMediaScope(SfuSignalingRole role)
{
  return role == SFU_SIGNALING_PUBLISHER ? SFU_GRANT_MEDIA_PUBLISH : SFU_GRANT_MEDIA_SUBSCRIBE;
}

static bool
sRandomId(char out[SFU_SIGNALING_SESSION_ID_LENGTH + 1])
{
  unsigned char bytes[SFU_SIGNALING_SESSION_ID_LENGTH / 2];
  if (RAND_bytes(bytes, sizeof bytes) != 1) return false;
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof bytes; ++i) {
    out[2 * i] = hex[bytes[i] >> 4];
    out[2 * i + 1] = hex[bytes[i] & 15];
  }
  out[SFU_SIGNALING_SESSION_ID_LENGTH] = 0;
  return true;
}

static bool
sNow(time_t *seconds)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
  *seconds = now.tv_sec;
  return true;
}

/* Registry lock protects record identity and references. Negotiation locks
 * can be taken only after dropping the registry lock. */
static void
sReapExpired(SfuSignalingSessions *sessions, time_t now)
{
  for (size_t i = 0; i < sessions->session_count; ++i) {
    Session *session = &sessions->entries[i];
    if (session->closed && !session->references && now - session->closed_at >= CLOSE_WINDOW_SECONDS) {
      pthread_mutex_destroy(&session->negotiation);
      memset(session, 0, sizeof *session);
    }
  }
  while (sessions->session_count && !sessions->entries[sessions->session_count - 1].session.session_id[0]) {
    --sessions->session_count;
  }
}

static size_t
sUnusedConference(const SfuSignalingSessions *sessions)
{
  size_t index = 0;
  while (index < CONFERENCE_LIMIT && sessions->conferences[index].active) ++index;
  return index;
}

static SfuSignalingSessionStatus
sOpenSession(SfuSignalingSessions *sessions, const SfuSignalingGrant *grant, SfuSignalingRole role,
              const char *key, SfuSignalingSession *result, time_t now)
{
  size_t conference_index = sConferenceIndex(sessions, grant->conference_id);
  const bool create_conference = conference_index == CONFERENCE_LIMIT;
  if (!(grant->scopes & (SFU_GRANT_CONFERENCE_START | SFU_GRANT_CONFERENCE_JOIN))) {
    return SFU_SIGNALING_SESSION_FORBIDDEN;
  }

  size_t slot = sessions->session_count;
  for (size_t i = 0; i < sessions->session_count; ++i) {
    const Session *publisher = &sessions->entries[i];
    if (!publisher->session.session_id[0]) {
      if (slot == sessions->session_count) slot = i;
      continue;
    }
    if (publisher->closed && now - publisher->closed_at >= CLOSE_WINDOW_SECONDS) continue;
    if (strcmp(publisher->conference_id, grant->conference_id) == 0 &&
        strcmp(publisher->subject, grant->subject) == 0 && strcmp(publisher->idempotency_key, key) == 0) {
      if (publisher->role != role) return SFU_SIGNALING_SESSION_CONFLICT;
      *result = publisher->session;
      return SFU_SIGNALING_SESSION_CREATED;
    }
  }

  const uint32_t conference_scope = create_conference ? SFU_GRANT_CONFERENCE_START : SFU_GRANT_CONFERENCE_JOIN;
  if (!(grant->scopes & conference_scope)) return SFU_SIGNALING_SESSION_FORBIDDEN;
  if (create_conference) conference_index = sUnusedConference(sessions);
  if (sessions->active_count == SESSION_LIMIT || slot == RECORD_LIMIT ||
      conference_index == CONFERENCE_LIMIT) return SFU_SIGNALING_SESSION_UNAVAILABLE;

  SfuSignalingSession session = {0};
  if (!sRandomId(session.session_id)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  for (size_t i = 0; i < sessions->session_count; ++i) {
    if (strcmp(session.session_id, sessions->entries[i].session.session_id) == 0) {
      return SFU_SIGNALING_SESSION_UNAVAILABLE;
    }
  }

  if (create_conference) {
    if (RAND_bytes((unsigned char *)&session.room_id, sizeof session.room_id) != 1 || !session.room_id) {
      return SFU_SIGNALING_SESSION_UNAVAILABLE;
    }
    for (size_t i = 0; i < CONFERENCE_LIMIT; ++i) {
      if (sessions->conferences[i].room_id == session.room_id) return SFU_SIGNALING_SESSION_UNAVAILABLE;
    }
  } else {
    session.room_id = sessions->conferences[conference_index].room_id;
  }

  Session *publisher = &sessions->entries[slot];
  if (pthread_mutex_init(&publisher->negotiation, NULL) != 0) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  Conference *conference = &sessions->conferences[conference_index];
  if (create_conference) {
    strcpy(conference->id, grant->conference_id);
    conference->room_id = session.room_id;
  }
  ++conference->active;
  ++sessions->active_count;
  if (slot == sessions->session_count) ++sessions->session_count;
  publisher->conference_index = conference_index;
  strcpy(publisher->conference_id, grant->conference_id);
  publisher->role = role;
  strcpy(publisher->subject, grant->subject);
  strcpy(publisher->idempotency_key, key);
  publisher->session = session;
  *result = session;
  return SFU_SIGNALING_SESSION_CREATED;
}

SfuSignalingSessionStatus
SfuSignalingOpenSession(SfuSignalingSessions *sessions, const char *authorization, const char *conference_id,
                        SfuSignalingRole role, const char *idempotency_key, SfuSignalingSession *result)
{
  if (!conference_id || !result || (role != SFU_SIGNALING_PUBLISHER && role != SFU_SIGNALING_SUBSCRIBER)) {
    return SFU_SIGNALING_SESSION_INVALID;
  }
  if (!sessions) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  SfuSignalingGrant grant;
  if (!SfuGrantVerifyBearer(sessions->verifier, authorization, &grant)) return SFU_SIGNALING_SESSION_UNAUTHORIZED;
  if (strcmp(grant.conference_id, conference_id) != 0) return SFU_SIGNALING_SESSION_NOT_FOUND;
  if (!(grant.scopes & sMediaScope(role))) return SFU_SIGNALING_SESSION_FORBIDDEN;
  if (!sIdempotencyKeyValid(idempotency_key)) return SFU_SIGNALING_SESSION_INVALID;

  time_t now;
  if (!sNow(&now)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  pthread_mutex_lock(&sessions->mutex);
  sReapExpired(sessions, now);
  const SfuSignalingSessionStatus status = sOpenSession(sessions, &grant, role, idempotency_key, result, now);
  pthread_mutex_unlock(&sessions->mutex);
  return status;
}

SfuSignalingSessionStatus
SfuSignalingOpenPublisher(SfuSignalingSessions *sessions, const char *authorization, const char *conference_id,
                          const char *idempotency_key, SfuSignalingSession *result)
{
  return SfuSignalingOpenSession(sessions, authorization, conference_id, SFU_SIGNALING_PUBLISHER,
                                 idempotency_key, result);
}

static Session *
sOwnedSession(SfuSignalingSessions *sessions, const char *id, const SfuSignalingGrant *grant, time_t now)
{
  Session *result = NULL;
  pthread_mutex_lock(&sessions->mutex);
  sReapExpired(sessions, now);
  for (size_t i = 0; i < sessions->session_count; ++i) {
    Session *publisher = &sessions->entries[i];
    if (strcmp(publisher->session.session_id, id) == 0 && strcmp(publisher->subject, grant->subject) == 0 &&
        strcmp(publisher->conference_id, grant->conference_id) == 0 &&
        (!publisher->closed || now - publisher->closed_at < CLOSE_WINDOW_SECONDS)) {
      ++publisher->references;
      result = publisher;
      break;
    }
  }
  pthread_mutex_unlock(&sessions->mutex);
  return result;
}

static void
sDropReference(SfuSignalingSessions *sessions, Session *session)
{
  pthread_mutex_lock(&sessions->mutex);
  --session->references;
  pthread_mutex_unlock(&sessions->mutex);
}

static Session *
sPublisherAt(SfuSignalingSessions *sessions, size_t index, uint64_t room_id)
{
  pthread_mutex_lock(&sessions->mutex);
  Session *publisher = &sessions->entries[index];
  if (!publisher->session.session_id[0] || publisher->closed || publisher->role != SFU_SIGNALING_PUBLISHER ||
      publisher->session.room_id != room_id) {
    publisher = NULL;
  } else {
    ++publisher->references;
  }
  pthread_mutex_unlock(&sessions->mutex);
  return publisher;
}

static size_t
sSessionCount(SfuSignalingSessions *sessions)
{
  pthread_mutex_lock(&sessions->mutex);
  const size_t count = sessions->session_count;
  pthread_mutex_unlock(&sessions->mutex);
  return count;
}

static SfuSignalingSessionStatus
sResolvePublication(SfuSignalingSessions *sessions, SfuServer *server, const Session *subscriber,
                    const char *id, SfuPeerConfigMediaV1 *section, ResolvedSubscription *binding)
{
  const size_t count = sSessionCount(sessions);
  for (size_t i = 0; i < count; ++i) {
    Session *source = sPublisherAt(sessions, i, subscriber->session.room_id);
    if (!source) continue;
    pthread_mutex_lock(&source->negotiation);
    const Publication *found = NULL;
    if (!source->closing && source->committed) {
      for (size_t j = 0; j < source->committed->publication_count; ++j) {
        if (strcmp(source->committed->publications[j].id, id) == 0) {
          found = &source->committed->publications[j];
          break;
        }
      }
    }
    if (found) {
      bool present;
      const bool available = SfuServerPeerLifecycle(server, SIGNALING_SHARD, source->session.session_id, &present);
      if (available && present && found->kind == section->kind) {
        strcpy(binding->peer, source->session.session_id);
        strcpy(binding->mid, found->mid);
        pthread_mutex_unlock(&source->negotiation);
        sDropReference(sessions, source);
        return SFU_SIGNALING_SESSION_OFFER_APPLIED;
      }
      const SfuSignalingSessionStatus status = !available ? SFU_SIGNALING_SESSION_UNAVAILABLE :
                                               !present ? SFU_SIGNALING_SESSION_NOT_FOUND : SFU_SIGNALING_SESSION_INVALID;
      pthread_mutex_unlock(&source->negotiation);
      sDropReference(sessions, source);
      return status;
    }
    pthread_mutex_unlock(&source->negotiation);
    sDropReference(sessions, source);
  }
  return SFU_SIGNALING_SESSION_NOT_FOUND;
}

static const char *
sPublicationForMid(const SfuSignalingSubscription *subscriptions, size_t count, const char *mid)
{
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(subscriptions[i].mid, mid) == 0) return subscriptions[i].publication_id;
  }
  return NULL;
}

static bool
sSubscriptionsValid(const SfuSignalingSubscription *subscriptions, size_t count)
{
  if (count > SFU_PEER_SUBS_MAX || (count && !subscriptions)) return false;
  for (size_t i = 0; i < count; ++i) {
    const char *mid = subscriptions[i].mid;
    const char *id = subscriptions[i].publication_id;
    if (!mid || !*mid || strnlen(mid, SFU_SDP_MID_BYTES) == SFU_SDP_MID_BYTES || !id ||
        strspn(id, "0123456789abcdef") != SFU_SIGNALING_SESSION_ID_LENGTH ||
        id[SFU_SIGNALING_SESSION_ID_LENGTH] != '\0') return false;
    for (size_t j = 0; j < i; ++j) {
      if (strcmp(mid, subscriptions[j].mid) == 0 || strcmp(id, subscriptions[j].publication_id) == 0) return false;
    }
  }
  return true;
}

static bool
sContinuesDescription(const Revision *previous, const SfuSdpOffer *offer)
{
  if (!previous) return true;
  if (offer->count < previous->media_count ||
      strcmp(previous->bundle_tag, offer->media[offer->bundled[0]].mid) != 0 ||
      memcmp(previous->fingerprint, offer->fingerprint, sizeof previous->fingerprint) != 0) return false;
  const bool ufrag_changed = strcmp(previous->remote_ufrag, offer->remote_ufrag) != 0;
  const bool password_changed = strcmp(previous->remote_password, offer->remote_password) != 0;
  if (ufrag_changed != password_changed) return false;
  for (size_t i = 0; i < previous->media_count; ++i) {
    if (previous->media[i].kind != offer->media[i].kind ||
        strcmp(previous->media[i].mid, offer->media[i].mid) != 0) return false;
  }
  return true;
}

static bool
sPlanPublication(const Revision *previous, const SfuPeerConfigMediaV1 *media, Publication *publication)
{
  if (previous) {
    for (size_t i = 0; i < previous->publication_count; ++i) {
      if (strcmp(previous->publications[i].mid, media->mid) == 0) {
        *publication = previous->publications[i];
        return true;
      }
    }
  }
  strcpy(publication->mid, media->mid);
  publication->kind = media->kind;
  return sRandomId(publication->id);
}

static SfuSignalingSessionStatus
sPlanSubscription(SfuSignalingSessions *sessions, SfuServer *server, const Session *session,
                  const char *id, SfuPeerConfigMediaV1 *media, ResolvedSubscription *binding)
{
  const SfuSignalingSessionStatus status = sResolvePublication(sessions, server, session, id, media, binding);
  if (status != SFU_SIGNALING_SESSION_OFFER_APPLIED) return status;
  strcpy(binding->receiving_mid, media->mid);
  strcpy(binding->publication_id, id);
  if (session->committed) {
    for (size_t i = 0; i < session->committed->subscription_count; ++i) {
      const ResolvedSubscription *retained = &session->committed->subscriptions[i];
      if (strcmp(retained->receiving_mid, media->mid) == 0 && strcmp(retained->publication_id, id) == 0) {
        binding->ssrc = retained->ssrc;
        return SFU_SIGNALING_SESSION_OFFER_APPLIED;
      }
    }
  }
  if (RAND_bytes((unsigned char *)&binding->ssrc, sizeof binding->ssrc) != 1 || !binding->ssrc) {
    return SFU_SIGNALING_SESSION_UNAVAILABLE;
  }
  return SFU_SIGNALING_SESSION_OFFER_APPLIED;
}

static SfuSignalingSessionStatus
sPlanOffer(SfuSignalingSessions *sessions, SfuServer *server, const Session *session, SfuSdpOffer *offer,
           const SfuSignalingSubscription *subscriptions, size_t count, Revision *revision)
{
  if (!sContinuesDescription(session->committed, offer)) return SFU_SIGNALING_SESSION_INVALID;
  strcpy(revision->bundle_tag, offer->media[offer->bundled[0]].mid);
  strcpy(revision->remote_ufrag, offer->remote_ufrag);
  strcpy(revision->remote_password, offer->remote_password);
  memcpy(revision->fingerprint, offer->fingerprint, sizeof revision->fingerprint);
  revision->media_count = offer->count;

  for (size_t i = 0; i < offer->count; ++i) {
    SfuPeerConfigMediaV1 *media = &offer->media[i];
    strcpy(revision->media[i].mid, media->mid);
    revision->media[i].kind = media->kind;
    if (media->direction == SFU_PEER_CONFIG_INACTIVE) continue;
    if (media->direction == SFU_PEER_CONFIG_PUBLISH) {
      Publication *publication = &revision->publications[revision->publication_count++];
      if (!sPlanPublication(session->committed, media, publication)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
    } else {
      const char *id = sPublicationForMid(subscriptions, count, media->mid);
      if (!id || revision->subscription_count == SFU_PEER_SUBS_MAX) return SFU_SIGNALING_SESSION_INVALID;
      ResolvedSubscription *binding = &revision->subscriptions[revision->subscription_count++];
      const SfuSignalingSessionStatus status = sPlanSubscription(sessions, server, session, id, media, binding);
      if (status != SFU_SIGNALING_SESSION_OFFER_APPLIED) return status;
    }
  }
  return revision->subscription_count == count ? SFU_SIGNALING_SESSION_OFFER_APPLIED : SFU_SIGNALING_SESSION_INVALID;
}

static bool
sSameOffer(const Revision *revision, const char *sdp, const SfuSignalingSubscription *subscriptions, size_t count)
{
  if (strcmp(revision->offer, sdp) != 0 || revision->subscription_count != count) return false;
  for (size_t i = 0; i < count; ++i) {
    const ResolvedSubscription *binding = &revision->subscriptions[i];
    const char *id = sPublicationForMid(subscriptions, count, binding->receiving_mid);
    if (!id || strcmp(id, binding->publication_id) != 0) return false;
  }
  return true;
}

SfuSignalingSessionStatus
SfuSignalingListPublications(SfuSignalingSessions *sessions, SfuServer *server, const char *authorization,
                             const char *conference_id, SfuSignalingPublication **publications, size_t *count)
{
  if (!publications || !count || !conference_id) return SFU_SIGNALING_SESSION_INVALID;
  *publications = NULL;
  *count = 0;
  if (!sessions || !server) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  SfuSignalingGrant grant;
  if (!SfuGrantVerifyBearer(sessions->verifier, authorization, &grant)) return SFU_SIGNALING_SESSION_UNAUTHORIZED;
  if (strcmp(grant.conference_id, conference_id) != 0) return SFU_SIGNALING_SESSION_NOT_FOUND;
  if (!(grant.scopes & SFU_GRANT_MEDIA_SUBSCRIBE)) return SFU_SIGNALING_SESSION_FORBIDDEN;

  pthread_mutex_lock(&sessions->mutex);
  const size_t conference = sConferenceIndex(sessions, grant.conference_id);
  const uint64_t room_id = conference < CONFERENCE_LIMIT ? sessions->conferences[conference].room_id : 0;
  const size_t session_count = sessions->session_count;
  pthread_mutex_unlock(&sessions->mutex);
  if (!room_id) return SFU_SIGNALING_SESSION_NOT_FOUND;

  SfuSignalingPublication *items = NULL;
  size_t used = 0;
  for (size_t i = 0; i < session_count; ++i) {
    Session *publisher = sPublisherAt(sessions, i, room_id);
    if (!publisher) continue;
    pthread_mutex_lock(&publisher->negotiation);
    if (!publisher->closing && publisher->committed && publisher->committed->publication_count) {
      bool present;
      if (!SfuServerPeerLifecycle(server, SIGNALING_SHARD, publisher->session.session_id, &present)) {
        pthread_mutex_unlock(&publisher->negotiation);
        sDropReference(sessions, publisher);
        free(items);
        return SFU_SIGNALING_SESSION_UNAVAILABLE;
      }
      if (present) {
        SfuSignalingPublication *grown = realloc(items, (used + publisher->committed->publication_count) * sizeof *items);
        if (!grown) {
          pthread_mutex_unlock(&publisher->negotiation);
          sDropReference(sessions, publisher);
          free(items);
          return SFU_SIGNALING_SESSION_UNAVAILABLE;
        }
        items = grown;
        for (size_t j = 0; j < publisher->committed->publication_count; ++j) {
          const Publication *source = &publisher->committed->publications[j];
          SfuSignalingPublication *item = &items[used++];
          strcpy(item->publication_id, source->id);
          strcpy(item->publisher_id, publisher->subject);
          item->kind = source->kind;
        }
      }
    }
    pthread_mutex_unlock(&publisher->negotiation);
    sDropReference(sessions, publisher);
  }
  *publications = items;
  *count = used;
  return SFU_SIGNALING_PUBLICATIONS_LISTED;
}

static SfuSignalingSessionStatus
sCopyAnswer(const Revision *revision, char **answer)
{
  *answer = strdup(revision->answer);
  return *answer ? SFU_SIGNALING_SESSION_OFFER_APPLIED : SFU_SIGNALING_SESSION_UNAVAILABLE;
}

static SfuSignalingSessionStatus
sApplyPending(Session *session, SfuServer *server, const SfuSdpOffer *offer,
              SfuSdpLocalTransport *local, BufferDescriptor *ice, char **answer)
{
  Revision *revision = session->pending;
  SfuPeerConfigMediaV1 media[SFU_CONFIG_MEDIA_MAX];
  size_t subscription = 0;
  for (size_t i = 0; i < offer->count; ++i) {
    media[i] = offer->media[i];
    if (media[i].direction == SFU_PEER_CONFIG_INACTIVE) {
      media[i].clock_rate = 0;
      media[i].payload_type = 0;
      media[i].mid_extension_id = 0;
    } else if (media[i].direction == SFU_PEER_CONFIG_SUBSCRIBE) {
      const ResolvedSubscription *binding = &revision->subscriptions[subscription++];
      media[i].publisher_ufrag = binding->peer;
      media[i].publisher_mid = binding->mid;
      media[i].outbound_ssrc = binding->ssrc;
    }
  }
  SfuPeerConfigV1 config = {
    .revision = revision->revision, .room_id = session->session.room_id, .ufrag = session->session.session_id,
    .remote_ufrag = offer->remote_ufrag, .remote_password = offer->remote_password,
    .media_count = offer->count, .media = media
  };
  memcpy(config.fingerprint, offer->fingerprint, sizeof config.fingerprint);
  const SfuPeerConfigResult result = SfuServerApplyNegotiatedPeerConfigV1(server, SIGNALING_SHARD, &config, ice);
  if (result == SFU_PEER_CONFIG_UNAVAILABLE) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  if (result != SFU_PEER_CONFIG_APPLIED) {
    session->pending = NULL;
    sFreeRevision(revision);
    return result == SFU_PEER_CONFIG_CONFLICT ? SFU_SIGNALING_SESSION_CONFLICT : SFU_SIGNALING_SESSION_INVALID;
  }
  if (!SfuSdpReadAcknowledgedIce(ice->data, local) ||
      !SfuSdpWriteAnswer(offer, local, media, session->session.session_id, session->origin, revision->revision,
                        revision->answer, SFU_SDP_ANSWER_BYTES)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  sFreeRevision(session->committed);
  session->committed = revision;
  session->pending = NULL;
  return sCopyAnswer(revision, answer);
}

static SfuSignalingSessionStatus
sNegotiateOffer(SfuSignalingSessions *sessions, Session *session, SfuServer *server, uint64_t revision,
                const char *sdp, const SfuSignalingSubscription *subscriptions, size_t count, char **answer)
{
  const Revision *current = session->committed;
  const Revision *pending = session->pending;
  if (pending) {
    if (revision != pending->revision || !sSameOffer(pending, sdp, subscriptions, count)) {
      return SFU_SIGNALING_SESSION_CONFLICT;
    }
  } else if (current && revision == current->revision && sSameOffer(current, sdp, subscriptions, count)) {
    return sCopyAnswer(current, answer);
  } else if ((!current && revision != 1) ||
             (current && (current->revision == UINT64_MAX || revision != current->revision + 1))) {
    return SFU_SIGNALING_SESSION_CONFLICT;
  }

  char *parsed = strdup(sdp);
  if (!parsed) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  SfuSdpOffer offer = {0};
  const SfuPeerConfigDirection direction = session->role == SFU_SIGNALING_PUBLISHER
                                            ? SFU_PEER_CONFIG_PUBLISH : SFU_PEER_CONFIG_SUBSCRIBE;
  if (!SfuSdpParseOffer(parsed, direction, &offer)) {
    free(parsed);
    return SFU_SIGNALING_SESSION_INVALID;
  }

  SfuSdpLocalTransport local = {0};
  BufferDescriptor ice = {0};
  SfuSignalingSessionStatus status = SFU_SIGNALING_SESSION_UNAVAILABLE;
  if (!SfuSdpReadLocalTransport(server, SIGNALING_SHARD, &local)) goto done;
  BufferDescriptorInit(&ice, 256);
  if (!ice.data) goto done;

  if (!pending) {
    Revision *next = calloc(1, sizeof *next);
    if (!next) goto done;
    next->revision = revision;
    status = sPlanOffer(sessions, server, session, &offer, subscriptions, count, next);
    if (status != SFU_SIGNALING_SESSION_OFFER_APPLIED) {
      sFreeRevision(next);
      goto done;
    }
    status = SFU_SIGNALING_SESSION_UNAVAILABLE;
    next->offer = strdup(sdp);
    next->answer = malloc(SFU_SDP_ANSWER_BYTES);
    if (!next->offer || !next->answer) {
      sFreeRevision(next);
      goto done;
    }
    if (!current) {
      uint64_t origin;
      if (RAND_bytes((unsigned char *)&origin, sizeof origin) != 1) {
        sFreeRevision(next);
        goto done;
      }
      session->origin = origin & INT64_MAX;
    }
    session->pending = next;
  }
  status = sApplyPending(session, server, &offer, &local, &ice, answer);

done:
  BufferDescriptorRelease(&ice);
  free(parsed);
  return status;
}

static SfuSignalingSessionStatus
sApplyOffer(SfuSignalingSessions *sessions, SfuServer *server, const char *authorization,
             const char *session_id, uint64_t revision, const char *sdp,
             const SfuSignalingSubscription *subscriptions, size_t count, bool publisher_only, char **answer)
{
  if (!answer) return SFU_SIGNALING_SESSION_INVALID;
  *answer = NULL;
  if (!session_id || !sdp || !*sdp || strnlen(sdp, SFU_SIGNALING_SDP_LIMIT + 1) > SFU_SIGNALING_SDP_LIMIT) {
    return SFU_SIGNALING_SESSION_INVALID;
  }
  if (!sessions) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  SfuSignalingGrant grant;
  if (!SfuGrantVerifyBearer(sessions->verifier, authorization, &grant)) return SFU_SIGNALING_SESSION_UNAUTHORIZED;
  time_t now;
  if (!sNow(&now)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  Session *session = sOwnedSession(sessions, session_id, &grant, now);
  if (!session) return SFU_SIGNALING_SESSION_NOT_FOUND;

  pthread_mutex_lock(&session->negotiation);
  SfuSignalingSessionStatus status;
  if (session->closed) {
    status = SFU_SIGNALING_SESSION_NOT_FOUND;
  } else if (session->closing) {
    status = SFU_SIGNALING_SESSION_UNAVAILABLE;
  } else if (!(grant.scopes & sMediaScope(session->role))) {
    status = SFU_SIGNALING_SESSION_FORBIDDEN;
  } else if ((session->role == SFU_SIGNALING_PUBLISHER && count) ||
             (publisher_only && session->role != SFU_SIGNALING_PUBLISHER) ||
             !sSubscriptionsValid(subscriptions, count)) {
    status = SFU_SIGNALING_SESSION_INVALID;
  } else {
    status = sNegotiateOffer(sessions, session, server, revision, sdp, subscriptions, count, answer);
  }
  pthread_mutex_unlock(&session->negotiation);
  sDropReference(sessions, session);
  return status;
}

SfuSignalingSessionStatus
SfuSignalingApplyOffer(SfuSignalingSessions *sessions, SfuServer *server, const char *authorization,
                       const char *session_id, uint64_t revision, const char *sdp,
                       const SfuSignalingSubscription *subscriptions, size_t count, char **answer)
{
  return sApplyOffer(sessions, server, authorization, session_id, revision, sdp, subscriptions, count, false, answer);
}

SfuSignalingSessionStatus
SfuSignalingApplyPublisherOffer(SfuSignalingSessions *sessions, SfuServer *server, const char *authorization,
                                const char *session_id, uint64_t revision, const char *sdp, char **answer)
{
  return sApplyOffer(sessions, server, authorization, session_id, revision, sdp, NULL, 0, true, answer);
}

SfuSignalingSessionStatus
SfuSignalingCloseSession(SfuSignalingSessions *sessions, SfuServer *server, const char *authorization,
                          const char *session_id)
{
  if (!session_id) return SFU_SIGNALING_SESSION_INVALID;
  if (!sessions) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  SfuSignalingGrant grant;
  if (!SfuGrantVerifyBearer(sessions->verifier, authorization, &grant)) return SFU_SIGNALING_SESSION_UNAUTHORIZED;
  time_t now;
  if (!sNow(&now)) return SFU_SIGNALING_SESSION_UNAVAILABLE;
  Session *session = sOwnedSession(sessions, session_id, &grant, now);
  if (!session) return SFU_SIGNALING_SESSION_NOT_FOUND;

  pthread_mutex_lock(&session->negotiation);
  SfuSignalingSessionStatus status = SFU_SIGNALING_SESSION_CLOSED;
  if (!session->closed) {
    session->closing = true;
    if (SfuServerPeerLifecycle(server, SIGNALING_SHARD, session->session.session_id, NULL)) {
      sFreeRevision(session->committed);
      sFreeRevision(session->pending);
      session->committed = NULL;
      session->pending = NULL;
      (void)sNow(&now);
      pthread_mutex_lock(&sessions->mutex);
      session->closed_at = now;
      session->closed = true;
      --sessions->active_count;
      Conference *conference = &sessions->conferences[session->conference_index];
      if (--conference->active == 0) *conference = (Conference){0};
      pthread_mutex_unlock(&sessions->mutex);
    } else {
      /* The command can still run after a timeout. Keep offers blocked
       * while the client retries the idempotent release. */
      status = SFU_SIGNALING_SESSION_UNAVAILABLE;
    }
  }
  pthread_mutex_unlock(&session->negotiation);
  sDropReference(sessions, session);
  return status;
}
