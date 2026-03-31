#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "optical-module-node.h"
#include "qbb-net-device.h"
#include "ppp-header.h"
#include "ns3/int-header.h"
#include "ns3/simulator.h"
#include <cmath>

namespace ns3 {

TypeId OpticalModuleNode::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::OpticalModuleNode")
          .SetParent<Node>()
          .AddConstructor<OpticalModuleNode>()
          .AddAttribute("CcMode", "CC mode.", UintegerValue(0),
                        MakeUintegerAccessor(&OpticalModuleNode::m_ccMode),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("MaxRtt", "Max Rtt of the network",
                        UintegerValue(9000),
                        MakeUintegerAccessor(&OpticalModuleNode::m_maxRtt),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("AckHighPrio", "Set high priority for ACK/NACK or not",
                        UintegerValue(0),
                        MakeUintegerAccessor(&OpticalModuleNode::m_ackHighPrio),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "OpticalLatencyNs",
              "新增：光模块固定处理时延（纳秒），用于模拟快速 O/E/O 处理开销。",
              UintegerValue(80),
              MakeUintegerAccessor(&OpticalModuleNode::m_opticalLatencyNs),
              MakeUintegerChecker<uint32_t>(1))
          .AddAttribute(
              "OpticalAdmissionScalePermille",
              "新增：光模块准入折减系数（千分比）。默认 850 表示仅按 85% 字节记账，"
              "",
              UintegerValue(850),
              MakeUintegerAccessor(
                  &OpticalModuleNode::m_opticalAdmissionScalePermille),
              MakeUintegerChecker<uint32_t>(1, 1000))
          .AddAttribute(
              "OpticalMinAdmissionBytes",
              "新增：准入最小记账字节，防止小包折减后记账为 0。",
              UintegerValue(64),
              MakeUintegerAccessor(&OpticalModuleNode::m_opticalMinAdmissionBytes),
              MakeUintegerChecker<uint32_t>(1));
  return tid;
}

OpticalModuleNode::OpticalModuleNode() {
  m_ecmpSeed = m_id;
  m_node_type = 3; // 新增：约定 node_type=3 表示光模块节点。
  m_mmu = CreateObject<SwitchMmu>();
  for (uint32_t i = 0; i < pCnt; i++)
    for (uint32_t j = 0; j < pCnt; j++)
      for (uint32_t k = 0; k < qCnt; k++)
        m_bytes[i][j][k] = 0;
  for (uint32_t i = 0; i < pCnt; i++) {
    m_txBytes[i] = 0;
    last_txBytes[i] = 0;
    last_port_qlen[i] = 0;
  }
  for (uint32_t i = 0; i < pCnt; i++)
    m_lastPktSize[i] = m_lastPktTs[i] = 0;
  for (uint32_t i = 0; i < pCnt; i++)
    m_u[i] = 0;
}

int OpticalModuleNode::GetOutDev(Ptr<const Packet> p, CustomHeader& ch) {
  (void)p;
  auto entry = m_rtTable.find(ch.dip);
  if (entry == m_rtTable.end())
    return -1;

  auto& nexthops = entry->second;
  union {
    uint8_t u8[4 + 4 + 2 + 2];
    uint32_t u32[3];
  } buf;
  buf.u32[0] = ch.sip;
  buf.u32[1] = ch.dip;
  if (ch.l3Prot == 0x6)
    buf.u32[2] = ch.tcp.sport | ((uint32_t)ch.tcp.dport << 16);
  else if (ch.l3Prot == 0x11)
    buf.u32[2] = ch.udp.sport | ((uint32_t)ch.udp.dport << 16);
  else if (ch.l3Prot == 0xFC || ch.l3Prot == 0xFD)
    buf.u32[2] = ch.ack.sport | ((uint32_t)ch.ack.dport << 16);

  uint32_t idx = EcmpHash(buf.u8, 12, m_ecmpSeed) % nexthops.size();
  return nexthops[idx];
}

void OpticalModuleNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex) {
  // 新增：光模块也按交换机语义发送 Pause，尽量维持无损转发。
  Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
  if (m_mmu->CheckShouldPause(inDev, qIndex)) {
    device->SendPfc(qIndex, 0);
    m_mmu->SetPause(inDev, qIndex);
  }
}

void OpticalModuleNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex) {
  // 新增：队列回落后及时发送 Resume，防止上游长期阻塞。
  Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
  if (m_mmu->CheckShouldResume(inDev, qIndex)) {
    device->SendPfc(qIndex, 1);
    m_mmu->SetResume(inDev, qIndex);
  }
}

