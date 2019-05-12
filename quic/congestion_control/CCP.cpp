
#include <quic/congestion_control/CCP.h>
#include <quic/congestion_control/CongestionControlFunctions.h>

namespace quic {

CCP::CCP(QuicConnectionStateBase& conn)
    : conn_(conn) {
        ccp_test();
}

void CCP::onRemoveBytesFromInflight(uint64_t bytes) {
  subtractAndCheckUnderflow(bytesInFlight_, bytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
           << conn_;
}

void CCP::onPacketSent(const OutstandingPacket& packet) {
  addAndCheckOverflow(bytesInFlight_, packet.encodedSize);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_
           << " packetNum="
           << folly::variant_match(
                  packet.packet.header,
                  [](auto& h) { return h.getPacketSequenceNum(); })
           << " " << conn_;
}

void CCP::onPacketAcked(const AckEvent& ack) {
  DCHECK(ack.largestAckedPacket.hasValue());
  subtractAndCheckUnderflow(bytesInFlight_, ack.ackedBytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
           << conn_;

  /*
  if (*ack.largestAckedPacket < endOfRecovery_) {
    return;
  }
  */
  if (cwndBytes_ < ssthresh_) {
    addAndCheckOverflow(cwndBytes_, ack.ackedBytes);
  } else {
    // TODO: I think this may be a bug in the specs. We should use
    // conn_.udpSendPacketLen for the cwnd calculation. But I need to
    // check how Linux handles this.
    uint64_t additionFactor =
        (kDefaultUDPSendPacketLen * ack.ackedBytes) / cwndBytes_;
    addAndCheckOverflow(cwndBytes_, additionFactor);
  }
  cwndBytes_ = boundedCwnd(
      cwndBytes_,
      conn_.udpSendPacketLen,
      conn_.transportSettings.maxCwndInMss,
      conn_.transportSettings.minCwndInMss);
}

void CCP::onPacketAckOrLoss(
    folly::Optional<AckEvent> ackEvent,
    folly::Optional<LossEvent> lossEvent) {
  if (lossEvent) {
    onPacketLoss(*lossEvent);
  }
  if (ackEvent && ackEvent->largestAckedPacket.hasValue()) {
    onPacketAcked(*ackEvent);
  }
}

void CCP::onPacketLoss(const LossEvent& loss) {
  DCHECK(loss.largestLostPacketNum.hasValue());
  subtractAndCheckUnderflow(bytesInFlight_, loss.lostBytes);
  /*
  if (endOfRecovery_ < *loss.largestLostPacketNum) {
    endOfRecovery_ = conn_.lossState.largestSent;

    cwndBytes_ = (cwndBytes_ >> kRenoLossReductionFactorShift);
    cwndBytes_ = boundedCwnd(
        cwndBytes_,
        conn_.udpSendPacketLen,
        conn_.transportSettings.maxCwndInMss,
        conn_.transportSettings.minCwndInMss);
    // This causes us to exit slow start.
    ssthresh_ = cwndBytes_;
    VLOG(10) << __func__ << " exit slow start, ssthresh=" << ssthresh_
             << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << bytesInFlight_ << " " << conn_;
  } else {
    VLOG(10) << __func__ << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << bytesInFlight_ << " " << conn_;
  }
  */
  if (loss.persistentCongestion) {
    VLOG(10) << __func__ << " writable=" << getWritableBytes()
             << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
             << conn_;
    cwndBytes_ = conn_.transportSettings.minCwndInMss * conn_.udpSendPacketLen;
  }
}

uint64_t CCP::getWritableBytes() const noexcept {
  if (bytesInFlight_ > cwndBytes_) {
    return 0;
  } else {
    return cwndBytes_ - bytesInFlight_;
  }
}

uint64_t CCP::getCongestionWindow() const noexcept {
  return cwndBytes_;
}

bool CCP::inSlowStart() const noexcept {
  return cwndBytes_ < ssthresh_;
}

CongestionControlType CCP::type() const noexcept {
  return CongestionControlType::CCP;
}

void CCP::setConnectionEmulation(uint8_t) noexcept {}

bool CCP::canBePaced() const noexcept {
  // Pacing is not supported on CCP currently
  return false;
}

uint64_t CCP::getBytesInFlight() const noexcept {
  return bytesInFlight_;
}

uint64_t CCP::getPacingRate(TimePoint /* currentTime */) noexcept {
  // Pacing is not supported on CCP currently
  return conn_.transportSettings.writeConnectionDataPacketsLimit;
}

void CCP::markPacerTimeoutScheduled(TimePoint /* currentTime */) noexcept {
  // Pacing is not supported on CCP currently
}
std::chrono::microseconds CCP::getPacingInterval() const noexcept {
  // Pacing is not supported on CCP currently
  return std::chrono::microseconds(
      folly::HHWheelTimerHighRes::DEFAULT_TICK_INTERVAL);
}

void CCP::setMinimalPacingInterval(std::chrono::microseconds) noexcept {}

void CCP::setAppLimited(bool, TimePoint) noexcept { /* unsupported */
}

bool CCP::isAppLimited() const noexcept {
  return false; // unsupported
}

} // namespace quic
