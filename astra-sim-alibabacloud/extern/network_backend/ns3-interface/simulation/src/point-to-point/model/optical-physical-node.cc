#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "optical-physical-node.h"
#include "qbb-net-device.h"
#include "ppp-header.h"
#include "ns3/int-header.h"
#include "ns3/simulator.h"
#include <cstdint>

namespace ns3 {

TypeId OpticalPhysicalNode::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::OpticalPhysicalNode")
          .SetParent<Node>()
          .AddConstructor<OpticalPhysicalNode>()
          .AddAttribute("CcMode", "CC mode.", UintegerValue(0),
                        MakeUintegerAccessor(&OpticalPhysicalNode::m_ccMode),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("MaxRtt", "Max Rtt of the network",
                        UintegerValue(9000),
                        MakeUintegerAccessor(&OpticalPhysicalNode::m_maxRtt),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("AckHighPrio", "Set high priority for ACK/NACK or not",
                        UintegerValue(0),
                        MakeUintegerAccessor(&OpticalPhysicalNode::m_ackHighPrio),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "OpticalOeoDelayNs",
              "中文注释：O/E/O 固定处理延迟（ns）。",
              UintegerValue(40),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_oeoDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalFecBaseDelayNs",
              "中文注释：FEC 固定处理延迟（ns）。",
              UintegerValue(20),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_fecBaseDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalFecPerKbDelayNs",
              "中文注释：FEC 随包长增加的延迟（每 KiB 的 ns）。",
              UintegerValue(1),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_fecPerKbDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalGuardIntervalNs",
              "中文注释：光层守护间隔（ns）。",
              UintegerValue(8),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_guardIntervalNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalWssReconfigDelayNs",
              "中文注释：输出端口切换时的简化重配延迟（ns）。",
              UintegerValue(100),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_wssReconfigDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalAdmissionScalePermille",
              "中文注释：准入折减系数（千分比）。",
              UintegerValue(800),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_opticalAdmissionScalePermille),
              MakeUintegerChecker<uint32_t>(1, 1000))
          .AddAttribute(
              "OpticalMinAdmissionBytes",
              "中文注释：准入最小记账字节。",
              UintegerValue(64),
              MakeUintegerAccessor(&OpticalPhysicalNode::m_opticalMinAdmissionBytes),
              MakeUintegerChecker<uint32_t>(1));
  return tid;
}

OpticalPhysicalNode::OpticalPhysicalNode() {
  m_ecmpSeed = m_id;
  // 中文注释：保持为 3，便于未来直接替换现有光模块节点路径。
  m_node_type = 3;
  m_mmu = CreateObject<SwitchMmu>();

  for (uint32_t i = 0; i < pCnt; i++) {
    for (uint32_t j = 0; j < pCnt; j++) {
      for (uint32_t k = 0; k < qCnt; k++) {
        m_bytes[i][j][k] = 0;
      }
    }
  }

  for (uint32_t i = 0; i < pCnt; i++) {
    m_txBytes[i] = 0;
    last_txBytes[i] = 0;
    last_port_qlen[i] = 0;
  }

  for (uint32_t i = 0; i < pCnt; i++) {
    m_lastPktSize[i] = 0;
    m_lastPktTs[i] = 0;
    m_u[i] = 0;
  }

  m_lastForwardOutDev = UINT32_MAX;
}

int OpticalPhysicalNode::GetOutDev(Ptr<const Packet> p, CustomHeader& ch) {
  (void)p;
  auto entry = m_rtTable.find(ch.dip);
  if (entry == m_rtTable.end()) {
    return -1;
  }

  auto& nexthops = entry->second;
  union {
    uint8_t u8[4 + 4 + 2 + 2];
    uint32_t u32[3];
  } buf;

  buf.u32[0] = ch.sip;
  buf.u32[1] = ch.dip;
  if (ch.l3Prot == 0x6) {
    buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
  } else if (ch.l3Prot == 0x11) {
    buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
  } else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD) {
    buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);
  }

  uint32_t idx = EcmpHash(buf.u8, 12, m_ecmpSeed) % nexthops.size();
  return nexthops[idx];
}

void OpticalPhysicalNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex) {
  Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
  if (m_mmu->CheckShouldPause(inDev, qIndex)) {
    device->SendPfc(qIndex, 0);
    m_mmu->SetPause(inDev, qIndex);
  }
}

void OpticalPhysicalNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex) {
  Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
  if (m_mmu->CheckShouldResume(inDev, qIndex)) {
    device->SendPfc(qIndex, 1);
    m_mmu->SetResume(inDev, qIndex);
  }
}

