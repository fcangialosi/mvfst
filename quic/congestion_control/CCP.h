
#pragma once

#include <quic/QuicException.h>
#include <quic/state/StateData.h>
#include <time.h>

extern "C" {
#include "libccp/ccp.h"
}

#include <limits>

namespace quic {

class CCP : public CongestionController {
 public:
  explicit CCP(QuicConnectionStateBase& conn);
  void onRemoveBytesFromInflight(uint64_t) override;
  void onPacketSent(const OutstandingPacket& packet) override;
  void onPacketAckOrLoss(folly::Optional<AckEvent>, folly::Optional<LossEvent>)
      override;

  uint64_t getWritableBytes() const noexcept override;
  uint64_t getCongestionWindow() const noexcept override;
  void setConnectionEmulation(uint8_t) noexcept override;
  bool canBePaced() const noexcept override;
  void setAppLimited(bool, TimePoint) noexcept override;
  CongestionControlType type() const noexcept override;

  bool inSlowStart() const noexcept;

  uint64_t getBytesInFlight() const noexcept;
  
  uint64_t getPacingRate(TimePoint currentTime) noexcept override;
  void markPacerTimeoutScheduled(TimePoint currentTime) noexcept override;
  
  std::chrono::microseconds getPacingInterval() const noexcept override;

  void setMinimalPacingInterval(std::chrono::microseconds) noexcept override;

  bool isAppLimited() const noexcept override;

  //void _ccpSetCwnd(struct ccp_datapath *dp, struct ccp_connection *conn, unsigned int newCwnd);
  void setCongestionWindow(unsigned int newCwnd);
 private:
  void onPacketAcked(const AckEvent&);
  void onPacketLoss(const LossEvent&);
  QuicConnectionStateBase& conn_;
  uint64_t bytesInFlight_{0};
  uint64_t cwndBytes_;
  uint64_t ssthresh_;
};

extern "C" {
static void _ccp_set_cwnd(struct ccp_datapath *dp, struct ccp_connection *conn, unsigned int newCwnd);
static void _ccp_set_rate_abs(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t rate);
static int _ccp_send_msg(struct ccp_datapath *dp, struct ccp_connection *conn, char *msg, int msg_size);
static void _ccp_log(struct ccp_datapath *dp, enum ccp_log_level level, const char *msg, int msg_size);
uint64_t now_usecs();
uint64_t time_since_usecs(uint64_t then);
uint64_t time_after_usecs(uint64_t usecs);
}
}
