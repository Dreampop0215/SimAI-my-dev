#ifndef OPTICAL_QBB_CHANNEL_H
#define OPTICAL_QBB_CHANNEL_H

#include "qbb-channel.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

class Packet;
class QbbNetDevice;

// 中文注释：方案B物理层增强链路。
// 在原 QbbChannel 基础上传输时延上叠加光层处理项（FEC/guard/reconfig），
// 并可选注入小概率物理层丢包。
class OpticalQbbChannel : public QbbChannel {
 public:
  static TypeId GetTypeId(void);
  OpticalQbbChannel();

  bool TransmitStart(Ptr<Packet> p, Ptr<QbbNetDevice> src, Time txTime) override;

 private:
  uint32_t m_opticalExtraFixedDelayNs;
  uint32_t m_opticalFecPerKbDelayNs;
  uint32_t m_opticalGuardIntervalNs;
  uint32_t m_opticalReconfigDelayNs;
  double m_opticalLossRate;

  // 中文注释：按方向记录上一包目的 IP，用于估算重配惩罚。
  bool m_hasLastDip[2];
  uint32_t m_lastDip[2];

  Ptr<UniformRandomVariable> m_uv;
};

} // namespace ns3

#endif // OPTICAL_QBB_CHANNEL_H
