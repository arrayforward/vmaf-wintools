#pragma once

#include "frame.h"

#include <memory>
#include <string>
#include <vector>

namespace vt {

struct VmafConfig {
    std::string modelName = "vmaf_v0.6.1"; // 内置模型名, 或 .json 路径
    int threads = 0;                       // 0 = 自动
    int subsample = 1;
};

class VmafEvaluator {
public:
    VmafEvaluator();
    ~VmafEvaluator();

    bool init(const VmafConfig& cfg, int w, int h, std::string& err);

    // 送入一对帧 (尺寸必须与 init 时一致)
    bool pushFramePair(const Frame& ref, const Frame& dist, std::string& err);

    // 结束送帧并 flush, 之后可查询所有帧分数
    bool flush(std::string& err);

    // 尝试取第 index 帧分数, 未就绪返回 false
    bool scoreAt(int index, double& score);

    bool pooledScore(double& mean, double& harmonicMean, double& minScore, double& maxScore);

    int frameCount() const { return pushed_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int pushed_ = 0;
};

} // namespace vt
