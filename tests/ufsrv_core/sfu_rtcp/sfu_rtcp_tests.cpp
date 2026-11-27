/**
 * @file sfu_rtcp_tests.cpp
 * @brief The RTCP compound walker, driven by hand-built packets.
 *
 * Each case is a byte fixture and the events expected from it: the muxed and
 * plain payload-type forms, SR timing extraction, SR/RR/SDES/BYE/PLI/FIR/NACK
 * classification, feedback-first reduced-size compounds, padding on the final
 * packet and the refusal of padding anywhere else, truncation, and an unknown
 * type that is reported rather than fatal.
 *
 * Copyright (C) 2015-2026  unfacd works
 */

#include "gtest/gtest.h"

#include <arpa/inet.h>

#include <cstdint>
#include <vector>

extern "C" {
#include "rtcp/sfu_rtcp_parse.h"
}

namespace {

struct Visited {
  SfuRtcpKind kind;
  uint8_t     fmt;
  uint32_t    sender_ssrc;
  uint32_t    media_ssrc;
  uint32_t    ntp_mid32;
  uint8_t     fir_sequence;
  uint8_t     nack_entries;
  uint16_t    nack_requests;
};

bool collect(void *context, const SfuRtcpEvent *event)
{
  auto *all = static_cast<std::vector<Visited> *>(context);
  all->push_back(Visited{event->kind, event->fmt, event->sender_ssrc, event->media_ssrc, event->ntp_mid32,
                         event->fir_sequence, event->nack_entries, event->nack_requests});
  return true;
}

void be16(std::vector<uint8_t> &out, uint16_t value)
{
  out.push_back((uint8_t)(value >> 8));
  out.push_back((uint8_t)(value & 0xff));
}

void put_ssrc(std::vector<uint8_t> &out, uint32_t value) {
  const uint32_t wire = htonl(value);
  out.insert(out.end(), (const uint8_t *)&wire, (const uint8_t *)&wire + 4);
}

void be32(std::vector<uint8_t> &out, uint32_t value)
{
  out.push_back((uint8_t)(value >> 24));
  out.push_back((uint8_t)(value >> 16));
  out.push_back((uint8_t)(value >> 8));
  out.push_back((uint8_t)value);
}

/** Common header: RC/FMT, payload type, length in words minus one. */
void header(std::vector<uint8_t> &out, uint8_t rc_fmt, uint8_t payload_type, uint16_t words)
{
  out.push_back((uint8_t)(0x80u | (rc_fmt & 0x1fu)));
  out.push_back(payload_type);
  be16(out, words);
}

/** A 28-byte sender report with a known NTP pair. */
std::vector<uint8_t> sender_report(uint32_t ssrc, uint32_t ntp_seconds, uint32_t ntp_fraction, uint8_t payload_type)
{
  std::vector<uint8_t> out;
  header(out, 0, payload_type, 6);
  put_ssrc(out, ssrc); /* sender SSRC */
  be32(out, ntp_seconds);
  be32(out, ntp_fraction);
  be32(out, 0x00010002); /* RTP timestamp */
  be32(out, 10);         /* packet count */
  be32(out, 1000);       /* octet count */
  return out;
}

std::vector<uint8_t> receiver_report(uint32_t ssrc)
{
  std::vector<uint8_t> out;
  header(out, 0, SFU_RTCP_PT_RR, 1);
  put_ssrc(out, ssrc);
  return out;
}

std::vector<uint8_t> feedback(uint8_t fmt, uint32_t sender, uint32_t media, bool with_fci_sequence)
{
  std::vector<uint8_t> out;
  header(out, fmt, SFU_RTCP_PT_PSFB, with_fci_sequence ? 4 : 2);
  put_ssrc(out, sender);
  put_ssrc(out, media);
  if (with_fci_sequence) {
    put_ssrc(out, 0xABCDEF01u);      /* FIR FCI target SSRC */
    out.push_back(7);            /* FIR sequence number */
    out.insert(out.end(), 3, 0); /* reserved */
  }
  return out;
}

std::vector<uint8_t> generic_nack()
{
  std::vector<uint8_t> out;
  header(out, SFU_RTCP_FMT_NACK, SFU_RTCP_PT_RTPFB, 3);
  put_ssrc(out, 0x01020304u); /* sender */
  put_ssrc(out, 0x05060708u); /* media source */
  be16(out, 100);         /* packet id */
  be16(out, 0);           /* bitmask */
  return out;
}

std::vector<uint8_t> sdes(uint32_t ssrc)
{
  std::vector<uint8_t> out;
  header(out, 1, SFU_RTCP_PT_SDES, 2);
  put_ssrc(out, ssrc);
  out.insert(out.end(), 4, 0); /* one empty chunk: enough for the walker */
  return out;
}

struct Parsed {
  std::vector<Visited> events;
  size_t               packets = 0;
  bool                 malformed = false;
};

Parsed parse(const std::vector<uint8_t> &compound)
{
  Parsed parsed;
  parsed.packets = SfuRtcpParseCompound(compound.data(), compound.size(), collect, &parsed.events, &parsed.malformed);
  return parsed;
}

TEST(SfuRtcpParseTest, ReadsSenderReportTimingInPlainForm)
{
  const auto parsed = parse(sender_report(0x11223344u, 0xAABBCCDDu, 0x11223344u, SFU_RTCP_PT_SR));
  ASSERT_TRUE(parsed.events.size() == 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_SR);
  EXPECT_EQ(parsed.events[0].sender_ssrc, 0x11223344u);
  EXPECT_EQ(parsed.events[0].ntp_mid32, 0xCCDD1122u); /* middle 32 bits */
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, AcceptsTheMuxedPayloadType)
{
  /* 200 - 128: what a muxed socket actually carries. */
  const auto parsed = parse(sender_report(0x55667788u, 1, 2, 72));
  ASSERT_TRUE(parsed.events.size() == 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_SR);
  EXPECT_EQ(parsed.events[0].sender_ssrc, 0x55667788u);
}

TEST(SfuRtcpParseTest, WalksACompoundOfReportAndSdes)
{
  auto compound = sender_report(1, 2, 3, SFU_RTCP_PT_SR);
  const auto rr = receiver_report(4);
  compound.insert(compound.end(), rr.begin(), rr.end());
  const auto sdes_packet = sdes(1);
  compound.insert(compound.end(), sdes_packet.begin(), sdes_packet.end());

  const auto parsed = parse(compound);
  ASSERT_EQ(parsed.events.size(), 3u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_SR);
  EXPECT_EQ(parsed.events[1].kind, SFU_RTCP_KIND_RR);
  EXPECT_EQ(parsed.events[2].kind, SFU_RTCP_KIND_SDES);
  EXPECT_EQ(parsed.packets, 3u);
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, AcceptsReducedSizeFeedbackFirst)
{
  /* a=rtcp-rsize: PLI arrives without a leading SR or RR. */
  const auto parsed = parse(feedback(SFU_RTCP_FMT_PLI, 0x11, 0x22, false));
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_PLI);
  EXPECT_EQ(parsed.events[0].media_ssrc, 0x22u);
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, ReadsFirTargetFromTheFci)
{
  const auto parsed = parse(feedback(SFU_RTCP_FMT_FIR, 0x11, 0x22, true));
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_FIR);
  EXPECT_EQ(parsed.events[0].media_ssrc, 0xABCDEF01u);
  EXPECT_EQ(parsed.events[0].fir_sequence, 7u);
}

