#ifndef OPTICAL_MODULE_NODE_H
#define OPTICAL_MODULE_NODE_H

#include <unordered_map>
#include <ns3/node.h>
#include <ns3/nstime.h>
#include "qbb-net-device.h"
#include "switch-mmu.h"
#include "pint.h"

namespace ns3 {

class Packet;

// 新增：光模块节点实体，接口与现有交换类节点保持一致，便于在 common.h 中按 node_type 分支接入。
class OpticalModuleNode : public Node {
  static const uint32_t pCnt = 1025; // Number of ports used
  static const uint32_t qCnt = 8;    // Number of queues/priorities used
  uint32_t m_ecmpSeed;
  // 目的 IP 到可选端口列表（ECMP）的路由表。
  std::unordered_map<uint32_t, std::vector<int>> m_rtTable;

  // 监控与拥塞控制需要的队列字节统计。
  uint32_t m_bytes[pCnt][pCnt][qCnt];
  uint64_t m_txBytes[pCnt];
  uint32_t m_lastPktSize[pCnt];
  uint64_t m_lastPktTs[pCnt]; // ns
  double m_u[pCnt];

 protected:
  // 新增：与 SwitchNode 对齐的拥塞控制参数，便于 common.h 统一下发属性。
  uint32_t m_ccMode;
  uint64_t m_maxRtt;
  uint32_t m_ackHighPrio; // set high priority for ACK/NACK
  // 新增：光模块固定处理时延（ns），用于模拟 O/E/O + FEC 等快速处理开销。
  uint32_t m_opticalLatencyNs;
  // 新增：光模块拥塞准入缩放（千分比）。小于 1000 表示占用折减，体现“更友好拥塞表现”。
  uint32_t m_opticalAdmissionScalePermille;
  // 新增：准入最小计费字节，避免小包折减后变成 0 导致统计失真。
  uint32_t m_opticalMinAdmissionBytes;

 private:
  int GetOutDev(Ptr<const Packet>, CustomHeader& ch);
  void SendToDev(Ptr<Packet> p, CustomHeader& ch);
  // 新增：对齐 SwitchNode，在拥塞时主动发送 PFC Pause，减少无谓丢包。
  void CheckAndSendPfc(uint32_t inDev, uint32_t qIndex);
  // 新增：对齐 SwitchNode，在拥塞缓解时发送 PFC Resume，避免上游长期停发。
  void CheckAndSendResume(uint32_t inDev, uint32_t qIndex);
  // 新增：根据光模块配置计算用于 MMU 记账的“等效字节”。
  uint32_t GetEffectiveAdmissionBytes(uint32_t packetBytes) const;
  // 新增：计算该报文在光模块内的处理时延。
  Time GetOpticalForwardDelay(uint32_t qIndex, uint32_t packetBytes) const;
  // 新增：准入失败时的重试入口，避免直接丢包导致上层 stream 无法完成。
  void RetrySendToDev(Ptr<Packet> p, CustomHeader ch);
  // 新增：在固定时延到期后真正执行发送；若链路已 down，需要回滚之前的 MMU 记账。
  void DoOpticalForward(uint32_t inDev, uint32_t outDev, uint32_t qIndex,
                        uint32_t effectiveBytes, Ptr<Packet> p, CustomHeader ch);
  static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);

 public:
  Ptr<SwitchMmu> m_mmu;

  static TypeId GetTypeId(void);
  OpticalModuleNode();
  void SetEcmpSeed(uint32_t seed);
  void AddTableEntry(Ipv4Address& dstAddr, uint32_t intf_idx);
  void ClearTable();
  bool SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet,
                               CustomHeader& ch);
  void SwitchNotifyDequeue(uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p);

  // for approximate calc in PINT
  int logres_shift(int b, int l);
  int log2apprx(int x, int b, int m, int l);

  // for monitor
  uint64_t last_txBytes[pCnt];
  uint64_t last_port_qlen[pCnt];
  void PrintSwitchQlen(FILE* qlen_output);
  void PrintSwitchBw(FILE* bw_output, uint32_t bw_mon_interval);
};

} // namespace ns3

#endif // OPTICAL_MODULE_NODE_H
