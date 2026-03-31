#include "optical-qbb-channel.h"
#include "qbb-net-device.h" // 中文注释：补齐 QbbNetDevice/PointToPointNetDevice 完整类型，避免 Ptr<T> 不完整类型编译错误。

#include "ns3/custom-header.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("OpticalQbbChannel");
NS_OBJECT_ENSURE_REGISTERED(OpticalQbbChannel);

TypeId OpticalQbbChannel::GetTypeId(void) {
  static TypeId tid =
      TypeId("ns3::OpticalQbbChannel")
          .SetParent<QbbChannel>()
          .AddConstructor<OpticalQbbChannel>()
          .AddAttribute(
              "OpticalExtraFixedDelayNs",
              "中文注释：光链路额外固定处理时延（ns）。",
              UintegerValue(40),
              MakeUintegerAccessor(&OpticalQbbChannel::m_opticalExtraFixedDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalFecPerKbDelayNs",
              "中文注释：FEC 按包长附加时延（每 KiB 的 ns）。",
              UintegerValue(1),
              MakeUintegerAccessor(&OpticalQbbChannel::m_opticalFecPerKbDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalGuardIntervalNs",
              "中文注释：光层 guard interval（ns）。",
              UintegerValue(8),
              MakeUintegerAccessor(&OpticalQbbChannel::m_opticalGuardIntervalNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalReconfigDelayNs",
              "中文注释：同方向目的变化时附加重配时延（ns）。",
              UintegerValue(80),
              MakeUintegerAccessor(&OpticalQbbChannel::m_opticalReconfigDelayNs),
              MakeUintegerChecker<uint32_t>(0))
          .AddAttribute(
              "OpticalLossRate",
              "中文注释：物理层随机丢包率（0~1），默认 0。",
              DoubleValue(0.0),
              MakeDoubleAccessor(&OpticalQbbChannel::m_opticalLossRate),
              MakeDoubleChecker<double>(0.0, 1.0));
  return tid;
}

OpticalQbbChannel::OpticalQbbChannel() : QbbChannel() {
  m_uv = CreateObject<UniformRandomVariable>();
  m_hasLastDip[0] = false;
  m_hasLastDip[1] = false;
  m_lastDip[0] = 0;
  m_lastDip[1] = 0;
}

bool OpticalQbbChannel::TransmitStart(Ptr<Packet> p, Ptr<QbbNetDevice> src,
                                      Time txTime) {
  // 中文注释：可选的物理层随机丢包开关（默认关闭）。
  if (m_opticalLossRate > 0.0 && m_uv->GetValue() < m_opticalLossRate) {
    return true;
  }

  uint32_t wire = (src == GetSource(0) ? 0u : 1u);

  uint64_t packetKb = (static_cast<uint64_t>(p->GetSize()) + 1023ULL) / 1024ULL;
  uint64_t extraNs = static_cast<uint64_t>(m_opticalExtraFixedDelayNs) +
                     static_cast<uint64_t>(m_opticalFecPerKbDelayNs) * packetKb +
                     static_cast<uint64_t>(m_opticalGuardIntervalNs);

  // 中文注释：通过目的 IP 变化估算重配惩罚，增强“物理层可区分性”。
  CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header |
                  CustomHeader::L4_Header);
  bool parsed = p->PeekHeader(ch) != 0;
  if (parsed) {
    if (m_hasLastDip[wire] && m_lastDip[wire] != ch.dip) {
      extraNs += static_cast<uint64_t>(m_opticalReconfigDelayNs);
    }
    m_lastDip[wire] = ch.dip;
    m_hasLastDip[wire] = true;
  }

  return QbbChannel::TransmitStart(p, src, txTime + NanoSeconds(extraNs));
}

} // namespace ns3
