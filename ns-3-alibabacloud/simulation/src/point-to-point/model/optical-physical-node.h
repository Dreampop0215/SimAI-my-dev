#ifndef OPTICAL_PHYSICAL_NODE_H
#define OPTICAL_PHYSICAL_NODE_H

#include <unordered_map>
#include <ns3/node.h>
#include <ns3/nstime.h>
#include "qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"

namespace ns3 {

class Packet;

// 中文注释：独立于 OpticalModuleNode 的“真实光学建模”节点。
// 当前阶段只新建实体，不在本步骤接入现有构建和运行路径。
class OpticalPhysicalNode : public Node {
  static const uint32_t pCnt = 1025; // Number of ports used
  static const uint32_t qCnt = 8;    // Number of queues/priorities used

  uint32_t m_ecmpSeed;
  std::unordered_map<uint32_t, std::vector<int>> m_rtTable;

  // 中文注释：与交换节点一致的队列占用与监控统计。
  uint32_t m_bytes[pCnt][pCnt][qCnt];
  uint64_t m_txBytes[pCnt];
  uint32_t m_lastPktSize[pCnt];
  uint64_t m_lastPktTs[pCnt];
  double m_u[pCnt];

 protected:
  // 中文注释：与现有交换节点对齐的通用参数。
  uint32_t m_ccMode;
  uint64_t m_maxRtt;
  uint32_t m_ackHighPrio;

  // 中文注释：真实光学路径参数（均为节点内部处理开销，不替代链路传播时延）。
  uint32_t m_oeoDelayNs;            // O/E/O 固定处理延迟
  uint32_t m_fecBaseDelayNs;        // FEC 固定延迟
  uint32_t m_fecPerKbDelayNs;       // FEC 按包长线性附加延迟
  uint32_t m_guardIntervalNs;       // 光层守护间隔
  uint32_t m_wssReconfigDelayNs;    // 输出端口切换时的重配延迟

  // 中文注释：为保持“无损语义”，沿用 MMU 准入并允许按比例折减记账。
  uint32_t m_opticalAdmissionScalePermille;
  uint32_t m_opticalMinAdmissionBytes;

  // 中文注释：用于估计输出端口切换惩罚（简化版 WSS 重配模型）。
  uint32_t m_lastForwardOutDev;

 private:
  int GetOutDev(Ptr<const Packet> p, CustomHeader& ch);
  void SendToDev(Ptr<Packet> p, CustomHeader& ch);
  void RetrySendToDev(Ptr<Packet> p, CustomHeader ch);
  void DoOpticalPhysicalForward(uint32_t inDev, uint32_t outDev,
                                uint32_t qIndex, uint32_t effectiveBytes,
                                Ptr<Packet> p, CustomHeader ch);

  void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
  void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);

  uint32_t GetEffectiveAdmissionBytes(uint32_t packetBytes) const;
  Time GetOpticalPhysicalDelay(uint32_t outDev, uint32_t qIndex,
                               uint32_t packetBytes) const;

  static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);

 public:
  Ptr<SwitchMmu> m_mmu;

  static TypeId GetTypeId(void);
  OpticalPhysicalNode();

  void SetEcmpSeed(uint32_t seed);
  void AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx);
  void ClearTable();

  bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                               CustomHeader& ch);
  void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

  // 中文注释：保留与其他节点一致的接口，便于后续无缝接入 PINT 路径。
  int logres_shift(int b, int l);
  int log2apprx(int x, int b, int m, int l);

  // for monitor
  uint64_t last_txBytes[pCnt];
  uint64_t last_port_qlen[pCnt];
  void PrintSwitchQlen(FILE* qlen_output);
  void PrintSwitchBw(FILE* bw_output, uint32_t bw_mon_interval);
};

} // namespace ns3

#endif // OPTICAL_PHYSICAL_NODE_H