uint32_t OpticalPhysicalNode::GetEffectiveAdmissionBytes(
    uint32_t packetBytes) const {
  uint64_t scaled = static_cast<uint64_t>(packetBytes) *
                    static_cast<uint64_t>(m_opticalAdmissionScalePermille);
  uint32_t effective = static_cast<uint32_t>((scaled + 999) / 1000);

  if (effective < m_opticalMinAdmissionBytes) {
    effective = m_opticalMinAdmissionBytes;
  }
  if (effective > packetBytes) {
    effective = packetBytes;
  }
  return effective == 0 ? 1 : effective;
}

Time OpticalPhysicalNode::GetOpticalPhysicalDelay(uint32_t outDev,
                                                  uint32_t qIndex,
                                                  uint32_t packetBytes) const {
  // 中文注释：控制报文优先快速通过，避免把控制面时延放大。
  if (qIndex == 0) {
    return NanoSeconds(0);
  }

  uint64_t packetKb = (packetBytes + 1023) / 1024;
  uint64_t delayNs = static_cast<uint64_t>(m_oeoDelayNs) +
                     static_cast<uint64_t>(m_fecBaseDelayNs) +
                     static_cast<uint64_t>(m_fecPerKbDelayNs) * packetKb +
                     static_cast<uint64_t>(m_guardIntervalNs);

  // 中文注释：输出端口切换时叠加简化 WSS 重配惩罚。
  if (m_lastForwardOutDev != UINT32_MAX && m_lastForwardOutDev != outDev) {
    delayNs += static_cast<uint64_t>(m_wssReconfigDelayNs);
  }

  return NanoSeconds(delayNs);
}

void OpticalPhysicalNode::DoOpticalPhysicalForward(uint32_t inDev,
                                                   uint32_t outDev,
                                                   uint32_t qIndex,
                                                   uint32_t effectiveBytes,
                                                   Ptr<Packet> p,
                                                   CustomHeader ch) {
  if (!m_devices[outDev]->IsLinkUp()) {
    // 中文注释：链路故障时回滚准入占用，避免 MMU 记账泄漏。
    if (qIndex != 0) {
      m_mmu->RemoveFromIngressAdmission(inDev, qIndex, effectiveBytes);
      m_mmu->RemoveFromEgressAdmission(outDev, qIndex, effectiveBytes);
      if (m_bytes[inDev][outDev][qIndex] >= effectiveBytes) {
        m_bytes[inDev][outDev][qIndex] -= effectiveBytes;
      } else {
        m_bytes[inDev][outDev][qIndex] = 0;
      }
    }
    return;
  }

  m_devices[outDev]->SwitchSend(qIndex, p, ch);
}

void OpticalPhysicalNode::RetrySendToDev(Ptr<Packet> p, CustomHeader ch) {
  SendToDev(p, ch);
}

void OpticalPhysicalNode::SendToDev(Ptr<Packet> p, CustomHeader& ch) {
  int idx = GetOutDev(p, ch);
  if (idx < 0) {
    return;
  }

  uint32_t qIndex;
  if (ch.l3Prot == 0xFF || ch.l3Prot == 0xFE ||
      (m_ackHighPrio && (ch.l3Prot == 0xFD || ch.l3Prot == 0xFC))) {
    qIndex = 0;
  } else {
    qIndex = (ch.l3Prot == 0x06 ? 1 : ch.udp.pg);
  }

  FlowIdTag t;
  p->PeekPacketTag(t);
  uint32_t inDev = t.GetFlowId();
  uint32_t effectiveBytes = 0;

  if (qIndex != 0) {
    effectiveBytes = GetEffectiveAdmissionBytes(p->GetSize());
    bool ingressOk = m_mmu->CheckIngressAdmission(inDev, qIndex, effectiveBytes);
    bool egressOk = m_mmu->CheckEgressAdmission(static_cast<uint32_t>(idx), qIndex,
                                                 effectiveBytes);
    if (!ingressOk || !egressOk) {
      // 中文注释：保持无损语义，短延迟重试，不直接丢包。
      CheckAndSendPfc(inDev, qIndex);
      Simulator::Schedule(NanoSeconds(50), &OpticalPhysicalNode::RetrySendToDev,
                          this, p, ch);
      return;
    }

    m_mmu->UpdateIngressAdmission(inDev, qIndex, effectiveBytes);
    m_mmu->UpdateEgressAdmission(static_cast<uint32_t>(idx), qIndex,
                                 effectiveBytes);
    CheckAndSendPfc(inDev, qIndex);
    m_bytes[inDev][static_cast<uint32_t>(idx)][qIndex] += effectiveBytes;
  }

  Time delay = GetOpticalPhysicalDelay(static_cast<uint32_t>(idx), qIndex,
                                       p->GetSize());
  m_lastForwardOutDev = static_cast<uint32_t>(idx);

  if (delay == NanoSeconds(0)) {
    DoOpticalPhysicalForward(inDev, static_cast<uint32_t>(idx), qIndex,
                             effectiveBytes, p, ch);
    return;
  }

  Simulator::Schedule(delay, &OpticalPhysicalNode::DoOpticalPhysicalForward,
                      this, inDev, static_cast<uint32_t>(idx), qIndex,
                      effectiveBytes, p, ch);
}