TEST(SfuRtcpParseTest, ClassifiesGenericNack)
{
  const auto parsed = parse(generic_nack());
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_NACK);
  EXPECT_EQ(parsed.events[0].fmt, SFU_RTCP_FMT_NACK);
  EXPECT_EQ(parsed.events[0].media_ssrc, 0x05060708u);
  EXPECT_EQ(parsed.events[0].nack_entries, 1u);
  EXPECT_EQ(parsed.events[0].nack_requests, 1u) << "one PID, empty bitmask";
}

TEST(SfuRtcpParseTest, ReportsAnUnknownTypeWithoutFailingTheCompound)
{
  std::vector<uint8_t> unknown;
  header(unknown, 0, 204, 1); /* APP: parsed, not acted on */
  be32(unknown, 0x99);
  const auto parsed = parse(unknown);
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_UNKNOWN);
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, StripsPaddingFromTheFinalPacket)
{
  auto compound = receiver_report(7);
  compound[0] |= 0x20u;                       /* P bit on the last packet */
  compound.insert(compound.end(), {0, 0, 0, 4}); /* four padding octets */
  compound[2] = 0;                            /* length now three words */
  compound[3] = 2;

  const auto parsed = parse(compound);
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_RR);
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, RejectsPaddingThatIsNotOnTheLastPacket)
{
  auto compound = receiver_report(7);
  compound[0] |= 0x20u;
  compound.insert(compound.end(), {0, 0, 0, 4});
  const auto trailing = receiver_report(9);
  compound.insert(compound.end(), trailing.begin(), trailing.end());

  const auto parsed = parse(compound);
  EXPECT_TRUE(parsed.malformed);
}

