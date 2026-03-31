/******************************************************************************
This source code is licensed under the MIT license found in the
LICENSE file in the root directory of this source tree.
*******************************************************************************/

#include "Workload.hh"
#include "CSVWriter.hh"
#include "Layer.hh"
#include "astra-sim/system/BaseStream.hh"
#include "astra-sim/system/MockNcclLog.h"
#include "astra-sim/system/AstraParamParse.hh"
#include "astra-sim/system/collective/Algorithm.hh"
#include "astra-sim/system/collective/NcclTreeFlowModel.hh"
#include "astra-sim/system/collective/Ring.hh"
#include <limits>
#include <string>

namespace {
const char* ToStreamStateName(AstraSim::StreamState state) {
  switch (state) {
    case AstraSim::StreamState::Created:
      return "Created";
    case AstraSim::StreamState::Transferring:
      return "Transferring";
    case AstraSim::StreamState::Ready:
      return "Ready";
    case AstraSim::StreamState::Executing:
      return "Executing";
    case AstraSim::StreamState::Zombie:
      return "Zombie";
    case AstraSim::StreamState::Dead:
      return "Dead";
  }
  return "Unknown";
}

const char* ToComTypeName(AstraSim::ComType type) {
  switch (type) {
    case AstraSim::ComType::None:
      return "None";
    case AstraSim::ComType::Reduce_Scatter:
      return "ReduceScatter";
    case AstraSim::ComType::All_Gather:
      return "AllGather";
    case AstraSim::ComType::All_Reduce:
      return "AllReduce";
    case AstraSim::ComType::All_to_All:
      return "AllToAll";
    case AstraSim::ComType::All_Reduce_All_to_All:
      return "AllReduceAllToAll";
    case AstraSim::ComType::All_Reduce_NVLS:
      return "AllReduceNVLS";
  }
  return "Unknown";
}

const char* ToAlgorithmName(const AstraSim::Algorithm* algorithm) {
  if (algorithm == nullptr) {
    return "None";
  }
  switch (algorithm->name) {
    case AstraSim::Algorithm::Name::Ring:
      return "Ring";
    case AstraSim::Algorithm::Name::DoubleBinaryTree:
      return "DoubleBinaryTree";
    case AstraSim::Algorithm::Name::AllToAll:
      return "AllToAll";
    case AstraSim::Algorithm::Name::HalvingDoubling:
      return "HalvingDoubling";
  }
  return "Unknown";
}

std::string BuildAlgorithmDiag(const AstraSim::BaseStream* stream) {
  if (stream == nullptr || stream->my_current_phase.algorithm == nullptr) {
    return "algo=None";
  }

  const AstraSim::Algorithm* algorithm = stream->my_current_phase.algorithm;
  std::string desc =
      std::string("algo=") + ToAlgorithmName(algorithm) +
      "|alg_layer=" + std::to_string(algorithm->layer_num) +
      "|alg_enabled=" + std::to_string(algorithm->enabled ? 1 : 0);

  if (const auto* ring = dynamic_cast<const AstraSim::Ring*>(algorithm)) {
    desc += "|ring_stream_count=" + std::to_string(ring->stream_count) +
            "|ring_free_packets=" + std::to_string(ring->free_packets) +
            "|ring_total_sent=" + std::to_string(ring->total_packets_sent) +
            "|ring_total_recv=" + std::to_string(ring->total_packets_received) +
            "|ring_packets_q=" + std::to_string(ring->packets.size()) +
            "|ring_locked=" + std::to_string(ring->locked_packets.size());
    return desc;
  }

  if (const auto* tree =
          dynamic_cast<const AstraSim::NcclTreeFlowModel*>(algorithm)) {
    long treeRemainingFlowTokens = 0;
    for (const auto& kv : tree->_stream_count) {
      treeRemainingFlowTokens += kv.second;
    }
    long treeFreePackets = 0;
    for (const auto& kv : tree->free_packets) {
      treeFreePackets += kv.second;
    }
    desc += "|tree_send_packets=" + std::to_string(tree->send_packets.load()) +
            "|tree_recv_packets=" + std::to_string(tree->recv_packets.load()) +
            "|tree_remaining_tokens=" + std::to_string(treeRemainingFlowTokens) +
            "|tree_free_packets=" + std::to_string(treeFreePackets) +
            "|tree_channels=" + std::to_string(tree->m_channels);
    return desc;
  }

  return desc;
}
} // namespace

