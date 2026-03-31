#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/boolean.h"
#include "ns3/net-device.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/simulator.h"
#include "optical-ocs.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace ns3 {

namespace {

std::string Trim(const std::string& s) {
  const size_t begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return std::string();
  }
  const size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

} // namespace

TypeId OpticalOcs::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::OpticalOcs")
          .SetParent<Node>()
          .AddConstructor<OpticalOcs>()
          .AddAttribute("CcMode", "CC mode.", UintegerValue(0),
                        MakeUintegerAccessor(&OpticalOcs::m_ccMode),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("MaxRtt", "Max Rtt of the network",
                        UintegerValue(9000),
                        MakeUintegerAccessor(&OpticalOcs::m_maxRtt),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("AckHighPrio", "Set high priority for ACK/NACK or not",
                        UintegerValue(0),
                        MakeUintegerAccessor(&OpticalOcs::m_ackHighPrio),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "NumPorts",
              "Number of OCS data ports (0 means auto from attached devices).",
              UintegerValue(0), MakeUintegerAccessor(&OpticalOcs::m_numPorts),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "PortMapping",
              "Static port mapping string. Example: 1:4,2:3,3:1,4:2. Empty means "
              "auto-generated cyclic matching.",
              StringValue(""),
              MakeStringAccessor(&OpticalOcs::m_portMappingSpec),
              MakeStringChecker())
          .AddAttribute(
              "ReconfigurationDelayNs",
              "OCS reconfiguration delay in ns.",
              UintegerValue(100),
              MakeUintegerAccessor(&OpticalOcs::m_reconfigurationDelayNs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "GuardTimeNs",
              "Optional guard time after each reconfiguration in ns.",
              UintegerValue(0),
              MakeUintegerAccessor(&OpticalOcs::m_guardTimeNs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "EpochNs",
              "Epoch interval for periodic reconfiguration in ns.",
              UintegerValue(5000),
              MakeUintegerAccessor(&OpticalOcs::m_epochNs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "EnableEpochReconfiguration",
              "Enable periodic epoch-based reconfiguration.",
              BooleanValue(true),
              MakeBooleanAccessor(&OpticalOcs::m_enableEpochReconfiguration),
              MakeBooleanChecker())
          .AddAttribute(
              "PendingMaxPackets",
              "Maximum number of packets buffered while waiting for an active "
              "port mapping (0 means no explicit limit).",
              UintegerValue(200000),
              MakeUintegerAccessor(&OpticalOcs::m_pendingMaxPackets),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "PendingDrainBurst",
              "Maximum buffered packets drained per drain invocation.",
              UintegerValue(256),
              MakeUintegerAccessor(&OpticalOcs::m_pendingDrainBurst),
              MakeUintegerChecker<uint32_t>(1))
          .AddAttribute(
              "StatusLogIntervalNs",
              "Status log interval in ns for OCS diagnostics (0 disables "
              "periodic status logs).",
              UintegerValue(0),
              MakeUintegerAccessor(&OpticalOcs::m_statusLogIntervalNs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "MinHoldEpochs",
              "Minimum epochs to keep current mapping before switching.",
              UintegerValue(2),
              MakeUintegerAccessor(&OpticalOcs::m_minHoldEpochs),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "NoProgressEpochThreshold",
              "Epochs without dequeue progress before entering drain mode.",
              UintegerValue(16),
              MakeUintegerAccessor(&OpticalOcs::m_noProgressEpochThreshold),
              MakeUintegerChecker<uint32_t>(1))
          .AddAttribute(
              "DemandAgeWeightBytes",
              "Per-epoch age bonus (in bytes) added to each pending pair "
              "during demand-aware matching.",
              UintegerValue(4096),
              MakeUintegerAccessor(&OpticalOcs::m_demandAgeWeightBytes),
              MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "MapChangeGainThresholdBytes",
              "Minimum demand score improvement required to accept a mapping "
              "change in auto mode.",
              UintegerValue(0),
              MakeUintegerAccessor(&OpticalOcs::m_mapChangeGainThresholdBytes),
              MakeUintegerChecker<uint32_t>());
  return tid;
}

OpticalOcs::OpticalOcs()
    : m_ccMode(0),
      m_maxRtt(9000),
      m_ackHighPrio(0),
      m_numPorts(0),
      m_reconfigurationDelayNs(100),
      m_guardTimeNs(0),
      m_epochNs(5000),
      m_enableEpochReconfiguration(true),
      m_pendingMaxPackets(200000),
      m_pendingDrainBurst(256),
      m_statusLogIntervalNs(0),
      m_minHoldEpochs(2),
      m_noProgressEpochThreshold(16),
      m_demandAgeWeightBytes(4096),
      m_mapChangeGainThresholdBytes(0),
      m_ecmpSeed(0),
      m_pendingPackets(0),
      m_pendingSoftOverflowWarned(false),
      m_portMapReady(false),
      m_reconfiguring(false),
      m_reconfigEndTs(NanoSeconds(0)),
      m_rotationOffset(1),
      m_lastStatusLogTs(NanoSeconds(0)),
      m_epochsSinceMapChange(0),
      m_lastEpochDequeuedPackets(0),
      m_noProgressEpochs(0),
      m_drainModeActive(false),
      m_drainTargetPair(std::make_pair(0u, 0u)),
      m_totalRxPackets(0),
      m_totalFastPathPackets(0),
      m_totalMappedEnqueuePackets(0),
      m_totalUnresolvedEnqueuePackets(0),
      m_totalPendingDequeuedPackets(0),
      m_totalUnresolvedMovedPackets(0),
      m_totalRouteMissPackets(0),
      m_totalPortMismatchBlocks(0),
      m_totalReconfigBlocks(0),
      m_totalSendFailPackets(0),
      m_totalDrainCalls(0),
      m_totalDrainNoProgress(0),
      m_consecutiveDrainNoProgress(0) {
  m_node_type = 3;

  for (uint32_t i = 0; i < pCnt; ++i) {
    m_txBytes[i] = 0;
    last_txBytes[i] = 0;
    last_port_qlen[i] = 0;
  }

  // 等到仿真开始后（设备已挂载）再初始化端口映射。
  Simulator::ScheduleNow(&OpticalOcs::EnsurePortMapReady, this);
}

void OpticalOcs::SetEcmpSeed(uint32_t seed) { m_ecmpSeed = seed; }

void OpticalOcs::AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx) {
  const uint32_t dip = dstAddr.Get();
  m_rtTable[dip].push_back(intf_idx);
}

void OpticalOcs::ClearTable() {
  m_rtTable.clear();
  m_pendingQueues.clear();
  m_pendingBytes.clear();
  m_pairAgeEpochs.clear();
  m_unresolvedPending.clear();
  m_pendingPackets = 0;
  m_pendingSoftOverflowWarned = false;
  if (!m_drainPendingEvent.IsExpired()) {
    Simulator::Cancel(m_drainPendingEvent);
  }
  m_consecutiveDrainNoProgress = 0;
  m_noProgressEpochs = 0;
  m_lastEpochDequeuedPackets = m_totalPendingDequeuedPackets;
  m_drainModeActive = false;
  m_drainTargetPair = std::make_pair(0u, 0u);
}

uint32_t OpticalOcs::EcmpHash(const uint8_t* key, size_t len,
                              uint32_t seed) const {
  uint32_t h = seed;
  if (len > 3) {
    const uint32_t* key_x4 = (const uint32_t*)key;
    size_t i = len >> 2;
    do {
      uint32_t k = *key_x4++;
      k *= 0xcc9e2d51;
      k = (k << 15) | (k >> 17);
      k *= 0x1b873593;
      h ^= k;
      h = (h << 13) | (h >> 19);
      h += (h << 2) + 0xe6546b64;
    } while (--i);
    key = (const uint8_t*)key_x4;
  }
  if (len & 3) {
    size_t i = len & 3;
    uint32_t k = 0;
    key = &key[i - 1];
    do {
      k <<= 8;
      k |= *key--;
    } while (--i);
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    h ^= k;
  }
  h ^= len;
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;
  return h;
}

int OpticalOcs::GetOutDev(uint32_t inDev, const CustomHeader& ch) const {
  auto entry = m_rtTable.find(ch.dip);
  if (entry == m_rtTable.end() || entry->second.empty()) {
    return -1;
  }

  std::vector<uint32_t> nexthops;
  nexthops.reserve(entry->second.size());
  for (uint32_t outDev : entry->second) {
    if (!IsValidOutputPort(outDev) || outDev == inDev) {
      continue;
    }
    if (!m_devices[outDev]->IsLinkUp()) {
      continue;
    }
    nexthops.push_back(outDev);
  }
  if (nexthops.empty()) {
    return -1;
  }

  // 优先选择当前 OCS 映射允许的出口，避免 ECMP 与 port-map 长期不一致。
  if (m_portMapReady && inDev < m_portMap.size()) {
    const uint32_t mappedOut = m_portMap[inDev];
    if (std::find(nexthops.begin(), nexthops.end(), mappedOut) != nexthops.end()) {
      return static_cast<int>(mappedOut);
    }
  }

  if (nexthops.size() == 1) {
    return static_cast<int>(nexthops.front());
  }

  union {
    uint8_t u8[4 + 4 + 2 + 2];
    uint32_t u32[3];
  } buf;
  buf.u32[0] = ch.sip;
  buf.u32[1] = ch.dip;
  buf.u32[2] = 0;
  if (ch.l3Prot == 0x6) {
    buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
  } else if (ch.l3Prot == 0x11) {
    buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
  } else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD) {
    buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);
  }

  const uint32_t idx = EcmpHash(buf.u8, 12, m_ecmpSeed) % nexthops.size();
  return static_cast<int>(nexthops[idx]);
}

bool OpticalOcs::IsValidOutputPort(uint32_t outDev) const {
  return outDev > 0 && outDev < GetNDevices() && outDev < m_devices.size() &&
         m_devices[outDev] != nullptr;
}

bool OpticalOcs::CanForwardNow(uint32_t inDev, uint32_t outDev) {
  EnsurePortMapReady();
  if (IsReconfigurationActive() || !m_portMapReady || inDev >= m_portMap.size()) {
    if (IsReconfigurationActive()) {
      ++m_totalReconfigBlocks;
    }
    return false;
  }
  if (!IsValidOutputPort(outDev) || outDev == inDev) {
    return false;
  }
  if (m_portMap[inDev] != outDev) {
    ++m_totalPortMismatchBlocks;
    return false;
  }
  return m_devices[outDev]->IsLinkUp();
}

bool OpticalOcs::SendOnPort(uint32_t outDev, Ptr<Packet> p, CustomHeader ch) {
  const uint32_t qIndex = SelectQueueIndex(ch);
  const bool ok = m_devices[outDev]->SwitchSend(qIndex, p, ch);
  if (!ok) {
    ++m_totalSendFailPackets;
    MaybeLogStatus("send_fail", false);
  }
  return ok;
}

bool OpticalOcs::EnqueuePending(uint32_t inDev, uint32_t outDev, Ptr<Packet> p,
                                CustomHeader ch, bool countPacket) {
  if (!IsValidOutputPort(outDev) || outDev == inDev) {
    return false;
  }
  if (countPacket) {
    if (m_pendingMaxPackets > 0 && m_pendingPackets >= m_pendingMaxPackets &&
        !m_pendingSoftOverflowWarned) {
      std::cerr << "[OpticalOcs] PendingMaxPackets("
                << m_pendingMaxPackets
                << ") reached; keep buffering to preserve lossless behavior."
                << std::endl;
      m_pendingSoftOverflowWarned = true;
    }
    ++m_pendingPackets;
  }
  PendingPacket pending{p->Copy(), ch};
  const std::pair<uint32_t, uint32_t> key = std::make_pair(inDev, outDev);
  m_pendingQueues[key].push_back(pending);
  AddPendingBytes(key, pending.packet->GetSize());
  ++m_totalMappedEnqueuePackets;
  return true;
}

void OpticalOcs::BufferUnresolved(uint32_t inDev, Ptr<Packet> p, CustomHeader ch,
                                  bool countPacket) {
  if (countPacket) {
    if (m_pendingMaxPackets > 0 && m_pendingPackets >= m_pendingMaxPackets &&
        !m_pendingSoftOverflowWarned) {
      std::cerr << "[OpticalOcs] PendingMaxPackets("
                << m_pendingMaxPackets
                << ") reached; keep buffering to preserve lossless behavior."
                << std::endl;
      m_pendingSoftOverflowWarned = true;
    }
    ++m_pendingPackets;
  }
  UnresolvedPacket unresolved{inDev, p->Copy(), ch};
  m_unresolvedPending.push_back(unresolved);
  ++m_totalUnresolvedEnqueuePackets;
}

bool OpticalOcs::TryMoveUnresolvedToMapped(const UnresolvedPacket& unresolved) {
  const int desiredOut = GetOutDev(unresolved.inDev, unresolved.header);
  if (desiredOut < 0) {
    return false;
  }
  const uint32_t outDev = static_cast<uint32_t>(desiredOut);
  if (CanForwardNow(unresolved.inDev, outDev)) {
    if (SendOnPort(outDev, unresolved.packet, unresolved.header)) {
      if (m_pendingPackets > 0) {
        --m_pendingPackets;
      }
      ++m_totalPendingDequeuedPackets;
      ++m_totalUnresolvedMovedPackets;
      return true;
    }
  }

  if (EnqueuePending(unresolved.inDev, outDev, unresolved.packet,
                     unresolved.header, false)) {
    ++m_totalUnresolvedMovedPackets;
    return true;
  }
  return false;
}

void OpticalOcs::AddPendingBytes(const std::pair<uint32_t, uint32_t>& key,
                                 uint32_t bytes) {
  if (bytes == 0) {
    return;
  }
  m_pendingBytes[key] += bytes;
}

void OpticalOcs::ConsumePendingBytes(const std::pair<uint32_t, uint32_t>& key,
                                     uint32_t bytes) {
  auto it = m_pendingBytes.find(key);
  if (it == m_pendingBytes.end() || bytes == 0) {
    return;
  }
  if (it->second <= bytes) {
    m_pendingBytes.erase(it);
  } else {
    it->second -= bytes;
  }
}

bool OpticalOcs::HasPendingForPair(uint32_t inDev, uint32_t outDev) const {
  auto it = m_pendingQueues.find(std::make_pair(inDev, outDev));
  return it != m_pendingQueues.end() && !it->second.empty();
}

uint64_t OpticalOcs::PendingBytesForPair(uint32_t inDev, uint32_t outDev) const {
  auto it = m_pendingBytes.find(std::make_pair(inDev, outDev));
  if (it == m_pendingBytes.end()) {
    return 0;
  }
  return it->second;
}

bool OpticalOcs::HasBufferedPackets() const {
  return !m_pendingQueues.empty() || !m_unresolvedPending.empty();
}

uint64_t OpticalOcs::CountMappedPendingPackets() const {
  uint64_t total = 0;
  for (const auto& kv : m_pendingQueues) {
    total += kv.second.size();
  }
  return total;
}

void OpticalOcs::UpdatePendingPairAges() {
  std::set<std::pair<uint32_t, uint32_t>> alive;
  for (const auto& kv : m_pendingQueues) {
    if (kv.second.empty()) {
      continue;
    }
    const std::pair<uint32_t, uint32_t>& key = kv.first;
    alive.insert(key);
    m_pairAgeEpochs[key] += 1;
    if (m_pendingBytes.find(key) == m_pendingBytes.end()) {
      uint64_t bytes = 0;
      for (const auto& pending : kv.second) {
        bytes += pending.packet->GetSize();
      }
      if (bytes > 0) {
        m_pendingBytes[key] = bytes;
      }
    }
  }

  for (auto it = m_pairAgeEpochs.begin(); it != m_pairAgeEpochs.end();) {
    if (alive.count(it->first) == 0) {
      it = m_pairAgeEpochs.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = m_pendingBytes.begin(); it != m_pendingBytes.end();) {
    if (alive.count(it->first) == 0) {
      it = m_pendingBytes.erase(it);
    } else {
      ++it;
    }
  }
}

std::map<std::pair<uint32_t, uint32_t>, uint64_t> OpticalOcs::BuildDemandWeights(
    const std::vector<uint32_t>& ports) const {
  std::map<std::pair<uint32_t, uint32_t>, uint64_t> weights;
  if (ports.empty()) {
    return weights;
  }
  std::set<uint32_t> validPorts(ports.begin(), ports.end());

  for (const auto& kv : m_pendingBytes) {
    const uint32_t inDev = kv.first.first;
    const uint32_t outDev = kv.first.second;
    if (validPorts.count(inDev) == 0 || validPorts.count(outDev) == 0 ||
        inDev == outDev || kv.second == 0) {
      continue;
    }
    uint64_t weight = kv.second;
    auto ageIt = m_pairAgeEpochs.find(kv.first);
    if (ageIt != m_pairAgeEpochs.end()) {
      weight += static_cast<uint64_t>(ageIt->second) *
                static_cast<uint64_t>(m_demandAgeWeightBytes);
    }
    weights[kv.first] += weight;
  }

  for (const auto& unresolved : m_unresolvedPending) {
    const int desiredOut = GetOutDev(unresolved.inDev, unresolved.header);
    if (desiredOut < 0) {
      continue;
    }
    const uint32_t inDev = unresolved.inDev;
    const uint32_t outDev = static_cast<uint32_t>(desiredOut);
    if (inDev == outDev || validPorts.count(inDev) == 0 ||
        validPorts.count(outDev) == 0) {
      continue;
    }
    weights[std::make_pair(inDev, outDev)] += unresolved.packet->GetSize();
  }

  return weights;
}

std::vector<uint32_t> OpticalOcs::BuildDemandAwareMap(
    const std::vector<uint32_t>& ports,
    const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights,
    bool pinPair,
    uint32_t pinIn,
    uint32_t pinOut) const {
  std::vector<uint32_t> map(GetNDevices(), std::numeric_limits<uint32_t>::max());
  if (ports.empty()) {
    return map;
  }

  std::set<uint32_t> unmatchedIns(ports.begin(), ports.end());
  std::set<uint32_t> unmatchedOuts(ports.begin(), ports.end());

  if (pinPair && pinIn != pinOut && unmatchedIns.count(pinIn) != 0 &&
      unmatchedOuts.count(pinOut) != 0) {
    map[pinIn] = pinOut;
    unmatchedIns.erase(pinIn);
    unmatchedOuts.erase(pinOut);
  }

  struct Edge {
    uint32_t inDev;
    uint32_t outDev;
    uint64_t weight;
    uint32_t age;
  };
  std::vector<Edge> edges;
  edges.reserve(weights.size());
  for (const auto& kv : weights) {
    const uint32_t inDev = kv.first.first;
    const uint32_t outDev = kv.first.second;
    if (inDev == outDev || kv.second == 0 || unmatchedIns.count(inDev) == 0 ||
        unmatchedOuts.count(outDev) == 0) {
      continue;
    }
    uint32_t age = 0;
    auto ageIt = m_pairAgeEpochs.find(kv.first);
    if (ageIt != m_pairAgeEpochs.end()) {
      age = ageIt->second;
    }
    edges.push_back(Edge{inDev, outDev, kv.second, age});
  }

  std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
    if (a.weight != b.weight) {
      return a.weight > b.weight;
    }
    if (a.age != b.age) {
      return a.age > b.age;
    }
    if (a.inDev != b.inDev) {
      return a.inDev < b.inDev;
    }
    return a.outDev < b.outDev;
  });

  for (const auto& edge : edges) {
    if (unmatchedIns.count(edge.inDev) == 0 ||
        unmatchedOuts.count(edge.outDev) == 0) {
      continue;
    }
    map[edge.inDev] = edge.outDev;
    unmatchedIns.erase(edge.inDev);
    unmatchedOuts.erase(edge.outDev);
  }

  // Fill unmatched ports to keep a complete perfect matching.
  std::vector<uint32_t> remOuts(unmatchedOuts.begin(), unmatchedOuts.end());
  if (!remOuts.empty()) {
    uint32_t cursor = remOuts.size() <= 1 ? 0 : (m_rotationOffset % remOuts.size());
    for (uint32_t inDev : unmatchedIns) {
      uint32_t bestIdx = std::numeric_limits<uint32_t>::max();
      for (uint32_t i = 0; i < remOuts.size(); ++i) {
        const uint32_t idx = (cursor + i) % remOuts.size();
        if (remOuts[idx] != inDev) {
          bestIdx = idx;
          break;
        }
      }
      if (bestIdx == std::numeric_limits<uint32_t>::max()) {
        bestIdx = 0;
      }
      map[inDev] = remOuts[bestIdx];
      remOuts.erase(remOuts.begin() + bestIdx);
      if (!remOuts.empty()) {
        cursor %= remOuts.size();
      }
    }
  }
  return map;
}

uint64_t OpticalOcs::ComputeMapScore(
    const std::vector<uint32_t>& map,
    const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights) const {
  if (map.empty() || weights.empty()) {
    return 0;
  }
  uint64_t score = 0;
  for (const auto& kv : weights) {
    const uint32_t inDev = kv.first.first;
    const uint32_t outDev = kv.first.second;
    if (inDev < map.size() && map[inDev] == outDev) {
      score += kv.second;
    }
  }
  return score;
}

bool OpticalOcs::IsSamePortMap(const std::vector<uint32_t>& lhs,
                               const std::vector<uint32_t>& rhs) const {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

std::pair<uint32_t, uint32_t> OpticalOcs::SelectDrainTargetPair(
    const std::map<std::pair<uint32_t, uint32_t>, uint64_t>& weights) const {
  std::pair<uint32_t, uint32_t> best = std::make_pair(0u, 0u);
  uint32_t bestAge = 0;
  uint64_t bestWeight = 0;
  uint64_t bestBytes = 0;
  bool found = false;

  for (const auto& kv : m_pendingQueues) {
    if (kv.second.empty()) {
      continue;
    }
    const std::pair<uint32_t, uint32_t>& key = kv.first;
    const uint32_t age =
        (m_pairAgeEpochs.count(key) == 0) ? 0 : m_pairAgeEpochs.at(key);
    const uint64_t weight =
        (weights.count(key) == 0) ? 0 : weights.at(key);
    const uint64_t bytes = PendingBytesForPair(key.first, key.second);
    if (!found || age > bestAge || (age == bestAge && weight > bestWeight) ||
        (age == bestAge && weight == bestWeight && bytes > bestBytes)) {
      best = key;
      bestAge = age;
      bestWeight = weight;
      bestBytes = bytes;
      found = true;
    }
  }

  if (!found) {
    return std::make_pair(0u, 0u);
  }
  return best;
}

void OpticalOcs::MaybeLogStatus(const char* reason, bool force) {
  if (m_statusLogIntervalNs == 0 && !force) {
    return;
  }
  const Time now = Simulator::Now();
  if (!force && m_statusLogIntervalNs > 0 && m_lastStatusLogTs != NanoSeconds(0) &&
      (now - m_lastStatusLogTs) < NanoSeconds(m_statusLogIntervalNs)) {
    return;
  }
  m_lastStatusLogTs = now;

  const uint64_t mappedPending = CountMappedPendingPackets();
  const uint64_t unresolvedPending = m_unresolvedPending.size();
  std::cout << "[OpticalOcs::status] time=" << now.GetTimeStep()
            << ", node=" << GetId()
            << ", reason=" << (reason == nullptr ? "unknown" : reason)
            << ", buffered_total=" << m_pendingPackets
            << ", mapped_pending=" << mappedPending
            << ", unresolved_pending=" << unresolvedPending
            << ", rx=" << m_totalRxPackets
            << ", fast_path=" << m_totalFastPathPackets
            << ", mapped_enq=" << m_totalMappedEnqueuePackets
            << ", unresolved_enq=" << m_totalUnresolvedEnqueuePackets
            << ", pending_deq=" << m_totalPendingDequeuedPackets
            << ", unresolved_moved=" << m_totalUnresolvedMovedPackets
            << ", route_miss=" << m_totalRouteMissPackets
            << ", port_mismatch_block=" << m_totalPortMismatchBlocks
            << ", reconfig_block=" << m_totalReconfigBlocks
            << ", send_fail=" << m_totalSendFailPackets
            << ", drain_calls=" << m_totalDrainCalls
            << ", drain_no_progress=" << m_totalDrainNoProgress
            << ", drain_no_progress_streak=" << m_consecutiveDrainNoProgress
            << ", epoch_no_progress=" << m_noProgressEpochs
            << ", drain_mode=" << (m_drainModeActive ? 1 : 0)
            << ", drain_target_in=" << m_drainTargetPair.first
            << ", drain_target_out=" << m_drainTargetPair.second
            << std::endl;
}

void OpticalOcs::ScheduleDrainPending(Time delay) {
  if (!HasBufferedPackets() || !m_drainPendingEvent.IsExpired()) {
    return;
  }
  if (delay.IsZero()) {
    m_drainPendingEvent = Simulator::ScheduleNow(&OpticalOcs::DrainPending, this);
  } else {
    m_drainPendingEvent = Simulator::Schedule(delay, &OpticalOcs::DrainPending, this);
  }
}

void OpticalOcs::DrainPending() {
  // 当前 drain 事件开始执行后，立即清空 event id，避免后续唤醒被误判为“已有事件”。
  m_drainPendingEvent = EventId();
  ++m_totalDrainCalls;
  if (!HasBufferedPackets()) {
    m_consecutiveDrainNoProgress = 0;
    return;
  }

  if (IsReconfigurationActive()) {
    const Time now = Simulator::Now();
    const Time delay = (m_reconfigEndTs > now) ? (m_reconfigEndTs - now)
                                               : NanoSeconds(1);
    m_drainPendingEvent = Simulator::Schedule(delay, &OpticalOcs::DrainPending, this);
    return;
  }

  const uint32_t budgetCap = std::max<uint32_t>(1, m_pendingDrainBurst);
  uint32_t budget = budgetCap;
  bool madeProgress = false;

  if (!m_unresolvedPending.empty() && budget > 0) {
    uint32_t unresolvedBudget = budget;
    if (!m_pendingQueues.empty() && unresolvedBudget > 1) {
      unresolvedBudget /= 2;
    }
    unresolvedBudget = std::min<uint32_t>(
        unresolvedBudget, static_cast<uint32_t>(m_unresolvedPending.size()));
    for (uint32_t i = 0; i < unresolvedBudget; ++i) {
      UnresolvedPacket unresolved = m_unresolvedPending.front();
      m_unresolvedPending.pop_front();
      if (!TryMoveUnresolvedToMapped(unresolved)) {
        m_unresolvedPending.push_back(unresolved);
      } else {
        madeProgress = true;
      }
    }
    budget -= unresolvedBudget;
  }

  std::vector<std::pair<uint32_t, uint32_t>> orderedKeys;
  orderedKeys.reserve(m_pendingQueues.size());
  for (const auto& kv : m_pendingQueues) {
    if (!kv.second.empty()) {
      orderedKeys.push_back(kv.first);
    }
  }
  std::sort(orderedKeys.begin(), orderedKeys.end(),
            [this](const std::pair<uint32_t, uint32_t>& a,
                   const std::pair<uint32_t, uint32_t>& b) {
              const uint32_t ageA =
                  (m_pairAgeEpochs.count(a) == 0) ? 0 : m_pairAgeEpochs.at(a);
              const uint32_t ageB =
                  (m_pairAgeEpochs.count(b) == 0) ? 0 : m_pairAgeEpochs.at(b);
              if (ageA != ageB) {
                return ageA > ageB;
              }
              const uint64_t bytesA =
                  (m_pendingBytes.count(a) == 0) ? 0 : m_pendingBytes.at(a);
              const uint64_t bytesB =
                  (m_pendingBytes.count(b) == 0) ? 0 : m_pendingBytes.at(b);
              if (bytesA != bytesB) {
                return bytesA > bytesB;
              }
              return a < b;
            });

  for (const auto& key : orderedKeys) {
    if (budget == 0) {
      break;
    }
    auto it = m_pendingQueues.find(key);
    if (it == m_pendingQueues.end() || it->second.empty()) {
      continue;
    }
    const uint32_t inDev = key.first;
    const uint32_t outDev = key.second;
    if (!CanForwardNow(inDev, outDev)) {
      continue;
    }

    std::deque<PendingPacket>& q = it->second;
    while (!q.empty() && budget > 0) {
      PendingPacket pending = q.front();
      q.pop_front();
      if (SendOnPort(outDev, pending.packet, pending.header)) {
        if (m_pendingPackets > 0) {
          --m_pendingPackets;
        }
        ConsumePendingBytes(key, pending.packet->GetSize());
        ++m_totalPendingDequeuedPackets;
        --budget;
        madeProgress = true;
      } else {
        // 端口设备队列满：保持原顺序回退到队首，后续重试。
        q.push_front(pending);
        break;
      }
    }

    if (q.empty()) {
      m_pendingBytes.erase(key);
      m_pairAgeEpochs.erase(key);
      m_pendingQueues.erase(it);
    }
  }

  if (HasBufferedPackets()) {
    if (madeProgress) {
      m_consecutiveDrainNoProgress = 0;
    } else {
      ++m_totalDrainNoProgress;
      ++m_consecutiveDrainNoProgress;
      // 中文注释：仅在长时间连续无进展时输出一次摘要，避免刷屏。
      if (m_statusLogIntervalNs > 0 &&
          (m_consecutiveDrainNoProgress % 5000 == 0)) {
        MaybeLogStatus("drain_no_progress", false);
      }
    }
    uint32_t retryNs = 1;
    if (!madeProgress) {
      if (m_enableEpochReconfiguration && m_epochNs > 0) {
        // 无进展时做退避：避免在极小 epoch(例如 200ns) 下产生事件风暴。
        retryNs = std::max<uint32_t>(10000, m_epochNs * 50);
      } else {
        retryNs = 10000;
      }
    }
    m_drainPendingEvent =
        Simulator::Schedule(NanoSeconds(retryNs), &OpticalOcs::DrainPending, this);
  }
}

void OpticalOcs::TryForwardOrQueue(uint32_t inDev, uint32_t outDev, Ptr<Packet> p,
                                   CustomHeader ch) {
  if (CanForwardNow(inDev, outDev) && SendOnPort(outDev, p, ch)) {
    ++m_totalFastPathPackets;
    return;
  }

  if (!EnqueuePending(inDev, outDev, p, ch, true)) {
    // 若当前候选出口非法，先进入 unresolved 队列，等待路由或映射稳定后重试。
    BufferUnresolved(inDev, p, ch, true);
    ++m_totalRouteMissPackets;
  }
  ScheduleDrainPending();
}

std::vector<uint32_t> OpticalOcs::GetCandidatePorts() const {
  std::vector<uint32_t> ports;
  const uint32_t nDev = GetNDevices();
  if (nDev <= 1) {
    return ports;
  }

  const uint32_t discovered = nDev - 1;
  const uint32_t limit = (m_numPorts == 0) ? discovered : std::min(m_numPorts, discovered);
  for (uint32_t i = 1; i <= limit; ++i) {
    ports.push_back(i);
  }
  return ports;
}

bool OpticalOcs::BuildMapFromSpec(const std::vector<uint32_t>& ports,
                                  std::vector<uint32_t>* outMap) const {
  const std::string spec = Trim(m_portMappingSpec);
  if (outMap == nullptr || ports.empty() || spec.empty() || spec == "auto" ||
      spec == "AUTO") {
    return false;
  }

  std::set<uint32_t> validPorts(ports.begin(), ports.end());
  std::set<uint32_t> usedOutputs;
  std::vector<uint32_t> map(GetNDevices(), std::numeric_limits<uint32_t>::max());

  std::string normalized = spec;
  for (char& c : normalized) {
    if (c == ';') {
      c = ',';
    }
  }

  std::stringstream ss(normalized);
  std::string token;
  uint32_t parsed = 0;

  while (std::getline(ss, token, ',')) {
    token = Trim(token);
    if (token.empty()) {
      continue;
    }

    size_t pos = token.find("->");
    size_t sepLen = 2;
    if (pos == std::string::npos) {
      pos = token.find(':');
      sepLen = 1;
    }
    if (pos == std::string::npos) {
      return false;
    }

    std::string lhs = Trim(token.substr(0, pos));
    std::string rhs = Trim(token.substr(pos + sepLen));
    if (lhs.empty() || rhs.empty()) {
      return false;
    }

    char* end = nullptr;
    unsigned long inPortRaw = std::strtoul(lhs.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
      return false;
    }
    end = nullptr;
    unsigned long outPortRaw = std::strtoul(rhs.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
      return false;
    }

    const uint32_t inPort = static_cast<uint32_t>(inPortRaw);
    const uint32_t outPort = static_cast<uint32_t>(outPortRaw);
    if (validPorts.count(inPort) == 0 || validPorts.count(outPort) == 0) {
      return false;
    }
    if (inPort >= map.size() || map[inPort] != std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    if (usedOutputs.count(outPort) != 0) {
      return false;
    }

    map[inPort] = outPort;
    usedOutputs.insert(outPort);
    ++parsed;
  }

  if (parsed != ports.size()) {
    return false;
  }
  for (uint32_t inPort : ports) {
    if (inPort >= map.size() || map[inPort] == std::numeric_limits<uint32_t>::max()) {
      return false;
    }
  }

  *outMap = map;
  return true;
}

std::vector<uint32_t> OpticalOcs::BuildCyclicMap(const std::vector<uint32_t>& ports,
                                                 uint32_t rotation) const {
  std::vector<uint32_t> map(GetNDevices(), std::numeric_limits<uint32_t>::max());
  if (ports.empty()) {
    return map;
  }
  if (ports.size() == 1) {
    map[ports[0]] = ports[0];
    return map;
  }

  const uint32_t n = static_cast<uint32_t>(ports.size());
  uint32_t shift = rotation % n;
  if (shift == 0) {
    shift = 1;
  }

  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t inPort = ports[i];
    const uint32_t outPort = ports[(i + shift) % n];
    map[inPort] = outPort;
  }
  return map;
}

void OpticalOcs::EnsurePortMapReady() {
  if (m_portMapReady) {
    if (m_enableEpochReconfiguration && m_epochNs > 0 && m_epochEvent.IsExpired()) {
      m_epochEvent = Simulator::Schedule(NanoSeconds(m_epochNs),
                                         &OpticalOcs::TriggerEpochReconfiguration,
                                         this);
    }
    return;
  }

  const std::vector<uint32_t> ports = GetCandidatePorts();
  if (ports.empty()) {
    return;
  }

  std::vector<uint32_t> newMap;
  if (!BuildMapFromSpec(ports, &newMap)) {
    const auto weights = BuildDemandWeights(ports);
    newMap = BuildDemandAwareMap(ports, weights, false, 0, 0);
    if (newMap.empty()) {
      newMap = BuildCyclicMap(ports, m_rotationOffset);
    }
  }

  m_portMap.swap(newMap);
  m_portMapReady = true;
  m_epochsSinceMapChange = 0;
  m_lastEpochDequeuedPackets = m_totalPendingDequeuedPackets;

  if (m_enableEpochReconfiguration && m_epochNs > 0 && m_epochEvent.IsExpired()) {
    m_epochEvent = Simulator::Schedule(NanoSeconds(m_epochNs),
                                       &OpticalOcs::TriggerEpochReconfiguration,
                                       this);
  }
}

bool OpticalOcs::IsReconfigurationActive() const {
  return m_reconfiguring && Simulator::Now() < m_reconfigEndTs;
}

void OpticalOcs::TriggerEpochReconfiguration() {
  if (!m_enableEpochReconfiguration || m_epochNs == 0) {
    return;
  }

  EnsurePortMapReady();
  const std::vector<uint32_t> ports = GetCandidatePorts();
  if (ports.empty()) {
    m_epochEvent = Simulator::Schedule(NanoSeconds(m_epochNs),
                                       &OpticalOcs::TriggerEpochReconfiguration,
                                       this);
    return;
  }

  UpdatePendingPairAges();
  const bool hasBuffered = HasBufferedPackets();
  const bool progressed = m_totalPendingDequeuedPackets > m_lastEpochDequeuedPackets;
  if (hasBuffered) {
    m_noProgressEpochs = progressed ? 0 : (m_noProgressEpochs + 1);
  } else {
    m_noProgressEpochs = 0;
  }
  m_lastEpochDequeuedPackets = m_totalPendingDequeuedPackets;

  const auto weights = BuildDemandWeights(ports);

  if (m_drainModeActive &&
      !HasPendingForPair(m_drainTargetPair.first, m_drainTargetPair.second)) {
    if (hasBuffered) {
      m_drainTargetPair = SelectDrainTargetPair(weights);
      if (m_drainTargetPair.first == 0 && m_drainTargetPair.second == 0) {
        m_drainModeActive = false;
      }
    } else {
      m_drainModeActive = false;
    }
    if (!m_drainModeActive) {
      m_drainTargetPair = std::make_pair(0u, 0u);
      MaybeLogStatus("exit_drain_mode", false);
    }
  }

  if (!m_drainModeActive && hasBuffered &&
      m_noProgressEpochs >= m_noProgressEpochThreshold) {
    const std::pair<uint32_t, uint32_t> target = SelectDrainTargetPair(weights);
    if (target.first != 0 || target.second != 0) {
      m_drainModeActive = true;
      m_drainTargetPair = target;
      MaybeLogStatus("enter_drain_mode", false);
      ScheduleDrainPending();
    }
  }

  if (!m_reconfiguring) {
    std::vector<uint32_t> nextMap;
    const bool hasStaticMap = BuildMapFromSpec(ports, &nextMap);
    if (!hasStaticMap) {
      const bool pinDrainPair =
          m_drainModeActive &&
          HasPendingForPair(m_drainTargetPair.first, m_drainTargetPair.second);
      nextMap = BuildDemandAwareMap(ports, weights, pinDrainPair,
                                    m_drainTargetPair.first,
                                    m_drainTargetPair.second);
      if (nextMap.empty()) {
        if (ports.size() > 1) {
          ++m_rotationOffset;
          if (m_rotationOffset >= ports.size()) {
            m_rotationOffset = 1;
          }
        } else {
          m_rotationOffset = 0;
        }
        nextMap = BuildCyclicMap(ports, m_rotationOffset);
      }
    }

    if (nextMap.empty()) {
      ++m_epochsSinceMapChange;
    } else {
      const bool mapChanged = !IsSamePortMap(nextMap, m_portMap);
      const bool canSwitch =
          mapChanged &&
          (m_drainModeActive || m_epochsSinceMapChange >= m_minHoldEpochs);

      bool gainSatisfied = true;
      if (!hasStaticMap && canSwitch && m_mapChangeGainThresholdBytes > 0) {
        const uint64_t currentScore = ComputeMapScore(m_portMap, weights);
        const uint64_t nextScore = ComputeMapScore(nextMap, weights);
        gainSatisfied = nextScore >=
                        currentScore +
                            static_cast<uint64_t>(m_mapChangeGainThresholdBytes);
      }

      if (canSwitch && gainSatisfied) {
        m_pendingPortMap.swap(nextMap);
        const uint64_t totalNs =
            static_cast<uint64_t>(m_reconfigurationDelayNs) +
            static_cast<uint64_t>(m_guardTimeNs);
        const Time hold = NanoSeconds(totalNs);
        m_reconfiguring = true;
        m_reconfigEndTs = Simulator::Now() + hold;
        if (hold.IsZero()) {
          FinishReconfiguration();
        } else {
          m_reconfigDoneEvent = Simulator::Schedule(
              hold, &OpticalOcs::FinishReconfiguration, this);
        }
      } else {
        ++m_epochsSinceMapChange;
      }
    }
  }

  uint32_t nextEpochNs = m_epochNs;
  if (m_noProgressEpochs >= m_noProgressEpochThreshold) {
    // 无进展阶段降低 epoch 频率，减少空转重构开销。
    nextEpochNs = std::max<uint32_t>(5000, m_epochNs);
  }
  m_epochEvent = Simulator::Schedule(NanoSeconds(nextEpochNs),
                                     &OpticalOcs::TriggerEpochReconfiguration,
                                     this);
  ScheduleDrainPending();
}

void OpticalOcs::FinishReconfiguration() {
  if (!m_pendingPortMap.empty()) {
    const bool changed = !IsSamePortMap(m_portMap, m_pendingPortMap);
    m_portMap.swap(m_pendingPortMap);
    m_pendingPortMap.clear();
    if (changed) {
      m_epochsSinceMapChange = 0;
    }
  }
  m_reconfiguring = false;
  m_reconfigEndTs = Simulator::Now();
  ScheduleDrainPending();
}

uint32_t OpticalOcs::SelectQueueIndex(const CustomHeader& ch) const {
  if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE ||
      (m_ackHighPrio && (ch.l3Prot == 0xFD || ch.l3Prot == 0xFC))) {
    return 0;
  }
  return (ch.l3Prot == 0x06 ? 1 : ch.udp.pg);
}

bool OpticalOcs::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                                         CustomHeader& ch) {
  ++m_totalRxPackets;
  const uint32_t inDev = device->GetIfIndex();
  const int desiredOut = GetOutDev(inDev, ch);
  if (desiredOut < 0) {
    ++m_totalRouteMissPackets;
    BufferUnresolved(inDev, packet, ch, true);
    MaybeLogStatus("route_miss", false);
    ScheduleDrainPending();
    return true;
  }
  TryForwardOrQueue(inDev, static_cast<uint32_t>(desiredOut), packet, ch);
  return true;
}