uint32_t OpticalModuleNode::GetEffectiveAdmissionBytes(
    uint32_t packetBytes) const {
  // 新增：光模块按折减后的“等效字节”进行 MMU 准入与占用统计，模拟更高效缓存行为。
  uint64_t scaled = static_cast<uint64_t>(packetBytes) *
                    static_cast<uint64_t>(m_opticalAdmissionScalePermille);
  uint32_t effective = static_cast<uint32_t>((scaled + 999) / 1000);
  if (effective < m_opticalMinAdmissionBytes) {
    effective = m_opticalMinAdmissionBytes;
  }
  // 新增：上限不超过真实包大小，避免过度记账。
  if (effective > packetBytes) {
    effective = packetBytes;
  }
  return effective == 0 ? 1 : effective;
}

Time OpticalModuleNode::GetOpticalForwardDelay(uint32_t qIndex,
                                               uint32_t packetBytes) const {
  // 新增：控制平面高优先级报文不额外增加光处理时延，降低控制面抖动。
  if (qIndex == 0) {
    return NanoSeconds(0);
  }
  // 新增：固定小延迟 + 极小包长相关项，体现“有光模块处理开销但整体很小”。
  uint64_t serdesNs = packetBytes / 1024;
  return NanoSeconds(static_cast<uint64_t>(m_opticalLatencyNs) + serdesNs);
}

