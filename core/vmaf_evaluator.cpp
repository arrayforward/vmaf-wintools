#include "vmaf_evaluator.h"

#include <libvmaf/libvmaf.h>
#include <libvmaf/model.h>
#include <libvmaf/picture.h>

#include <algorithm>
#include <cstring>

namespace vt {

struct VmafEvaluator::Impl {
    VmafContext* ctx = nullptr;
    VmafModel* model = nullptr;
    int w = 0, h = 0;
    int flushed = 0;
    int lastIndex = -1;
};

VmafEvaluator::VmafEvaluator() : impl_(std::make_unique<Impl>()) {}

VmafEvaluator::~VmafEvaluator() {
    if (impl_->model) vmaf_model_destroy(impl_->model);
    if (impl_->ctx) vmaf_close(impl_->ctx);
}

bool VmafEvaluator::init(const VmafConfig& cfg, int w, int h, std::string& err) {
    impl_->w = w;
    impl_->h = h;

    VmafConfiguration vcfg = {};
    vcfg.log_level = VMAF_LOG_LEVEL_NONE;
    vcfg.n_threads = cfg.threads > 0 ? static_cast<unsigned>(cfg.threads) : 0;
    vcfg.n_subsample = cfg.subsample > 0 ? static_cast<unsigned>(cfg.subsample) : 1;
    vcfg.cpumask = 0;

    if (vmaf_init(&impl_->ctx, vcfg) != 0) {
        err = "vmaf_init 失败";
        return false;
    }

    VmafModelConfig mcfg = {};
    mcfg.flags = VMAF_MODEL_FLAGS_DEFAULT;

    int ret;
    const bool isPath = cfg.modelName.find(".json") != std::string::npos;
    if (isPath)
        ret = vmaf_model_load_from_path(&impl_->model, &mcfg, cfg.modelName.c_str());
    else
        ret = vmaf_model_load(&impl_->model, &mcfg, cfg.modelName.c_str());
    if (ret != 0) {
        err = "模型加载失败: " + cfg.modelName +
              (isPath ? "" : " (内置模型需要 libvmaf 编译时启用 built_in_models)");
        return false;
    }
    if (vmaf_use_features_from_model(impl_->ctx, impl_->model) != 0) {
        err = "vmaf_use_features_from_model 失败";
        return false;
    }
    return true;
}

static void fillPicture(VmafPicture& pic, const Frame& f) {
    vmaf_picture_alloc(&pic, VMAF_PIX_FMT_YUV420P, 8, f.width, f.height);
    for (int r = 0; r < f.height; ++r)
        memcpy(static_cast<uint8_t*>(pic.data[0]) + static_cast<size_t>(r) * pic.stride[0],
               f.y.data() + static_cast<size_t>(r) * f.strideY, f.width);
    const int cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
    for (int r = 0; r < ch; ++r) {
        memcpy(static_cast<uint8_t*>(pic.data[1]) + static_cast<size_t>(r) * pic.stride[1],
               f.u.data() + static_cast<size_t>(r) * f.strideU, cw);
        memcpy(static_cast<uint8_t*>(pic.data[2]) + static_cast<size_t>(r) * pic.stride[2],
               f.v.data() + static_cast<size_t>(r) * f.strideV, cw);
    }
}

bool VmafEvaluator::pushFramePair(const Frame& ref, const Frame& dist, std::string& err) {
    if (ref.width != impl_->w || ref.height != impl_->h || dist.width != impl_->w ||
        dist.height != impl_->h) {
        err = "帧尺寸与初始化不一致";
        return false;
    }
    VmafPicture picRef = {}, picDist = {};
    fillPicture(picRef, ref);
    fillPicture(picDist, dist);
    const int ret = vmaf_read_pictures(impl_->ctx, &picRef, &picDist, pushed_);
    vmaf_picture_unref(&picRef);
    vmaf_picture_unref(&picDist);
    if (ret != 0) {
        err = "vmaf_read_pictures 失败";
        return false;
    }
    ++pushed_;
    return true;
}

bool VmafEvaluator::flush(std::string& err) {
    if (impl_->flushed) return true;
    if (vmaf_read_pictures(impl_->ctx, nullptr, nullptr, 0) != 0) {
        err = "vmaf flush 失败";
        return false;
    }
    impl_->flushed = 1;
    impl_->lastIndex = pushed_ - 1;
    return true;
}

bool VmafEvaluator::scoreAt(int index, double& score) {
    if (index < 0 || index >= pushed_) return false;
    return vmaf_score_at_index(impl_->ctx, impl_->model, &score, static_cast<unsigned>(index)) == 0;
}

bool VmafEvaluator::pooledScore(double& mean, double& harmonicMean, double& minScore,
                                double& maxScore) {
    if (!impl_->flushed || pushed_ == 0) return false;
    const unsigned lo = 0, hi = static_cast<unsigned>(pushed_ - 1);
    if (vmaf_score_pooled(impl_->ctx, impl_->model, VMAF_POOL_METHOD_MEAN, &mean, lo, hi) != 0)
        return false;
    if (vmaf_score_pooled(impl_->ctx, impl_->model, VMAF_POOL_METHOD_HARMONIC_MEAN, &harmonicMean,
                          lo, hi) != 0)
        return false;
    if (vmaf_score_pooled(impl_->ctx, impl_->model, VMAF_POOL_METHOD_MIN, &minScore, lo, hi) != 0)
        return false;
    if (vmaf_score_pooled(impl_->ctx, impl_->model, VMAF_POOL_METHOD_MAX, &maxScore, lo, hi) != 0)
        return false;
    return true;
}

} // namespace vt
