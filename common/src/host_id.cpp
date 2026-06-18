#include "common/host_id.h"

#include <unistd.h>

#include <fstream>

namespace mm {

namespace {
// 计算一次本机标识:/etc/machine-id 优先,回退 hostname,再回退常量。
std::string compute_host_id() {
    // 1. /etc/machine-id:systemd 机器在安装时生成,机器级稳定,跨进程一致。
    std::ifstream f("/etc/machine-id");
    if (f) {
        std::string id;
        std::getline(f, id);
        // 去掉可能的尾部空白
        while (!id.empty() && (id.back() == '\n' || id.back() == '\r' || id.back() == ' '))
            id.pop_back();
        if (!id.empty()) return id;
    }
    // 2. 回退:主机名(同机一致,跨机大概率不同)。
    char buf[256] = {0};
    if (::gethostname(buf, sizeof(buf) - 1) == 0 && buf[0] != '\0') {
        return std::string("host:") + buf;
    }
    // 3. 最后兜底:固定串(退化为"全部视为同机",至少不崩)。
    return "unknown-host";
}
}  // namespace

const std::string& local_host_id() {
    static const std::string id = compute_host_id();   // 首次调用计算并缓存
    return id;
}

}  // namespace mm
