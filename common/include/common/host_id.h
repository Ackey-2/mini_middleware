#pragma once

#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// local_host_id():本机的机器级稳定标识。
// 优先读 /etc/machine-id(同一台机器上所有进程一致),失败则回退 gethostname()。
// 进程内缓存,首次调用后恒定。
//
// 用途(Phase 4):发现公告携带它;数据面比较本地与远端 host_id 是否相等,
// 相等即"同主机" → 走共享内存零拷贝,否则走 TCP。
// ═══════════════════════════════════════════════════════════════
const std::string& local_host_id();

}  // namespace mm