void OpticalModuleNode::DoOpticalForward(uint32_t inDev, uint32_t outDev,
                                         uint32_t qIndex, uint32_t effectiveBytes,
                                         Ptr<Packet> p, CustomHeader ch) {
  // 新增：若处理完成前链路 down，需要回滚之前占用，避免 MMU 统计泄漏。
  if (!m_devices[outDev]->IsLinkUp()) {
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

void OpticalModuleNode::RetrySendToDev(Ptr<Packet> p, CustomHeader ch) {
  // 新增：将值传递的 header 转回引用形参，复用现有 SendToDev 逻辑。
  SendToDev(p, ch);
}

void OpticalModuleNode::SendToDev(Ptr<Packet> p, CustomHeader& ch) {
  int idx = GetOutDev(p, ch);
  if (idx < 0)
    return; // Drop

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
    // 新增：对数据报文使用折减后的“等效字节”做准入，体现光模块更友好的拥塞行为。
    effectiveBytes = GetEffectiveAdmissionBytes(p->GetSize());
    bool ingressOk = m_mmu->CheckIngressAdmission(inDev, qIndex, effectiveBytes);
    bool egressOk = m_mmu->CheckEgressAdmission(idx, qIndex, effectiveBytes);
    if (!ingressOk || !egressOk) {
      // 新增：准入不通过时不丢包，短延迟重试，避免出现 streams_injected > streams_finished。
      // 说明：这保持“无损优先”语义，同时避免越过准入导致队列侧静默丢包。
      CheckAndSendPfc(inDev, qIndex);
      Simulator::Schedule(NanoSeconds(50), &OpticalModuleNode::RetrySendToDev,
                          this, p, ch);
      return;
    }
    m_mmu->UpdateIngressAdmission(inDev, qIndex, effectiveBytes);
    m_mmu->UpdateEgressAdmission(idx, qIndex, effectiveBytes);
    CheckAndSendPfc(inDev, qIndex);
    m_bytes[inDev][idx][qIndex] += effectiveBytes;
  }
  // 新增：光模块转发增加小固定处理时延，与纯电交换“立即转发”形成差异。
  Time opticalDelay = GetOpticalForwardDelay(qIndex, p->GetSize());
  if (opticalDelay == NanoSeconds(0)) {
    DoOpticalForward(inDev, static_cast<uint32_t>(idx), qIndex, effectiveBytes, p,
                     ch);
    return;
  }
  Simulator::Schedule(opticalDelay, &OpticalModuleNode::DoOpticalForward, this,
                      inDev, static_cast<uint32_t>(idx), qIndex, effectiveBytes,
                      p, ch);
}

uint32_t OpticalModuleNode::EcmpHash(const uint8_t* key, size_t len,
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

void OpticalModuleNode::SetEcmpSeed(uint32_t seed) { m_ecmpSeed = seed; }

void OpticalModuleNode::AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx) {
  uint32_t dip = dstAddr.Get();
  m_rtTable[dip].push_back(intf_idx);
}

void OpticalModuleNode::ClearTable() { m_rtTable.clear(); }

bool OpticalModuleNode::SwitchReceiveFromDevice(Ptr<NetDevice> device,
                                                Ptr<Packet> packet,
                                                CustomHeader& ch) {
  SendToDev(packet, ch);
  return true;
}

void OpticalModuleNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex,
                                            Ptr<Packet> p) {
  FlowIdTag t;
  p->PeekPacketTag(t);
  if (qIndex != 0) {
    uint32_t inDev = t.GetFlowId();
    // 新增：出队释放必须与入队时使用的“等效字节”一致，确保记账闭环。
    uint32_t effectiveBytes = GetEffectiveAdmissionBytes(p->GetSize());
    m_mmu->RemoveFromIngressAdmission(inDev, qIndex, effectiveBytes);
    m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, effectiveBytes);
    if (m_bytes[inDev][ifIndex][qIndex] >= effectiveBytes) {
      m_bytes[inDev][ifIndex][qIndex] -= effectiveBytes;
    } else {
      m_bytes[inDev][ifIndex][qIndex] = 0;
    }
    // 新增：和 SwitchNode 对齐，在释放占用后触发 Resume 检查。
    CheckAndSendResume(inDev, qIndex);
  }
  m_txBytes[ifIndex] += p->GetSize();
  m_lastPktSize[ifIndex] = p->GetSize();
  m_lastPktTs[ifIndex] = Simulator::Now().GetTimeStep();
}

void OpticalModuleNode::PrintSwitchQlen(FILE* qlen_output) {
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

void OpticalModuleNode::PrintSwitchBw(FILE* bw_output,
                                      uint32_t bw_mon_interval) {
  uint32_t n_dev = this->GetNDevices();
  for (uint32_t i = 1; i < n_dev; ++i) {
    if (last_txBytes[i] == m_txBytes[i]) {
      continue;
    }
    double bw = (m_txBytes[i] - last_txBytes[i]) * 8 * 1e6 / bw_mon_interval;
    bw = bw * 1.0 / 1e9; // Gbps
    fprintf(bw_output, "%lu, %u, %u, %f\n", Simulator::Now().GetTimeStep(), m_id,
            i, bw);
    fflush(bw_output);
    last_txBytes[i] = m_txBytes[i];
  }
}

int OpticalModuleNode::logres_shift(int b, int l) {
  // 新增：该节点当前复用 NVSwitch 路径，此函数暂未使用，保留接口兼容。
  (void)b;
  (void)l;
  return 0;
}

int OpticalModuleNode::log2apprx(int x, int b, int m, int l) {
  // 新增：该节点当前复用 NVSwitch 路径，此函数暂未使用，保留接口兼容。
  (void)x;
  (void)b;
  (void)m;
  (void)l;
  return 0;
}

} // namespace ns3
