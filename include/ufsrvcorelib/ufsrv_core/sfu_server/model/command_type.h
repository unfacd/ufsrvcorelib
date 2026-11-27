/**
 * @file command_type.h
 * @brief Signalling commands applied by a shard between receive turns.
 *
 * Copyright (C) 2015-2026  unfacd works
 *
 * This file is part of ufsrvcorelib source code.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef UFSRVCORELIB_COMMAND_TYPE_H
#define UFSRVCORELIB_COMMAND_TYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounds of the credential strings a command may carry; matches the peer
 * record's limits so registration can never truncate silently. */
#define SFU_COMMAND_UFRAG_MAX    64
#define SFU_COMMAND_PASSWORD_MAX 64
#define SFU_COMMAND_FINGERPRINT_SIZE 32

/** What a publisher track carries; the media path only needs it for logging
 * and keyframe policy, but the control plane must state it. */
typedef enum SfuTrackKind {
  SFU_TRACK_AUDIO = 0,
  SFU_TRACK_VIDEO = 1
} SfuTrackKind;

typedef enum SfuCommandType {
  SFU_COMMAND_REGISTER_PEER,   /* provision an ICE peer: ufrag + password */
  SFU_COMMAND_CREATE_ROOM,     /* create a room owned by this shard */
  SFU_COMMAND_JOIN_ROOM,       /* attach a registered peer to a room */
  SFU_COMMAND_ADD_TRACK,       /* publish one SSRC into a room */
  SFU_COMMAND_SUBSCRIBE,       /* map a publisher SSRC to our outbound SSRC */
  SFU_COMMAND_LEAVE_PEER,      /* release a peer and everything it owns */
  SFU_COMMAND_ADD_TRANSPORT,
  SFU_COMMAND_PRODUCE,
  SFU_COMMAND_CONSUME,
  SFU_COMMAND_LEAVE,
  SFU_COMMAND_DESCRIBE_SHARD,   /* state document; carried reply is required */
  SFU_COMMAND_RESTART_ICE,     /* prepare new credentials on the existing peer */
  SFU_COMMAND_APPLY_PEER_CONFIG,
  SFU_COMMAND_PEER_STATUS,     /* lookup stable peer key on the owning shard */
  SFU_COMMAND_APPLY_NEGOTIATED_CONFIG,
  SFU_COMMAND_DESCRIBE_ROOM
} SfuCommandType;

/** Signalling request applied by a shard between receive turns. */
typedef struct SfuCommand {
  SfuCommandType type;
  size_t         shard_index;
  uint64_t       peer_id;      /* signalling identity, opaque to the engine */
  char           ufrag[SFU_COMMAND_UFRAG_MAX];
  char           password[SFU_COMMAND_PASSWORD_MAX];
  uint8_t        ufrag_length;
  uint8_t        password_length;
  uint8_t        remote_fingerprint[SFU_COMMAND_FINGERPRINT_SIZE];
  bool           has_remote_fingerprint;
  uint64_t       room_id;
  uint32_t       ssrc;          /* publisher SSRC for SUBSCRIBE */
  uint32_t       out_ssrc;      /* SSRC the subscriber sees; 0 asks the shard to assign */
  uint32_t       clock_rate;    /* RTP units/s for ADD_TRACK; 0 uses the kind's default */
  uint16_t       seq_offset;    /* added to every forwarded sequence */
  uint8_t        track_kind;
  char           restart_ufrag[SFU_COMMAND_UFRAG_MAX + 1];
} SfuCommand;

#endif /* UFSRVCORELIB_COMMAND_TYPE_H */