TEST(SfuRtcpParseTest, RejectsALengthThatOverrunsTheCompound)
{
  auto compound = sender_report(1, 2, 3, SFU_RTCP_PT_SR);
  compound[3] = 40; /* claims 164 bytes in a 28-byte compound */
  const auto parsed = parse(compound);
  EXPECT_TRUE(parsed.malformed);
  EXPECT_EQ(parsed.events.size(), 0u);
}

TEST(SfuRtcpParseTest, RejectsATrailingFragment)
{
  auto compound = receiver_report(7);
  compound.push_back(0x80); /* one byte that cannot be a header */
  const auto parsed = parse(compound);
  EXPECT_TRUE(parsed.malformed);
}

std::vector<uint8_t> nack_with_bitmask(uint16_t bitmask, uint16_t second_pid, uint16_t second_bitmask)
{
  std::vector<uint8_t> out;
  header(out, SFU_RTCP_FMT_NACK, SFU_RTCP_PT_RTPFB, 1 + 2); /* two 4-byte FCI entries */
  put_ssrc(out, 0x01020304u);
  put_ssrc(out, 0x05060708u);
  be16(out, 100);
  be16(out, bitmask);
  if (second_bitmask || second_pid) {
    be16(out, second_pid);
    be16(out, second_bitmask);
    out[3] = 1 + 3; /* header, one FCI, second FCI, third words */
  }
  return out;
}

TEST(SfuRtcpParseTest, ExpandsNackBitmasksAndMultipleFcis)
{
  /* Each entry always names its PID; the bitmask adds the later sequences.
   * 0x00FF names eight more, so the two entries name ten in total. */
  const auto parsed = parse(nack_with_bitmask(0x00FFu, 0x012Cu, 0));
  ASSERT_EQ(parsed.events.size(), 1u);
  EXPECT_EQ(parsed.events[0].kind, SFU_RTCP_KIND_NACK);
  EXPECT_EQ(parsed.events[0].nack_entries, 2u);
  EXPECT_EQ(parsed.events[0].nack_requests, 10u);
  EXPECT_FALSE(parsed.malformed);
}

TEST(SfuRtcpParseTest, RejectsANackWhoseFciIsNotWholeEntries)
{
  auto nack = generic_nack();
  nack.push_back(0x00); /* a partial PID/BLP */
  nack[3] = 4;          /* claim the extra byte */
  const auto parsed = parse(nack);
  EXPECT_TRUE(parsed.malformed);
}

} // namespace