namespace AstraSim {
Workload::~Workload() {
  if (end_to_end != nullptr) {
    delete end_to_end;
  }
  if (detailed != nullptr) {
    delete detailed;
  }
  if (dimension_utilization != nullptr) {
    delete dimension_utilization;
  }
  for (int i = 0; i < SIZE; i++) {
    delete layers[i];
  }
  if (layers != nullptr) {
    delete[] layers;
  }
}
Workload::Workload(
    std::string run_name,
    Sys* generator,
    std::string name,
    int TOTAL_PASS,
    int total_rows,
    int stat_row,
    std::string path,
    bool seprate_log) {
  this->initialized = false;
  this->layers = nullptr;
  this->SIZE = 0;
  this->counter = 0;
  this->delay_loaded = false;
  this->checkpoint_initiated = false;
  this->collective_issued = false;
  this->current_state = LoopState::Forward_Pass;
  this->generator = generator;
  this->TOTAL_PASS = TOTAL_PASS;
  this->pass_counter = 0;
  this->index = 0;
  this->waiting_for_comm = 0;
  end_to_end = nullptr;
  detailed = nullptr;
  dimension_utilization = nullptr;
  this->path = path;
  this->stat_row = stat_row;
  this->seprate_log = seprate_log;
  this->initialized = initialize_workload(name);
  if (this->initialized == false) {
    return;
  }
  this->total_rows = total_rows;
  this->run_name = run_name;
  this->registered_for_finished_streams = false;
  this->tail_watchdog_armed = false;
  this->tail_last_log_tick = 0;
  this->tail_watchdog_interval = 1000000;
  this->tail_last_streams_finished = 0;
  this->tail_wait_start_tick = 0;
  this->tail_last_progress_tick = 0;
  this->tail_stall_report_interval = 5000000;
  this->tail_last_stall_report_tick = 0;
  this->tail_last_next_event_tick = std::numeric_limits<Tick>::max();
  this->tail_last_sample_signature = "";
  this->tail_watchdog_counter = 0;
  this->tail_last_recovery_tick = 0;
  this->tail_soft_recovery_interval = 10000000;
  this->tail_hard_recovery_interval =
      UserParam::getInstance()->tail_hard_recovery_interval;
  this->tail_hard_recovery_fired = false;
  this->tail_recovery_attempts = 0;
  this->sim_end_notified = false;
  #ifndef PHY_MTP
  if (generator->id == 0 && seprate_log) {
    std::cout << "stat path: " << path << " ,total rows: " << total_rows
              << " ,stat row: " << stat_row << std::endl;
    detailed = new CSVWriter(path, "detailed_"+std::to_string(generator->total_nodes)+".csv");
    end_to_end = new CSVWriter(path, "EndToEnd.csv");
    dimension_utilization =
        new CSVWriter(path, run_name + "_dimension_utilization_"+std::to_string(generator->npu_offset)+".csv");
    if (stat_row == 0) {
      initialize_stat_files();
    }
  }
  #endif
}
void Workload::initialize_stat_files() {
  #ifdef NS3_MPI
  detailed->initialize_csv(SIZE * total_rows + 20, 50);
  #endif
  #ifdef NS3_MTP 
  detailed->initialize_csv(SIZE * total_rows + 20, 50);
  #endif
  end_to_end->initialize_csv(SIZE * total_rows + 20, 50);
  // 中文注释：初始化时立即写入状态行，避免仿真长时间运行但 EndToEnd.csv 仍是空文件。
  end_to_end->write_line("status,initialized");
}
void Workload::call(EventType event, CallData* data) {
  if (current_state == LoopState::Wait_For_Sim_Finish &&
      event == EventType::General && tail_watchdog_armed) {
    // 中文注释：尾流监控心跳事件触发后立刻解锁，后续由 check_for_sim_end 决定是否重挂。
    tail_watchdog_armed = false;
  }
  if (counter > 0) {
    if(generator->id == 0) std::cout << "counter > 0" << std::endl;
    generator->try_register_event(
        this, EventType::Workload_Wait, NULL, counter);
    return;
  }
  if (parallelismPolicy == ParallelismPolicy::Data) {
    iterate_data_parallel();
  } else if (parallelismPolicy == ParallelismPolicy::Transformer) {
    iterate_hybrid_parallel_Transformer();
  } else if (
      parallelismPolicy == ParallelismPolicy::DLRM ||
      parallelismPolicy == ParallelismPolicy::DLRMEnhanced) {
    iterate_hybrid_parallel_DLRM();
  } else if (parallelismPolicy == ParallelismPolicy::MicroBenchmark) {
    iterate_micro_benchmark();
  } else if (parallelismPolicy == ParallelismPolicy::Model) {
    iterate_model_parallel();
  } else if (parallelismPolicy == ParallelismPolicy::HybridDataModel) {
    iterate_hybrid_parallel_data_model();
  } else if (parallelismPolicy == ParallelismPolicy::HybridModelData) {
    iterate_hybrid_parallel_model_data();
  } else if (parallelismPolicy == ParallelismPolicy::DistributedInference) {
    iterate_distributed_inference();
  } else if (parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd) {
    iterate_hybrid_parallel_Transformer_fwd_in_bckwd();
  } else if (parallelismPolicy == ParallelismPolicy::HybridCustomized) {
    iterate_hybrid_parallel_customized();
  } else {
    Sys::sys_panic("No known parallelism!");
  }
}
void Workload::report() {
  // 中文注释：调试用，确认是否真正进入 report 阶段（CSV 写入发生在 report 中）。
  if (generator->id == 0) {
    std::cout << "[Workload::report] enter report, pass_counter=" << pass_counter
              << ", streams_finished=" << generator->streams_finished
              << ", streams_injected=" << generator->streams_injected
              << std::endl;
  }
  double total_compute = 0;
  double total_exposed = 0;
  // #ifdef ANALYTI
  double pre_bubble_time = 0;
  double DP_comm = 0;
  double DP_EP_comm = 0;
  double Expose_TP_comm = 0;
  double Expose_EP_comm = 0;
  // #endif
  std::vector<double> total_fwd_time = {0, 0, 0};
  std::vector<double> total_wg_time = {0, 0, 0};
  std::vector<double> total_ig_time = {0, 0, 0};
  AstraSimDataAPI astraSimDataAPI;
  astraSimDataAPI.run_name = run_name;
  astraSimDataAPI.workload_finished_time = ((double)Sys::boostedTick()) / FREQ;
  std::cout<<"workload stats for the job scheduled at NPU offset: "
            <<generator->npu_offset<<std::endl;
  for (int i = 0; i < SIZE; i++) {
    #ifdef ANALYTI
    astraSimDataAPI.layers_stats.push_back(layers[i]->report(
        run_name,
        i,
        total_rows,
        stat_row,
        detailed,
        end_to_end,
        total_compute,
        total_exposed,
        pre_bubble_time,
        DP_comm,
        DP_EP_comm,
        Expose_TP_comm,
        Expose_EP_comm,
        this->seprate_log));
    #else
    astraSimDataAPI.layers_stats.push_back(layers[i]->report(
        run_name,
        i,
        total_rows,
        stat_row,
        detailed,
        end_to_end,
        total_compute,
        total_exposed,
        this->seprate_log,
        total_fwd_time,
        total_wg_time,
        total_ig_time,
        pre_bubble_time,
        DP_comm,
        DP_EP_comm,
        Expose_TP_comm,
        Expose_EP_comm));
    #endif
  }
  astraSimDataAPI.total_compute = total_compute;
  astraSimDataAPI.total_exposed_comm = total_exposed;
  astraSimDataAPI.avg_chunk_latency_per_logical_dimension =
      generator->scheduler_unit->get_average_latency_per_dimension();
  for (auto& latency :
       astraSimDataAPI.avg_chunk_latency_per_logical_dimension) {
    latency /= FREQ;
  }
  std::cout << "*************************" << std::endl;
  std::cout << "all passes finished at time: " << Sys::boostedTick()
            << ", id of first layer: " << layers[0]->id << std::endl;
  generator->NI->pass_front_end_report(astraSimDataAPI);
  #ifdef NS3_MTP 
  if (this->seprate_log) {
    std::list<std::list<std::pair<uint64_t, double>>> dims;
    for (int i = 0; i < generator->scheduler_unit->usage.size(); i++) {
      dims.push_back(
          generator->scheduler_unit->usage[i].report_percentage(10000));
    }
    dimension_utilization->finalize_csv(dims);
  }
  #endif
  #ifdef NS3_MPI 
  if (this->seprate_log) {
    std::list<std::list<std::pair<uint64_t, double>>> dims;
    for (int i = 0; i < generator->scheduler_unit->usage.size(); i++) {
      dims.push_back(
          generator->scheduler_unit->usage[i].report_percentage(10000));
    }
    dimension_utilization->finalize_csv(dims);
  }
  #endif
}
void Workload::check_for_sim_end() {
  if (pass_counter == TOTAL_PASS) {
    // 中文注释：仅在首次进入尾流等待时打印，避免等待期间重复刷屏。
    if (generator->id == 0 && !registered_for_finished_streams) {
      std::cout << "[Workload::check_for_sim_end] pass reached, streams_finished="
                << generator->streams_finished
                << ", streams_injected=" << generator->streams_injected
                << ", event_waiting=" << registered_for_finished_streams
                << std::endl;
    }
    current_state = LoopState::Wait_For_Sim_Finish;
    if (generator->streams_finished != generator->streams_injected) {
      const Tick now = Sys::boostedTick();
      if (tail_wait_start_tick == 0) {
        tail_wait_start_tick = now;
      }
      const bool stream_progress =
          generator->streams_finished != tail_last_streams_finished;
      if (stream_progress) {
        tail_last_progress_tick = now;
        tail_hard_recovery_fired = false;
      }
      const bool heartbeat_due =
          (tail_last_log_tick == 0) ||
          (now - tail_last_log_tick >= tail_watchdog_interval);

      bool emitted_log = false;
      bool emitted_stall_log = false;
      if (generator->id == 0) {
        uint64_t active_streams = 0;
        BaseStream* sample_stream = nullptr;
        for (const auto& kv : generator->active_Streams) {
          active_streams += kv.second.size();
          if (sample_stream == nullptr && !kv.second.empty()) {
            sample_stream = kv.second.front();
          }
        }
        const uint64_t outstanding =
            (generator->streams_injected >= generator->streams_finished)
                ? (generator->streams_injected - generator->streams_finished)
                : 0;
        const Tick no_event_tick = std::numeric_limits<Tick>::max();
        Tick next_event_tick = no_event_tick;
        Tick next_event_in = no_event_tick;
        uint64_t head_bucket_size = 0;
        int head_event_type = static_cast<int>(EventType::NONE);
        if (!generator->event_queue.empty()) {
          auto head_it = generator->event_queue.begin();
          next_event_tick = head_it->first;
          next_event_in = next_event_tick - now;
          head_bucket_size = head_it->second.size();
          if (!head_it->second.empty()) {
            head_event_type = static_cast<int>(std::get<1>(head_it->second.front()));
          }
        }
        const std::string next_event_in_str =
            (next_event_in == no_event_tick) ? "none" : std::to_string(next_event_in);
        const std::string next_event_tick_str =
            (next_event_tick == no_event_tick) ? "none" : std::to_string(next_event_tick);

        std::string sample_desc = "none";
        std::string algo_desc = "algo=None";
        if (sample_stream != nullptr) {
          sample_desc =
              "id=" + std::to_string(sample_stream->stream_num) +
              "|q=" + std::to_string(sample_stream->current_queue_id) +
              "|steps=" + std::to_string(sample_stream->steps_finished) +
              "|phases_left=" + std::to_string(sample_stream->phases_to_go.size()) +
              "|state=" + std::to_string(static_cast<int>(sample_stream->state)) +
              "|state_name=" + ToStreamStateName(sample_stream->state) +
              "|com=" +
              std::to_string(static_cast<int>(sample_stream->current_com_type)) +
              "|com_name=" + ToComTypeName(sample_stream->current_com_type);
          algo_desc = BuildAlgorithmDiag(sample_stream);
        }
        const std::string sample_signature = sample_desc + "|" + algo_desc;
        const bool sample_changed = sample_signature != tail_last_sample_signature;
        const bool head_event_changed = next_event_tick != tail_last_next_event_tick;
        const Tick last_progress =
            (tail_last_progress_tick == 0) ? tail_wait_start_tick : tail_last_progress_tick;
        const Tick idle_for = now - last_progress;
        const bool stall_due =
            (idle_for >= tail_stall_report_interval) &&
            ((tail_last_stall_report_tick == 0) ||
             (now - tail_last_stall_report_tick >= tail_stall_report_interval));

        if (stream_progress || heartbeat_due || sample_changed || head_event_changed) {
          ++tail_watchdog_counter;
          std::string reason = "";
          if (stream_progress) {
            reason += "progress|";
          }
          if (heartbeat_due) {
            reason += "heartbeat|";
          }
          if (sample_changed) {
            reason += "stream_change|";
          }
          if (head_event_changed) {
            reason += "event_head_change|";
          }
          if (!reason.empty()) {
            reason.pop_back();
          } else {
            reason = "none";
          }
          std::cout << "[Workload::tail-watchdog] tick=" << now
                    << ", seq=" << tail_watchdog_counter
                    << ", reason=" << reason
                    << ", outstanding=" << outstanding
                    << ", active_streams=" << active_streams
                    << ", pending_events=" << generator->pending_events
                    << ", event_queue_size=" << generator->event_queue.size()
                    << ", pending_sends=" << generator->pending_sends.size()
                    << ", ready_list=" << generator->ready_list.size()
                    << ", wait_elapsed=" << (now - tail_wait_start_tick)
                    << ", idle_for=" << idle_for
                    << ", next_event_in=" << next_event_in_str
                    << ", next_event_tick=" << next_event_tick_str
                    << ", head_events=" << head_bucket_size
                    << ", head_event_type=" << head_event_type
                    << ", sample_stream={" << sample_desc << "}"
                    << ", sample_algo={" << algo_desc << "}"
                    << std::endl;
          if (sample_changed || head_event_changed) {
            std::cout << "[Workload::tail-delta] tick=" << now
                      << ", sample_changed=" << sample_changed
                      << ", prev_sample={" << tail_last_sample_signature << "}"
                      << ", curr_sample={" << sample_signature << "}"
                      << ", prev_next_event_tick="
                      << ((tail_last_next_event_tick == no_event_tick) ? std::string("none")
                                                                       : std::to_string(tail_last_next_event_tick))
                      << ", curr_next_event_tick=" << next_event_tick_str
                      << std::endl;
          }
          emitted_log = true;
        }
        if (stall_due) {
          std::cout << "[Workload::tail-stall] tick=" << now
                    << ", idle_for=" << idle_for
                    << ", outstanding=" << outstanding
                    << ", active_streams=" << active_streams
                    << ", event_queue_size=" << generator->event_queue.size()
                    << ", pending_sends=" << generator->pending_sends.size()
                    << ", sample_stream={" << sample_desc << "}"
                    << ", sample_algo={" << algo_desc << "}"
                    << std::endl;
          tail_last_stall_report_tick = now;
          emitted_stall_log = true;
        }

        const bool is_single_stream_stall_candidate =
            (outstanding == 1) && (active_streams == 1) &&
            (sample_stream != nullptr) &&
            (sample_stream->state == StreamState::Executing) &&
            (sample_stream->phases_to_go.size() == 0);
        const bool soft_recovery_due =
            is_single_stream_stall_candidate &&
            (idle_for >= tail_soft_recovery_interval) &&
            ((tail_last_recovery_tick == 0) ||
             (now - tail_last_recovery_tick >= tail_soft_recovery_interval));
        const bool hard_recovery_due =
            is_single_stream_stall_candidate &&
            !tail_hard_recovery_fired &&
            (idle_for >= tail_hard_recovery_interval) &&
            generator->pending_sends.empty() &&
            generator->ready_list.empty() &&
            (generator->pending_events == 0) &&
            (generator->event_queue.size() <= 1);

        if (soft_recovery_due) {
          ++tail_recovery_attempts;
          tail_last_recovery_tick = now;
          std::cout << "[Workload::tail-recovery-probe] tick=" << now
                    << ", attempt=" << tail_recovery_attempts
                    << ", idle_for=" << idle_for
                    << ", next_event_tick=" << next_event_tick_str
                    << ", pending_events=" << generator->pending_events
                    << ", pending_sends=" << generator->pending_sends.size()
                    << ", sample_stream={" << sample_desc << "}"
                    << ", sample_algo={" << algo_desc << "}"
                    << std::endl;
          emitted_stall_log = true;
        }

        if (hard_recovery_due) {
          tail_hard_recovery_fired = true;
          std::cout << "[Workload::tail-recovery-force] tick=" << now
                    << ", action=force_wait_for_vnet_turn"
                    << ", idle_for=" << idle_for
                    << ", stream_id=" << sample_stream->stream_num
                    << ", reason=single_stream_executing_without_progress"
                    << std::endl;
          generator->register_event(
              sample_stream, EventType::WaitForVnetTurn, NULL, 1);
          emitted_stall_log = true;
        }

        if (seprate_log && end_to_end != nullptr && (emitted_log || emitted_stall_log)) {
          end_to_end->write_line(
              "status,tail_wait,tick," + std::to_string(now) +
              ",seq," + std::to_string(tail_watchdog_counter) +
              ",streams_finished," +
              std::to_string(generator->streams_finished) +
              ",streams_injected," +
              std::to_string(generator->streams_injected) +
              ",outstanding," + std::to_string(outstanding) +
              ",pending_events," + std::to_string(generator->pending_events) +
              ",event_queue_size," +
              std::to_string(generator->event_queue.size()) +
              ",pending_sends," +
              std::to_string(generator->pending_sends.size()) +
              ",ready_list," + std::to_string(generator->ready_list.size()) +
              ",wait_elapsed," + std::to_string(now - tail_wait_start_tick) +
              ",idle_for," + std::to_string(idle_for) +
              ",next_event_in," + next_event_in_str +
              ",next_event_tick," + next_event_tick_str +
              ",head_events," + std::to_string(head_bucket_size) +
              ",head_event_type," + std::to_string(head_event_type) +
              ",active_streams," + std::to_string(active_streams) +
              ",sample_stream," + sample_desc +
              ",sample_algo," + algo_desc);
          if (sample_changed || head_event_changed) {
            end_to_end->write_line(
                "status,tail_delta,tick," + std::to_string(now) +
                ",sample_changed," + std::to_string(sample_changed ? 1 : 0) +
                ",prev_sample," + tail_last_sample_signature +
                ",curr_sample," + sample_signature +
                ",prev_next_event_tick," +
                ((tail_last_next_event_tick == no_event_tick) ? "none"
                                                              : std::to_string(tail_last_next_event_tick)) +
                ",curr_next_event_tick," + next_event_tick_str);
          }
          if (soft_recovery_due || hard_recovery_due) {
            end_to_end->write_line(
                "status,tail_recovery,tick," + std::to_string(now) +
                ",soft_due," + std::to_string(soft_recovery_due ? 1 : 0) +
                ",hard_due," + std::to_string(hard_recovery_due ? 1 : 0) +
                ",attempts," + std::to_string(tail_recovery_attempts) +
                ",idle_for," + std::to_string(idle_for) +
                ",stream," + sample_desc +
                ",algo," + algo_desc);
          }
        }

        if (sample_changed) {
          tail_last_sample_signature = sample_signature;
        }
        if (head_event_changed) {
          tail_last_next_event_tick = next_event_tick;
        }
      }
      if (stream_progress || heartbeat_due || emitted_log || emitted_stall_log) {
        tail_last_log_tick = now;
        tail_last_streams_finished = generator->streams_finished;
      }

      if (registered_for_finished_streams == false) {
        // 中文注释：首次进入尾流等待时注册回调；后续依赖 watchdog 心跳持续输出诊断信息。
        if (generator->id == 0 && seprate_log && end_to_end != nullptr) {
          end_to_end->write_line(
              "status,waiting_for_streams,streams_finished," +
              std::to_string(generator->streams_finished) +
              ",streams_injected," +
              std::to_string(generator->streams_injected));
        }
        generator->register_for_finished_stream(this);
        registered_for_finished_streams = true;
      }
      if (!tail_watchdog_armed) {
        generator->register_event(
            this, EventType::General, NULL,
            static_cast<int>(tail_watchdog_interval));
        tail_watchdog_armed = true;
      }
      layers[0]->is_weight_grad_comm_finished_blocking();
      return;
    }
    if (generator->streams_finished == generator->streams_injected) {
      tail_watchdog_armed = false;
      if (sim_end_notified) {
        return;
      }
      sim_end_notified = true;
      #ifndef PHY_MTP
      if (generator->id == 0) {
        report();
      }
      #endif
      generator->workload_finished();
      return;
    }
  }
  return;
}
void Workload::iterate_micro_benchmark() {
  assert(index >= 0);
  assert(index < SIZE);
  if (current_state != LoopState::Wait_For_Sim_Finish) {
    for (pass_counter = 0; pass_counter < TOTAL_PASS; pass_counter++) {
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    }
  }
  check_for_sim_end();
}
void Workload::iterate_data_parallel() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    index++;
    delay_loaded = false;
    if (index >= SIZE) {
      current_state = LoopState::Weight_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    delay_loaded = false;
    layers[index]->issue_weight_grad_comm(
        SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    if (index == 0) {
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    delay_loaded = false;
    index--;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_customized() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_data_model() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_model_data() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_distributed_inference() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      index = 0;
      pass_counter++;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_model_parallel() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      std::vector<bool> involved_dimensions{true, true, true};
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued && index > 0) {
      collective_issued = true;
      std::vector<bool> involved_dimensions{true, true, true};
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Non_Blocking);
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_Transformer() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Blocking);
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_Transformer_fwd_in_bckwd() {
  MockNcclLog* NcclLog = MockNcclLog::getInstance();
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      if(layers[index]->fwd_pass_comm_size < 4096 && layers[index]->fwd_pass_comm_size >0){
        layers[index]->fwd_pass_comm_size = 4096;
      }
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Input_Gradient;
      index--;
    }
    NcclLog->writeLog(NcclLogLevel::DEBUG,"workload::call fwd_pass register_event EventType::General ");
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::FIFO, CollectiveBarrier::Non_Blocking);
    }
    if (!layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    collective_issued = false;
    delay_loaded = false;
    if (index >= 0) {
      index--;
    }
    if (index == -1) {
      index = 0;
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Input_Gradient) {
    if (layers[index]->needs_fwd_in_bckwd_initiation && !checkpoint_initiated) {
      int tmp = index;
      while (!layers[index--]->is_checkpoint)
        ;
      index++;
      current_state = LoopState::Forward_In_BackPass;
      checkpoint_initiated = true;
      generator->register_event(this, EventType::General, NULL, 1);
      if (generator->id == 0) {
        std::cout << "***** info, initiating fwd_in_bkwd starting from layer:"
                  << index << " to layer: " << tmp
                  << " ,at time: " << Sys::boostedTick() << std::endl;
      }
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_input_grad_comm(
          SchedulingPolicy::LIFO, CollectiveBarrier::Blocking);
      return;
    }
    checkpoint_initiated = false;
    collective_issued = false;
    delay_loaded = false;
    current_state = LoopState::Weight_Gradient;
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Forward_In_BackPass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::None, CollectiveBarrier::Blocking);
      return;
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (layers[index]->needs_fwd_in_bckwd_initiation) {
      current_state = LoopState::Input_Gradient;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  }
}
void Workload::iterate_hybrid_parallel_DLRM() {
  assert(index >= 0);
  assert(index < SIZE);
  check_for_sim_end();
  if (current_state == LoopState::Forward_Pass) {
    if (!layers[index]->is_weight_grad_comm_finished_blocking()) {
      return;
    }
    if (delay_loaded == false) {
      counter = layers[index]->get_fwd_pass_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued &&
        layers[index]->fwd_pass_comm_type == ComType::All_to_All) {
      collective_issued = true;
      layers[index]->issue_forward_pass_comm(
          SchedulingPolicy::HIGHEST, CollectiveBarrier::Non_Blocking);

    } else if (index == DLRM_LAST_BOTTOM_LAYER) {
      if (!layers[0]->is_fwd_pass_comm_finished_blocking()) {
        return;
      }
    }
    index++;
    delay_loaded = false;
    collective_issued = false;
    if (index >= SIZE) {
      current_state = LoopState::Weight_Gradient;
      index--;
    }
    if (generator->id == 0) {
      std::cout << "*************************layer changed to: " << index
                << std::endl;
    }
    generator->register_event(this, EventType::General, NULL, 1);
    return;
  } else if (current_state == LoopState::Weight_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_weight_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (!collective_issued) {
      collective_issued = true;
      layers[index]->issue_weight_grad_comm(
          SchedulingPolicy::None, CollectiveBarrier::Non_Blocking);
    }
    if (parallelismPolicy == ParallelismPolicy::DLRM &&
        !layers[index]->is_input_grad_comm_finished_blocking()) {
      return;
    }
    if (index == 0) {
      if (generator->id == 0) {
        std::cout << "pass: " << pass_counter
                  << " finished at time: " << Sys::boostedTick() << std::endl;
      }
      pass_counter++;
      current_state = LoopState::Forward_Pass;
    } else {
      current_state = LoopState::Input_Gradient;
    }
    delay_loaded = false;
    collective_issued = false;
    generator->register_event(this, EventType::General, NULL, 1);
  } else if (current_state == LoopState::Input_Gradient) {
    if (delay_loaded == false) {
      counter = layers[index]->get_input_grad_compute();
      delay_loaded = true;
    }
    if (counter > 0) {
      generator->try_register_event(
          this, EventType::Workload_Wait, NULL, counter);
      return;
    }
    if (index == DLRM_LAST_BOTTOM_LAYER + 1) {
      layers[0]->issue_input_grad_comm(
          SchedulingPolicy::HIGHEST, CollectiveBarrier::Non_Blocking);
    }
    index--;
    if (generator->id == 0) {
      std::cout << "*************************layer changed to: " << index
                << " in ig" << std::endl;
    }
    current_state = LoopState::Weight_Gradient;
    collective_issued = false;
    delay_loaded = false;
    generator->register_event(this, EventType::General, NULL, 1);
  }
}
int Workload::get_layer_numbers(std::string workload_input) {
  std::ifstream inFile;
  inFile.open("workload_inputs/" + workload_input);
  if (!inFile) {
    std::cerr << "Unable to open file: " << workload_input << std::endl;
    std::cerr << "This error is fatal. Please check your path and filename."
              << std::endl;
    exit(1);
  } else {
    std::cout << "Success in opening workload file" << std::endl;
  }
  std::string dummyLine;
  std::getline(inFile, dummyLine);
  int layers;
  inFile >> layers;
  inFile.close();
  return layers;
}
ParallelismPolicy Workload::decode_parallelsim(std::string parallelism) {
  if (parallelism == "DATA")
    return ParallelismPolicy::Data;
  else if (parallelism == "HYBRID_TRANSFORMER")
    return ParallelismPolicy::Transformer;
  else if (parallelism == "HYBRID_TRANSFORMER_FWD_IN_BCKWD")
    return ParallelismPolicy::TransformerFwdInBckwd;
  else if (parallelism == "HYBRID_DLRM")
    return ParallelismPolicy::DLRM;
  else if (parallelism == "HYBRID_DLRM_ENHANCED")
    return ParallelismPolicy ::DLRMEnhanced;
  else if (parallelism == "MODEL")
    return ParallelismPolicy::Model;
  else if (parallelism == "HYBRID_DATA_MODEL")
    return ParallelismPolicy::HybridDataModel;
  else if (parallelism == "HYBRID_MODEL_DATA")
    return ParallelismPolicy::HybridModelData;
  else if (parallelism == "HYBRID_CUSTOMIZED")
    return ParallelismPolicy::HybridCustomized;
  else if (parallelism == "MICRO")
    return ParallelismPolicy::MicroBenchmark;
  else if (parallelism == "DISTRIBUTED_INFERENCE")
    return ParallelismPolicy::DistributedInference;
  else
    return ParallelismPolicy::None;
}
std::map<std::string, std::vector<bool>> Workload::decode_involved_dimensions(
    ParallelismPolicy policy,
    int model_parallel_npu_group) {
  std::map<std::string, std::vector<bool>> result;
  std::vector<bool> none{
      false, false, false, false, false, false, false, false, false, false};
  std::vector<bool> all{
      true, true, true, true, true, true, true, true, true, true};
  if (policy == ParallelismPolicy::All) {
    result["fwd"] = all;
    result["ig"] = all;
    result["wg"] = all;
  } else if (
      policy == ParallelismPolicy::Data || policy == ParallelismPolicy::DLRM ||
      policy == ParallelismPolicy::DLRMEnhanced ||
      policy == ParallelismPolicy::MicroBenchmark) {
    result["fwd"] = none;
    result["ig"] = none;
    result["wg"] = all;
  } else if (
      policy == ParallelismPolicy::Model ||
      policy == ParallelismPolicy::DistributedInference) {
    result["fwd"] = all;
    result["ig"] = all;
    result["wg"] = none;
  } else if (policy == ParallelismPolicy::HybridModelData) {
    std::vector<bool> data{
        true, false, false, false, false, false, false, false, false, false};
    std::vector<bool> model{
        false, true, true, true, true, true, true, true, true, true};
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  } else if (policy == ParallelismPolicy::HybridDataModel) {
    std::vector<bool> model{
        true, false, false, false, false, false, false, false, false, false};
    std::vector<bool> data{
        false, true, true, true, true, true, true, true, true, true};
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  } else if (
      policy == ParallelismPolicy::TransformerFwdInBckwd ||
      policy == ParallelismPolicy::Transformer) {
    int model_parallel_boundary =
        generator->break_dimension(model_parallel_npu_group);
    std::vector<bool> model;
    std::vector<bool> data;
    for (int i = 0; i <= model_parallel_boundary; i++) {
      model.push_back(true);
      data.push_back(false);
    }
    for (int i = model_parallel_boundary + 1; i < 10; i++) {
      model.push_back(false);
      data.push_back(true);
    }
    result["fwd"] = model;
    result["ig"] = model;
    result["wg"] = data;
  }
  return result;
}
bool Workload::initialize_workload(std::string name) {
  std::map<int, bool> chekpoints;
  std::map<int, bool> need_checkpoint_initiation;
  std::ifstream inFile;
  inFile.open(name);
  if (!inFile) {
    std::cerr << "Unable to open file: " << name << std::endl;
    std::cerr << "######### Exiting because unable to open the workload input "
                 "file #########"
              << std::endl;
    std::cerr << "This error is fatal. Please check your path and filename."
              << std::endl;
    exit(1);
  } else {
    if (generator->id == 0) {
      std::cout << "Success in opening workload file" << std::endl;
    }
  }
 std::string firstline;
  std::getline(inFile,firstline);
  // std::cout << "First line is : '" << firstline << "'" << std::endl;
  std::istringstream iss(firstline);
  std:string token;
  std::vector<std::string> tokens;
  // bool findparallesimPolcy = false;
  
  while (iss >> token) {
        tokens.push_back(token);
        // std::cout << "Token is : '" << token << "'" << std::endl;
    }



  if(!tokens.empty()){
    parallelismPolicy = decode_parallelsim(tokens[0]);
  }

  if (parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd ||
      parallelismPolicy == ParallelismPolicy::Transformer) {
        for (size_t i = 1; i < tokens.size(); i = i+1){
          if(tokens[i]=="model_parallel_NPU_group:"){
            model_parallel_npu_group = std::stoi(tokens[i+1]);
            if (generator->id == 0) {
              std::cout <<"model_parallel_NPU_group is " << model_parallel_npu_group << std::endl;
            }
          }else if(tokens[i]=="ep:"){
            expert_parallel_npu_group = std::stoi(tokens[i+1]);
          }else if(tokens[i]== "pp:"){
            pipeline_model_parallelism = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="vpp:"){
            vpp = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="ga:"){
            GA = std::stoi(tokens[i+1]);
          }else if(tokens[i]=="all_gpus:"){
            all_gpus = std::stoi(tokens[i+1]);
          }
        }

        if(parallelismPolicy == ParallelismPolicy::TransformerFwdInBckwd){
          if (generator->id == 0) {
            std::cout << "checkpoints layers are: ";
          }
          for(size_t i = 1; i < tokens.size(); i = i+1){
            if(tokens[i]=="checkpoints:"){
              int account = std::stoi(tokens[i+1]);
              while(account-- >0){
                int j = 2;
                int layer = std::stoi(tokens[i+j]);
                chekpoints[layer] = true;
                if (generator->id == 0) {
                  std::cout << layer << ", ";
                }
                j++;
              }
                
            }else if(tokens[i]=="checkpoint_initiates:"){
                if (generator->id == 0) {
                  std::cout << std::endl;
                  std::cout << "layers initiating fwd_in_bckwd are: ";
                }
                int account = std::stoi(tokens[i+1]);
                while(account-- >0){
                  int j = 2;
                  int layer = std::stoi(tokens[i+j]);
                  need_checkpoint_initiation[layer] = true;
                  if (generator->id == 0) {
                    std::cout << layer << ", ";
                  }
                  j++;
                }
                if (generator->id == 0) {
                  std::cout << std::endl;
                }
              }
            }
          }
      }else if(parallelismPolicy == ParallelismPolicy::DLRM ||
                parallelismPolicy == ParallelismPolicy::DLRMEnhanced){
                  for (size_t i = 1; i < tokens.size(); i = i+1){
                    if(tokens[i]=="DLRM_LAST_BOTTOM_LAYER:"){
                      DLRM_LAST_BOTTOM_LAYER = std::stoi(tokens[i+1]);
                    }
                  }
                if (generator->id == 0) {
                  std::cout
                  << "****************** info: DLRM workload last bottom layer is: "
                  << DLRM_LAST_BOTTOM_LAYER << std::endl;
                }
        }else if (parallelismPolicy == ParallelismPolicy::None) {
          #ifndef PHY_MTP
          std::cerr << "######### Exiting because unable to decode the workload "
                 "parallelization strategy #########"
                  << std::endl;
          inFile.close();
          exit(1);
          #else
          parallelismPolicy = ParallelismPolicy::TransformerFwdInBckwd;
          #endif
  }
  std::map<std::string, std::vector<bool>> general_involved_dimensions =
      decode_involved_dimensions(parallelismPolicy, model_parallel_npu_group);
  pp_commsize = 0;
  for (size_t i = 1; i < tokens.size(); i = i+1){
    if(tokens[i]=="pp_comm"||tokens[i]=="pp_comm:"){
      pp_commsize = std::stoi(tokens[i+1]);
    }
  }
  if (generator->id == 0) {
      std::cout <<"pp_commize:"<< pp_commsize << std::endl;
  }
  if(generator->id == 0){
    if (model_parallel_npu_group == 0 || expert_parallel_npu_group == 0 || pipeline_model_parallelism == 0 
        || vpp==0 || GA == 0 || all_gpus == 0 ||(pipeline_model_parallelism !=1 && pp_commsize ==0)||(pipeline_model_parallelism == 1 && pp_commsize !=0)){
          std::cerr << "*****Warining: Input workload format mismatch. It may cause simulation error. Pleased use the latest AICB to generate.*****" << std::endl;
      }
  }        
  run_type = tokens[0];
  std::string secondline;
  std::getline(inFile,secondline);

  int lines;
  // std::cout << "Second line content: '" << secondline << "'" << std::endl;
  lines = std::stoi(secondline);


  SIZE = lines;
  layers = new Layer*[SIZE];
  for (int i = 0; i < lines; i++) {
    std::string id;
    inFile >> id;
    int depen;
    inFile >> depen;

    Tick fp_compute_time;
    inFile >> fp_compute_time;
    std::string fp_comm_type_s;
    inFile >> fp_comm_type_s;
    uint64_t fp_comm_size;
    inFile >> fp_comm_size;

    Tick ig_compute_time;
    inFile >> ig_compute_time;
    std::string ig_comm_type_s;
    inFile >> ig_comm_type_s;
    uint64_t ig_comm_size;
    inFile >> ig_comm_size;

    Tick wg_compute_time;
    inFile >> wg_compute_time;
    std::string wg_comm_type_s;
    inFile >> wg_comm_type_s;
    uint64_t wg_comm_size;
    inFile >> wg_comm_size;
    Tick wg_update_time;
    inFile >> wg_update_time;

    ParallelismPolicy specific_policy = ParallelismPolicy::None;
    std::map<std::string, std::vector<bool>> selected_involved_dimensions;
    ComType fp_type = ComType::None;
    ComType ig_type = ComType::None;
    ComType wg_type = ComType::None;
    MockNccl::GroupType fp_group_type = MockNccl::GroupType::NONE;
    MockNccl::GroupType ig_group_type = MockNccl::GroupType::NONE;
    MockNccl::GroupType wg_group_type = MockNccl::GroupType::NONE;
    if (wg_comm_type_s.substr(0,9) == "ALLREDUCE") {
      wg_type = ComType::All_Reduce;
      if(wg_comm_type_s == "ALLREDUCE"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLREDUCE_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLREDUCE_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,8) == "ALLTOALL") {
      wg_type = ComType::All_to_All;
      if(wg_comm_type_s == "ALLTOALL"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLTOALL_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLTOALL_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      wg_type = ComType::All_Reduce_All_to_All;
      if(wg_comm_type_s == "ALLREDUCEALLTOALL"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,9) == "ALLGATHER") {
      wg_type = ComType::All_Gather;
      if(wg_comm_type_s == "ALLGATHER"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "ALLGATHER_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "ALLGATHER_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    } else if (wg_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      wg_type = ComType::Reduce_Scatter;
      if(wg_comm_type_s == "REDUCESCATTER"){
        wg_group_type = MockNccl::GroupType::DP;
      } else if(wg_comm_type_s == "REDUCESCATTER_EP"){
        wg_group_type = MockNccl::GroupType::EP;
      } else if(wg_comm_type_s == "REDUCESCATTER_DP_EP"){
        wg_group_type = MockNccl::GroupType::DP_EP;
      } else{
        wg_group_type = MockNccl::GroupType::NONE;
      }
    }

    // generate flow model

    if (ig_comm_type_s.substr(0,9) == "ALLREDUCE") {
      ig_type = ComType::All_Reduce;
      if(ig_comm_type_s == "ALLREDUCE"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLREDUCE_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLREDUCE_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,8) == "ALLTOALL") {
      ig_type = ComType::All_to_All;
      if(ig_comm_type_s == "ALLTOALL"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLTOALL_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLTOALL_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      ig_type = ComType::All_Reduce_All_to_All;
      if(ig_comm_type_s == "ALLREDUCEALLTOALL"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,9) == "ALLGATHER") {
      ig_type = ComType::All_Gather;
      if(ig_comm_type_s == "ALLGATHER"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "ALLGATHER_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "ALLGATHER_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    } else if (ig_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      ig_type = ComType::Reduce_Scatter;
      if(ig_comm_type_s == "REDUCESCATTER"){
        ig_group_type = MockNccl::GroupType::TP;
      } else if(ig_comm_type_s == "REDUCESCATTER_EP"){
        ig_group_type = MockNccl::GroupType::EP;
      } else if(ig_comm_type_s == "REDUCESCATTER_DP_EP"){
        ig_group_type = MockNccl::GroupType::DP_EP;
      } else{
        ig_group_type = MockNccl::GroupType::NONE;
      }
    }

    if (fp_comm_type_s.substr(0,9) == "ALLREDUCE") {
      fp_type = ComType::All_Reduce;
      if(fp_comm_type_s == "ALLREDUCE"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLREDUCE_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLREDUCE_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,8) == "ALLTOALL") {
      fp_type = ComType::All_to_All;
      if(fp_comm_type_s == "ALLTOALL"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLTOALL_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLTOALL_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,17) == "ALLREDUCEALLTOALL") {
      fp_type = ComType::All_Reduce_All_to_All;
      if(fp_comm_type_s == "ALLREDUCEALLTOALL"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLREDUCEALLTOALL_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLREDUCEALLTOALL_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,9) == "ALLGATHER") {
      fp_type = ComType::All_Gather;
      if(fp_comm_type_s == "ALLGATHER"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "ALLGATHER_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "ALLGATHER_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    } else if (fp_comm_type_s.substr(0,13) == "REDUCESCATTER") {
      fp_type = ComType::Reduce_Scatter;
      if(fp_comm_type_s == "REDUCESCATTER"){
        fp_group_type = MockNccl::GroupType::TP;
      } else if(fp_comm_type_s == "REDUCESCATTER_EP"){
        fp_group_type = MockNccl::GroupType::EP;
      } else if(fp_comm_type_s == "REDUCESCATTER_DP_EP"){
        fp_group_type = MockNccl::GroupType::DP_EP;
      } else{
        fp_group_type = MockNccl::GroupType::NONE;
      }
    }
    if (generator->id == 0) {
      std::cout << "id: " << id << " , depen: " << depen
                << " , wg_comp_time: " << wg_compute_time << std::endl;
    }
    if (parallelismPolicy == ParallelismPolicy::HybridCustomized) {
      std::string specific_parallelsim;
      inFile >> specific_parallelsim;
      specific_policy = decode_parallelsim(specific_parallelsim);
    }
    if ((parallelismPolicy == ParallelismPolicy::DLRM ||
         parallelismPolicy == ParallelismPolicy::DLRMEnhanced) &&
        i == 0) {
      specific_policy = ParallelismPolicy::All;
    }
    if (specific_policy != ParallelismPolicy::None) {
      selected_involved_dimensions =
          decode_involved_dimensions(specific_policy, model_parallel_npu_group);
    } else {
      selected_involved_dimensions = general_involved_dimensions;
    }
    Layer* l = new Layer(
        id,
        i,
        generator,
        this,
        fp_compute_time * generator->compute_scale,
        fp_type,
        fp_group_type,
        fp_comm_size * generator->comm_scale,
        selected_involved_dimensions["fwd"],
        ig_compute_time * generator->compute_scale,
        ig_type,
        ig_group_type,
        ig_comm_size * generator->comm_scale,
        selected_involved_dimensions["ig"],
        wg_compute_time * generator->compute_scale,
        wg_type,
        wg_group_type,
        wg_comm_size * generator->comm_scale,
        selected_involved_dimensions["wg"],
        wg_update_time,
        specific_policy);
    if (chekpoints.find(i) != chekpoints.end()) {
      l->is_checkpoint = true;
    }
    if (need_checkpoint_initiation.find(i) !=
        need_checkpoint_initiation.end()) {
      l->needs_fwd_in_bckwd_initiation = true;
    }
    layers[i] = l;
  }
  if (generator->id == 0) {
    std::cout << "type: " << run_type << " ,num passes: " << TOTAL_PASS
              << " ,lines: " << lines
              << " compute scale: " << generator->compute_scale
              << " ,comm scale: " << generator->comm_scale << std::endl;
  }
  inFile.close();
  return true;
}
void Workload::fire() {
  call(EventType::General, NULL);
}
} // namespace AstraSim
