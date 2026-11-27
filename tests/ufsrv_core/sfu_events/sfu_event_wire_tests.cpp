#include <gtest/gtest.h>
#include <ufsrvcorelib/ufsrv_core/sfu_server/events/sfu_event_wire_header_type.h>

TEST(SfuEventWire, GoldenFramesAndRejectedInputs)
{
  uint8_t frame[SFU_EVT_FRAME_MAX];
  const uint8_t disconnected[] = {
    0x53, 0x46, 1, 2, 0, 3, 2, 1, 1, 2, 3, 4,
    1, 2, 3, 4, 5, 6, 7, 8, 1, 0, 0, 0, '4', '2', 'p'
  };
  const uint8_t cleared[] = {
    0x53, 0x46, 1, 3, 0, 2, 2, 0, 0, 0, 0, 5,
    0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, '4', '2'
  };
  ASSERT_TRUE(SfuEventWireEncode(frame, sizeof(frame), SFU_EVT_PEER_DISCONNECTED, SFU_DISC_ICE_TIMEOUT,
                            0x01020304, UINT64_C(0x0102030405060708), "42", 2, "p", 1) == sizeof(disconnected));
  ASSERT_TRUE(memcmp(frame, disconnected, sizeof(disconnected)) == 0);
  ASSERT_TRUE(SfuEventWireEncode(frame, sizeof(frame), SFU_EVT_DOMINANT_SPEAKER, 0, 5, 6,
                            "42", 2, NULL, 0) == sizeof(cleared));
  ASSERT_TRUE(memcmp(frame, cleared, sizeof(cleared)) == 0);
  ASSERT_TRUE(!SfuEventWireEncode(frame, sizeof(cleared) - 1, SFU_EVT_DOMINANT_SPEAKER, 0, 5, 6, "42", 2, NULL, 0));
  ASSERT_TRUE(!SfuEventWireEncode(frame, sizeof(frame), SFU_EVT_PEER_CONNECTED, 0, 1, 1, "42", 2, NULL, 0));
  ASSERT_TRUE(!SfuEventWireEncode(frame, sizeof(frame), SFU_EVT_DOMINANT_SPEAKER, 128, 1, 1, "42", 2, "p", 1));
  ASSERT_TRUE(!SfuEventWireEncode(frame, sizeof(frame), SFU_EVT_DOMINANT_SPEAKER, 0, 1, 1, "42", 65, "p", 1));
  ASSERT_TRUE(memcmp(frame, cleared, sizeof(cleared)) == 0);
}

TEST(SfuEventWire, ExtendedGoldenFrames)
{
  uint8_t frame[SFU_EVT_FRAME_MAX];
  SfuEvent event{};
  event.seq = 7;
  event.timestamp_ms = 8;
  event.room_id_len = 2;
  memcpy(event.room_id, "42", 2);
  event.peer_id_len = 1;
  event.peer_id[0] = 'p';

  uint8_t publication[] = {
    0x53, 0x46, 1, 4, 0, 7, 2, 1, 0, 0, 0, 7,
    0, 0, 0, 0, 0, 0, 0, 8, 1, 0, 0, 0, '4', '2', 'p', 3, 'p', 'u', 'b'
  };
  event.type = SFU_EVT_PUBLICATION_ADDED;
  event.detail = SFU_EVT_MEDIA_AUDIO;
  event.data.publication.id_len = 3;
  memcpy(event.data.publication.id, "pub", 3);
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(publication));
  EXPECT_EQ(memcmp(frame, publication, sizeof(publication)), 0);

  publication[3] = 5;
  publication[20] = 2;
  event.type = SFU_EVT_PUBLICATION_REMOVED;
  event.detail = SFU_EVT_MEDIA_VIDEO;
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(publication));
  EXPECT_EQ(memcmp(frame, publication, sizeof(publication)), 0);
  event.data.publication.id_len = SFU_MAX_ID_LEN + 1;
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), 0u);
  EXPECT_EQ(memcmp(frame, publication, sizeof(publication)), 0);

  uint8_t restart[] = {
    0x53, 0x46, 1, 6, 0, 11, 2, 1, 0, 0, 0, 7,
    0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, '4', '2', 'p', 1, 2, 3, 4, 5, 6, 7, 8
  };
  event.type = SFU_EVT_ICE_RESTART_SUCCEEDED;
  event.detail = 0;
  event.data.ice_generation = UINT64_C(0x0102030405060708);
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(restart));
  EXPECT_EQ(memcmp(frame, restart, sizeof(restart)), 0);

  restart[3] = 7;
  restart[20] = 1;
  event.type = SFU_EVT_ICE_RESTART_FAILED;
  event.detail = SFU_EVT_ICE_RESTART_TIMEOUT;
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(restart));
  EXPECT_EQ(memcmp(frame, restart, sizeof(restart)), 0);
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(restart) - 1, &event), 0u);
  event.data.ice_generation = 0;
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), 0u);
  EXPECT_EQ(memcmp(frame, restart, sizeof(restart)), 0);

  const uint8_t idle[] = {
    0x53, 0x46, 1, 8, 0, 2, 2, 0, 0, 0, 0, 7,
    0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, '4', '2'
  };
  event.type = SFU_EVT_ROOM_IDLE;
  event.detail = 0;
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), 0u);
  event.peer_id_len = 0;
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(idle));
  EXPECT_EQ(memcmp(frame, idle, sizeof(idle)), 0);

  const uint8_t gap[] = {
    0x53, 0x46, 1, 9, 0, 8, 0, 0, 0, 0, 0, 7,
    0, 0, 0, 0, 0, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2
  };
  event.type = SFU_EVT_EVENT_DELIVERY_GAP;
  event.detail = SFU_EVT_GAP_QUEUE_FULL;
  event.data.dropped_count = UINT64_C(0x100000002);
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), 0u);
  event.room_id_len = 0;
  ASSERT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), sizeof(gap));
  EXPECT_EQ(memcmp(frame, gap, sizeof(gap)), 0);
  event.data.dropped_count = 0;
  EXPECT_EQ(SfuEventWireEncodeRecord(frame, sizeof(frame), &event), 0u);
  EXPECT_EQ(memcmp(frame, gap, sizeof(gap)), 0);
}
