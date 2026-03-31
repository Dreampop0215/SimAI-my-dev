#ifndef OPTICAL_OCS_H
#define OPTICAL_OCS_H

#include <cstddef>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <ns3/event-id.h>
#include <ns3/node.h>
#include <ns3/nstime.h>
#include "qbb-net-device.h"
#include "pint.h"

namespace ns3 {

class Packet;

// OCS 抽象节点：按目的地址选期望出口，再受端口映射门控。
class OpticalOcs : public Node {
  static const uint32_t pCnt = 1025;
  static const uint32_t qCnt = 8;

 protected:
  uint32_t m_ccMode;
  uint64_t m_maxRtt;
  uint32_t m_ackHighPrio;

  uint32_t m_numPorts;
  std::string m_portMappingSpec;
  uint32_t m_reconfigurationDelayNs;
  uint32_t m_guardTimeNs;
  uint32_t m_epochNs;
  bool m_enableEpochReconfiguration;
  uint32_t m_pendingMaxPackets;
  uint32_t m_pendingDrainBurst;
  uint32_t m_statusLogIntervalNs;
  uint32_t m_minHoldEpochs;
  uint32_t m_noProgressEpochThreshold;
  uint32_t m_demandAgeWeightBytes;
  uint32_t m_mapChangeGainThresholdBytes;

 private:
  struct PendingPacket {
    Ptr<Packet> packet;
    CustomHeader header;
  };

  struct UnresolvedPacket {
    uint32_t inDev;
    Ptr<Packet> packet;
    CustomHeader header;
  };

  uint32_t m_ecmpSeed;
  std::vector<uint32_t> m_portMap;
  std::vector<uint32_t> m_pendingPortMap;
  std::map<uint32_t, std::vector<uint32_t>> m_rtTable;
  std::map<std::pair<uint32_t, uint32_t>, std::deque<PendingPacket>>
      m_pendingQueues;
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> m_pendingBytes;
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> m_pairAgeEpochs;
  std::deque<UnresolvedPacket> m_unresolvedPending;
  uint32_t m_pendingPackets;
  bool m_pendingSoftOverflowWarned;
  bool m_portMapReady;
  bool m_reconfiguring;
  Time m_reconfigEndTs;
  EventId m_reconfigDoneEvent;
  EventId m_epochEvent;
  EventId m_drainPendingEvent;
  uint32_t m_rotationOffset;
  Time m_lastStatusLogTs;
  uint32_t m_epochsSinceMapChange;
  uint64_t m_lastEpochDequeuedPackets;
  uint32_t m_noProgressEpochs;
  bool m_drainModeActive;
  std::pair<uint32_t, uint32_t> m_drainTargetPair;

  uint64_t m_totalRxPackets;
  uint64_t m_totalFastPathPackets;
  uint64_t m_totalMappedEnqueuePackets;
  uint64_t m_totalUnresolvedEnqueuePackets;
  uint64_t m_totalPendingDequeuedPackets;
  uint64_t m_totalUnresolvedMovedPackets;
  uint64_t m_totalRouteMissPackets;
  uint64_t m_totalPortMismatchBlocks;
  uint64_t m_totalReconfigBlocks;
  uint64_t m_totalSendFailPackets;
  uint64_t m_totalDrainCalls;
  uint64_t m_totalDrainNoProgress;
  uint64_t m_consecutiveDrainNoProgress;

  uint64_t m_txBytes[pCnt];

 private:
  uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed) const;
  int GetOutDev(uint32_t inDev, const CustomHeader& ch) const;
  bool IsValidOutputPort(uint32_t outDev) const;
  bool CanForwardNow(uint32_t inDev, uint32_t outDev);
  bool SendOnPort(uint32_t outDev, Ptr<Packet> p, CustomHeader ch);
  bool EnqueuePending(uint32_t inDev, uint32_t outDev, Ptr<Packet> p,
                      CustomHeader ch, bool countPacket = true);
  void BufferUnresolved(uint32_t inDev, Ptr<Packet> p, CustomHeader ch,
                        bool countPacket = true);
  bool TryMoveUnresolvedToMapped(const UnresolvedPacket& unresolved);
  void AddPendingBytes(const std::pair<uint32_t, uint32_t>& key,
                       uint32_t bytes);
  void ConsumePendingBytes(const std::pair<uint32_t, uint32_t>& key,
                           uint32_t bytes);
  bool HasPendingForPair(uint32_t inDev, uint32_t outDev) const;
  uint64_t PendingBytesForPair(uint32_t inDev, uint32_t outDev) const;
  bool HasBufferedPackets() const;
  uint64_t CountMappedPendingPackets() const;
  void UpdatePendingPairAges();
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> BuildDemandWeights(
      const std::vector<uint32_t>& ports) const;
  std::vector<uint32_t> BuildDemandAwareMap(
      const std::vector<uint32_t>& ports,
      const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights,
      bool pinPair,
      uint32_t pinIn,
      uint32_t pinOut) const;
  uint64_t ComputeMapScore(
      const std::vector<uint32_t>& map,
      const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights) const;
  bool IsSamePortMap(const std::vector<uint32_t>& lhs,
                     const std::vector<uint32_t>& rhs) const;
  std::pair<uint32_t, uint32_t> SelectDrainTargetPair(
      const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights) const;
  void MaybeLogStatus(const char* reason, bool force = false);
  void ScheduleDrainPending(Time delay = NanoSeconds(0));
  void DrainPending();
  void TryForwardOrQueue(uint32_t inDev, uint32_t outDev, Ptr<Packet> p,
                         CustomHeader ch);
  void EnsurePortMapReady();
  std::vector<uint32_t> GetCandidatePorts() const;
  bool BuildMapFromSpec(const std::vector<uint32_t>& ports,
                        std::vector<uint32_t>* outMap) const;
  std::vector<uint32_t> BuildCyclicMap(const std::vector<uint32_t>& ports,
                                       uint32_t rotation) const;
  bool IsReconfigurationActive() const;
  void TriggerEpochReconfiguration();
  void FinishReconfiguration();
  uint32_t SelectQueueIndex(const CustomHeader& ch) const;

 public:
  static TypeId GetTypeId(void);
  OpticalOcs();

  void SetEcmpSeed(uint32_t seed);
  void AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx);
  void ClearTable();

  bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                               CustomHeader& ch);
  void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

  int logres_shift(int b, int l);
  int log2apprx(int x, int b, int m, int l);

  uint64_t last_txBytes[pCnt];
  uint64_t last_port_qlen[pCnt];
  void PrintSwitchQlen(FILE* qlen_output);
  void PrintSwitchBw(FILE* bw_output, uint32_t bw_mon_interval);
};

} // namespace ns3

#endif // OPTICAL_OCS_H