uint32_t OpticalPhysicalNode::EcmpHash(const uint8_t* key, size_t len,
                                       uint32_t seed) {
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

void OpticalPhysicalNode::SetEcmpSeed(uint32_t seed) { m_ecmpSeed = seed; }

void OpticalPhysicalNode::AddTableEntry(Ipv4Address& dstAddr,
                                        uint32_t intf_idx) {
  uint32_t dip = dstAddr.Get();
  m_rtTable[dip].push_back(intf_idx);
}

void OpticalPhysicalNode::ClearTable() { m_rtTable.clear(); }

bool OpticalPhysicalNode::SwitchReceiveFromDevice(Ptr<NetDevice> device,
                                                  Ptr<Packet> packet,
                                                  CustomHeader& ch) {
  (void)device;
  SendToDev(packet, ch);
  return true;
}

void OpticalPhysicalNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex,
                                              Ptr<Packet> p) {
  FlowIdTag t;
  p->PeekPacketTag(t);

  if (qIndex != 0) {
    uint32_t inDev = t.GetFlowId();
    uint32_t effectiveBytes = GetEffectiveAdmissionBytes(p->GetSize());

    m_mmu->RemoveFromIngressAdmission(inDev, qIndex, effectiveBytes);
    m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, effectiveBytes);

    if (m_bytes[inDev][ifIndex][qIndex] >= effectiveBytes) {
      m_bytes[inDev][ifIndex][qIndex] -= effectiveBytes;
    } else {
      m_bytes[inDev][ifIndex][qIndex] = 0;
    }

    CheckAndSendResume(inDev, qIndex);
  }

  m_txBytes[ifIndex] += p->GetSize();
  m_lastPktSize[ifIndex] = p->GetSize();
  m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();
}

void OpticalPhysicalNode::PrintSwitchQlen(FILE* qlen_output) {
  uint32_t n_dev = this->GetNDevices();
  for (uint32_t i = 1; i < n_dev; ++i) {
    uint64_t port_len = 0;
    for (uint32_t j = 0; j < qCnt; ++j) {
      port_len += m_mmu->egress_bytes[i][j];
    }
    if (port_len == last_port_qlen[i]) {
      continue;
    }
    for (uint32_t j = 0; j < qCnt; ++j) {
      fprintf(qlen_output, "%lu, %u, %u, %u, %u, %lu\n",
              Simulator::Now().GetTimeStep(), m_id, i, j,
              m_mmu->egress_bytes[i][j], port_len);
      fflush(qlen_output);
    }
    last_port_qlen[i] = port_len;
  }
}

void OpticalPhysicalNode::PrintSwitchBw(FILE* bw_output,
                                        uint32_t bw_mon_interval) {
  uint32_t n_dev = this->GetNDevices();
  for (uint32_t i = 1; i < n_dev; ++i) {
    if (last_txBytes[i] == m_txBytes[i]) {
      continue;
    }
    double bw = (m_txBytes[i] - last_txBytes[i]) * 8 * 1e6 / bw_mon_interval;
    bw = bw * 1.0 / 1e9; // Gbps
    fprintf(bw_output, "%lu, %u, %u, %f\n", Simulator::Now().GetTimeStep(),
            m_id, i, bw);
    fflush(bw_output);
    last_txBytes[i] = m_txBytes[i];
  }
}

int OpticalPhysicalNode::logres_shift(int b, int l) {
  (void)b;
  (void)l;
  return 0;
}

int OpticalPhysicalNode::log2apprx(int x, int b, int m, int l) {
  (void)x;
  (void)b;
  (void)m;
  (void)l;
  return 0;
}

} // namespace ns3
