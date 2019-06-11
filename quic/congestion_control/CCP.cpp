
#include <cstring>
#include <quic/congestion_control/CCP.h>
#include <quic/congestion_control/CongestionControlFunctions.h>

namespace quic {

CCP::CCP(QuicConnectionStateBase& conn)
    : quic_conn_(conn) {

    // TODO fill this out correctly
    struct ccp_datapath_info info = {
        init_cwnd  : 0,
        mss        : 0,
        src_ip     : 0,
        src_port   : 0,
        dst_ip     : 0, // quic_conn_.peerAddress.getIPAddress() .getPort()
        dst_port   : 0
    };
    strncpy(info.congAlg, "reno", 4);
    ccp_conn_ = ccp_connection_start((void *) this, &info);

    // TODO find the right place for this global initialization:
        /*
    struct ccp_datapath dp = {
        set_cwnd : &_ccp_set_cwnd, //&(CCP::_ccpSetCwnd),
        set_rate_abs : &_ccp_set_rate_abs,
        send_msg : &_ccp_send_msg,
		log : &_ccp_log, 
		time_zero : 0 ,
        now : &now_usecs,
        since_usecs : &time_since_usecs,
        after_usecs : &time_after_usecs,
		state : NULL,
        impl : NULL
    };

    if (ccp_init(&dp) < 0) {
        printf("error! failed to initialize ccp connection map\n");
    }
    */
}


extern "C" {

    static void _ccp_set_cwnd(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t cwnd) {
		CCP *alg = (CCP *) ccp_get_impl(conn);
		alg->setCongestionWindow(cwnd);
    }


    static void _ccp_set_rate_abs(struct ccp_datapath *dp, struct ccp_connection *conn, uint32_t rate) {
		CCP *alg = (CCP *) ccp_get_impl(conn);
		alg->setCongestionWindow(rate);
    }

    static int _ccp_send_msg(struct ccp_datapath *dp, struct ccp_connection *conn, char *msg, int msg_size) {
		return 0;
	}

	static void _ccp_log(struct ccp_datapath *dp, enum ccp_log_level level, const char *msg, int msg_size) {
	}

	uint64_t init_time_ns = 0;
	uint32_t last_print = 0;

	uint64_t now_usecs() {
		struct timespec now;
		uint64_t now_ns, now_us;

		clock_gettime(CLOCK_MONOTONIC, &now);

		now_ns = (1000000000L * now.tv_sec) + now.tv_nsec;
		if (init_time_ns == 0) {
			init_time_ns = now_ns;
		}

		now_us = ((now_ns - init_time_ns) / 1000) & 0xffffffff;
		return now_us;
	}

	uint64_t time_since_usecs(uint64_t then) {
		return now_usecs() - then;
	}

	uint64_t time_after_usecs(uint64_t usecs) {
		return now_usecs() + usecs;
	}
}

void CCP::setCongestionWindow(unsigned int newCwnd) {
    cwndBytes_ = newCwnd; 
}

void CCP::onRemoveBytesFromInflight(uint64_t bytes) {
  subtractAndCheckUnderflow(bytesInFlight_, bytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
           << quic_conn_;
}

void CCP::onPacketSent(const OutstandingPacket& packet) {
  addAndCheckOverflow(bytesInFlight_, packet.encodedSize);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_
           << " packetNum="
           << folly::variant_match(
                  packet.packet.header,
                  [](auto& h) { return h.getPacketSequenceNum(); })
           << " " << quic_conn_;
}

void CCP::onPacketAcked(const AckEvent& ack) {
  DCHECK(ack.largestAckedPacket.hasValue());
  subtractAndCheckUnderflow(bytesInFlight_, ack.ackedBytes);
  VLOG(10) << __func__ << " writable=" << getWritableBytes()
           << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
           << quic_conn_;

  /*
  if (*ack.largestAckedPacket < endOfRecovery_) {
    return;
  }
  */
  if (cwndBytes_ < ssthresh_) {
    addAndCheckOverflow(cwndBytes_, ack.ackedBytes);
  } else {
    // TODO: I think this may be a bug in the specs. We should use
    // quic_conn_.udpSendPacketLen for the cwnd calculation. But I need to
    // check how Linux handles this.
    uint64_t additionFactor =
        (kDefaultUDPSendPacketLen * ack.ackedBytes) / cwndBytes_;
    addAndCheckOverflow(cwndBytes_, additionFactor);
  }
  cwndBytes_ = boundedCwnd(
      cwndBytes_,
      quic_conn_.udpSendPacketLen,
      quic_conn_.transportSettings.maxCwndInMss,
      quic_conn_.transportSettings.minCwndInMss);
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
    endOfRecovery_ = quic_conn_.lossState.largestSent;

    cwndBytes_ = (cwndBytes_ >> kRenoLossReductionFactorShift);
    cwndBytes_ = boundedCwnd(
        cwndBytes_,
        quic_conn_.udpSendPacketLen,
        quic_conn_.transportSettings.maxCwndInMss,
        quic_conn_.transportSettings.minCwndInMss);
    // This causes us to exit slow start.
    ssthresh_ = cwndBytes_;
    VLOG(10) << __func__ << " exit slow start, ssthresh=" << ssthresh_
             << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << bytesInFlight_ << " " << quic_conn_;
  } else {
    VLOG(10) << __func__ << " packetNum=" << *loss.largestLostPacketNum
             << " writable=" << getWritableBytes() << " cwnd=" << cwndBytes_
             << " inflight=" << bytesInFlight_ << " " << quic_conn_;
  }
  */
  if (loss.persistentCongestion) {
    VLOG(10) << __func__ << " writable=" << getWritableBytes()
             << " cwnd=" << cwndBytes_ << " inflight=" << bytesInFlight_ << " "
             << quic_conn_;
    cwndBytes_ = quic_conn_.transportSettings.minCwndInMss * quic_conn_.udpSendPacketLen;
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
  return quic_conn_.transportSettings.writeConnectionDataPacketsLimit;
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