void OpticalOcs::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex,
                                     Ptr<Packet> p) {
  (void)qIndex;
  if (ifIndex < pCnt) {
    m_txBytes[ifIndex] += p->GetSize();
  }
  // 设备队列有出队后立即尝试继续排空 pending，可降低尾流等待。
  ScheduleDrainPending();
}

int OpticalOcs::logres_shift(int b, int l) {
  static int data[] = {0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
                       5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
  return l - data[b];
}

int OpticalOcs::log2apprx(int x, int b, int m, int l) {
  int x0 = x;
  int msb = int(log2(x)) + 1;
  if (msb > m) {
    x = (x >> (msb - m) << (msb - m));
    int mask = (1 << (msb - m)) - 1;
    if ((x0 & mask) > (rand() & mask))
      x += 1 << (msb - m);
  }
  return int(log2(x) * (1 << logres_shift(b, l)));
}

void OpticalOcs::PrintSwitchQlen(FILE* qlen_output) {
  const uint32_t nDev = GetNDevices();
  for (uint32_t i = 1; i < nDev; ++i) {
    Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(GetDevice(i));
    if (dev == nullptr || dev->GetQueue() == nullptr) {
      continue;
    }

    const uint64_t portLen = dev->GetQueue()->GetNBytesTotal();
    if (portLen == last_port_qlen[i]) {
      continue;
    }

    for (uint32_t q = 0; q < qCnt; ++q) {
      fprintf(qlen_output, "%lu, %u, %u, %u, %u, %lu\n",
              Simulator::Now().GetTimeStep(), m_id, i, q,
              dev->GetQueue()->GetNBytes(q), portLen);
      fflush(qlen_output);
    }
    last_port_qlen[i] = portLen;
  }
}

void OpticalOcs::PrintSwitchBw(FILE* bw_output, uint32_t bw_mon_interval) {
  const uint32_t nDev = GetNDevices();
  for (uint32_t i = 1; i < nDev; ++i) {
    if (last_txBytes[i] == m_txBytes[i]) {
      continue;
    }
    double bw = (m_txBytes[i] - last_txBytes[i]) * 8 * 1e6 / bw_mon_interval;
    bw = bw / 1e9;
    fprintf(bw_output, "%lu, %u, %u, %f\n", Simulator::Now().GetTimeStep(), m_id,
            i, bw);
    fflush(bw_output);
    last_txBytes[i] = m_txBytes[i];
  }
}

} // namespace ns3
