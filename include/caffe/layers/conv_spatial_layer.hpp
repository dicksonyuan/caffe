#ifdef USE_INTEL_SPATIAL
#ifndef CAFFE_CONV_SPATIAL_LAYER_HPP_
#define CAFFE_CONV_SPATIAL_LAYER_HPP_

#include <map>
#include <string>
#include <vector>

#include "caffe/blob.hpp"
#include "caffe/layer.hpp"
#include "caffe/proto/caffe.pb.h"

#include "caffe/layers/base_conv_layer.hpp"

namespace caffe {
enum class ConvType: int_tp {IDLF = 2, WINOGRAD = 3, BASIC = 4, GEMM_LIKE = 5, DWCONV = 6};
template<typename Dtype>
class ConvolutionLayerSpatial : public BaseConvolutionLayer<Dtype> {
 public:
  /**
   * @param param provides ConvolutionParameter convolution_param,
   *    with ConvolutionLayer options:
   *  - num_output. The number of filters.
   *  - kernel_size / kernel_h / kernel_w. The filter dimensions, given by
   *  kernel_size for square filters or kernel_h and kernel_w for rectangular
   *  filters.
   *  - stride / stride_h / stride_w (\b optional, default 1). The filter
   *  stride, given by stride_size for equal dimensions or stride_h and stride_w
   *  for different strides. By default the convolution is dense with stride 1.
   *  - pad / pad_h / pad_w (\b optional, default 0). The zero-padding for
   *  convolution, given by pad for equal dimensions or pad_h and pad_w for
   *  different padding. Input padding is computed implicitly instead of
   *  actually padding.
   *  - group (\b optional, default 1). The number of filter groups. Group
   *  convolution is a method for reducing parameterization by selectively
   *  connecting input and output channels. The input and output channel dimensions must be divisible
   *  by the number of groups. For group @f$ \geq 1 @f$, the
   *  convolutional filters' input and output channels are separated s.t. each
   *  group takes 1 / group of the input channels and makes 1 / group of the
   *  output channels. Concretely 4 input channels, 8 output channels, and
   *  2 groups separate input channels 1-2 and output channels 1-4 into the
   *  first group and input channels 3-4 and output channels 5-8 into the second
   *  group.
   *  - bias_term (\b optional, default true). Whether to have a bias.
   *  - engine: convolution has CAFFE (matrix multiplication) and CUDNN (library
   *    kernels + stream parallelism) engines.
   */
  explicit ConvolutionLayerSpatial(const LayerParameter& param)
      : BaseConvolutionLayer<Dtype>(param) {
    winograd_weights_image_ = NULL;
    bestKernelConfig = NULL;
    pretuned_key_ = {0};
  }
  virtual void LayerSetUp(const vector<Blob<Dtype>*>& bottom,
                          const vector<Blob<Dtype>*>& top);
  virtual void Reshape(const vector<Blob<Dtype>*>& bottom,
                       const vector<Blob<Dtype>*>& top);

  virtual inline const char* type() const {
    return "Convolution";
  }

  virtual inline int_tp MinBottomBlobs() const {
    return 1;
  }
  virtual inline int_tp MinTopBlobs() const {
    return 1;
  }
  virtual inline bool EqualNumBottomTopBlobs() const {
    return (IsFusedWithEltwise() || IsFusedWithPReLU()) ? false : true;
  }

#ifdef USE_GREENTEA
  ~ConvolutionLayerSpatial() {
    if (winograd_weights_image_)
      clReleaseMemObject(winograd_weights_image_);
    winograd_weights_image_ = NULL;
  }
#endif
 protected:
  virtual void Forward_cpu(const vector<Blob<Dtype>*>& bottom,
                           const vector<Blob<Dtype>*>& top);

  virtual void Forward_gpu(const vector<Blob<Dtype>*>& bottom,
                           const vector<Blob<Dtype>*>& top);

  virtual void Backward_cpu(const vector<Blob<Dtype>*>& top,
                            const vector<bool>& propagate_down,
                            const vector<Blob<Dtype>*>& bottom);

  virtual void Backward_gpu(const vector<Blob<Dtype>*>& top,
                            const vector<bool>& propagate_down,
                            const vector<Blob<Dtype>*>& bottom);

  virtual inline bool reverse_dimensions() {
    return false;
  }
  virtual void compute_output_shape();

  struct PretunedKey{
    //todo: change bits and possible orders
    const static int EU_BITS = 8;
    const static int KERNEL_W_BITS = 8;
    const static int KERNEL_H_BITS = 8;
    const static int PAD_W_BITS = 8;
    const static int PAD_H_BITS = 8;
    const static int STRIDE_W_BITS = 8;
    const static int STRIDE_H_BITS = 8;
    const static int DILATION_W_BITS = 8;
    const static int DILATION_H_BITS = 8;
    const static int GROUP_BITS = 8;
    const static int INPUT_CHANNEL_BITS = 16;
    const static int INPUT_W_BITS = 16;
    const static int INPUT_H_BITS = 16;
    const static int BATCH_SIZE_BITS = 16;
    const static int OUTPUT_CHANNEL_PER_GROUP_BITS = 16;
    const static int FUSE_TYPE_BITS = 6;
    const static int HAS_BIAS_BITS = 1;
    const static int DATA_TYPE_BITS = 1;

    uint32_t eu:            EU_BITS;
    uint32_t kernel_w:      KERNEL_W_BITS;
    uint32_t kernel_h:      KERNEL_H_BITS;
    uint32_t pad_w:         PAD_W_BITS;
    uint32_t pad_h:         PAD_H_BITS;
    uint32_t stride_w:      STRIDE_W_BITS;
    uint32_t stride_h:      STRIDE_H_BITS;
    uint32_t dilation_w:    DILATION_W_BITS;
    uint32_t dilation_h:    DILATION_H_BITS;
    uint32_t group:         GROUP_BITS;
    uint32_t input_channel: INPUT_CHANNEL_BITS;
    uint32_t input_w:       INPUT_W_BITS;
    uint32_t input_h:       INPUT_H_BITS;
    uint32_t batch_size:    BATCH_SIZE_BITS;
    uint32_t output_channel_per_group:OUTPUT_CHANNEL_PER_GROUP_BITS;
    uint32_t fuse_type:     FUSE_TYPE_BITS;
    uint32_t has_bias:      HAS_BIAS_BITS;
    uint32_t data_type:     DATA_TYPE_BITS;   // 1: float;  0: half

    void set(uint32_t euv, uint32_t kernel_wv, uint32_t kernel_hv, uint32_t pad_wv, uint32_t pad_hv,
             uint32_t stride_wv, uint32_t stride_hv, uint32_t dilation_wv, uint32_t dilation_hv,
             uint32_t groupv, uint32_t input_channelv, uint32_t input_wv, uint32_t input_hv,
             uint32_t batch_sizev, uint32_t output_channel_per_groupv, uint32_t fuse_typev, uint32_t has_biasv) {
      //data_type is set outside

      CHECK(euv < (1<<EU_BITS));
      CHECK(kernel_wv < (1<<KERNEL_W_BITS));
      CHECK(kernel_hv < (1<<KERNEL_H_BITS));
      CHECK(pad_wv < (1<<PAD_W_BITS));
      CHECK(pad_hv < (1<<PAD_H_BITS));
      CHECK(stride_wv < (1<<STRIDE_W_BITS));
      CHECK(stride_hv < (1<<STRIDE_H_BITS));
      CHECK(dilation_wv < (1<<DILATION_W_BITS));
      CHECK(dilation_hv < (1<<DILATION_H_BITS));
      CHECK(groupv < (1<<GROUP_BITS));
      CHECK(input_channelv < (1<<INPUT_CHANNEL_BITS));
      CHECK(input_wv < (1<<INPUT_W_BITS));
      CHECK(input_hv < (1<<INPUT_H_BITS));
      CHECK(batch_sizev < (1<<BATCH_SIZE_BITS));
      CHECK(output_channel_per_groupv < (1<<OUTPUT_CHANNEL_PER_GROUP_BITS));
      CHECK(fuse_typev < (1<<FUSE_TYPE_BITS));
      CHECK(has_biasv < (1<<HAS_BIAS_BITS));

      eu = euv;
      kernel_w = kernel_wv;
      kernel_h = kernel_hv;
      pad_w = pad_wv;
      pad_h = pad_hv;
      stride_w = stride_wv;
      stride_h = stride_hv;
      dilation_w = dilation_wv;
      dilation_h = dilation_hv;
      group = groupv;
      input_channel = input_channelv;
      input_w = input_wv;
      input_h = input_hv;
      batch_size = batch_sizev;
      output_channel_per_group = output_channel_per_groupv;
      fuse_type = fuse_typev;
      has_bias = has_biasv;
    }

#define TIE(v) \
        std::tie(v.eu, v.kernel_w, v.kernel_h, v.pad_w, v.pad_h,                \
                 v.stride_w, v.stride_h, v.dilation_w, v.dilation_h,            \
                 v.group, v.input_channel, v.input_w, v.input_h, v.batch_size,  \
                 v.output_channel_per_group, v.fuse_type, v.has_bias, v.data_type)
    bool operator<(const PretunedKey& rhs) const {
      return TIE((*this)) < TIE(rhs);
    }

    bool operator==(const PretunedKey& rhs) const {
      return TIE((*this)) == TIE(rhs);
    }
#undef TIE

    // make sure that the output order should be the same as declaration
    std::string str() const
    {
      std::stringstream ss;
      ss << "{"
         << eu << ","
         << kernel_w << ","
         << kernel_h << ","
         << pad_w << ","
         << pad_h << ","
         << stride_w << ","
         << stride_h << ","
         << dilation_w << ","
         << dilation_h << ","
         << group << ","
         << input_channel << ","
         << input_w << ","
         << input_h << ","
         << batch_size << ","
         << output_channel_per_group << ","
         << fuse_type << ","
         << has_bias << ","
         << data_type
         << "}";
      return ss.str();
    }
  };

  struct PretunedValue {
    const static int BLOCK_W_BITS = 8;
    const static int BLOCK_H_BITS = 8;
    const static int BLOCK_D_BITS = 8;
    const static int LOCAL_SIZE_X_BITS = 8;
    const static int LOCAL_SIZE_Y_BITS = 8;
    const static int LOCAL_SIZE_Z_BITS = 8;
    const static int KERNEL_TYPE_BITS = 3;

    uint32_t block_w:       BLOCK_W_BITS;
    uint32_t block_h:       BLOCK_H_BITS;
    uint32_t block_d:       BLOCK_D_BITS;         //simd_size
    uint32_t local_size_x:  LOCAL_SIZE_X_BITS;
    uint32_t local_size_y:  LOCAL_SIZE_Y_BITS;
    uint32_t local_size_z:  LOCAL_SIZE_Z_BITS;
    uint32_t kernel_type:   KERNEL_TYPE_BITS;

    void set(uint32_t block_wv, uint32_t block_hv, uint32_t block_dv,
             uint32_t local_size_xv, uint32_t local_size_yv, uint32_t local_size_zv,
             uint32_t kernel_typev) {
      CHECK(block_wv < (1<< BLOCK_W_BITS));
      CHECK(block_hv < (1<< BLOCK_H_BITS));
      CHECK(block_dv < (1<< BLOCK_D_BITS));
      CHECK(local_size_xv < (1<<LOCAL_SIZE_X_BITS));
      CHECK(local_size_yv < (1<<LOCAL_SIZE_Y_BITS));
      CHECK(local_size_zv < (1<<LOCAL_SIZE_Z_BITS));
      CHECK(kernel_typev < (1<<KERNEL_TYPE_BITS));

      block_w = block_wv;
      block_h = block_hv;
      block_d = block_dv;
      local_size_x = local_size_xv;
      local_size_y = local_size_yv;
      local_size_z = local_size_zv;
      kernel_type = kernel_typev;
    }

    bool operator==(const PretunedValue& rhs) const {
      return std::tie(block_w, block_h, block_d, local_size_x, local_size_y, local_size_z, kernel_type)
             ==
             std::tie(rhs.block_w, rhs.block_h, rhs.block_d,
                      rhs.local_size_x, rhs.local_size_y, rhs.local_size_z,
                      rhs.kernel_type);
    }

    // make sure that the output order should be the same as declaration
    std::string str() const
    {
      std::stringstream ss;
      ss << "{"
         << block_w << ","
         << block_h << ","
         << block_d << ","
         << local_size_x << ","
         << local_size_y << ","
         << local_size_z << ","
         << kernel_type
         << "}";
      return ss.str();
    }
  };

  // to save space, consider to merge key and value into one struct and use array
  static std::map<PretunedKey, PretunedValue> pretuned_kv;

  struct kernelConfig {
    string kernelName;
    float executionTime;
    size_t local_work_size[3];
    size_t global_work_size[3];
    int_tp workItem_output[3];
    bool verified;
    bool autoTune;
    bool tested;
    bool swizzle_weights;
    bool use_null_local;
    ConvType kernelType;

    kernelConfig() {
      kernelType = ConvType::BASIC;
    }
    kernelConfig(string name, size_t* global_size, size_t* local_size,
                 int_tp* workItem,
                 bool tune, bool swizzle, bool null_local,
                 ConvType type = ConvType::BASIC) {
      kernelName = name;
      for (int_tp x = 0; x < 3; x++) {
        local_work_size[x] = local_size[x];
        global_work_size[x] = global_size[x];
        workItem_output[x] = workItem[x];
      }
      autoTune = tune;
      swizzle_weights = swizzle;
      use_null_local = null_local;
      verified = false;
      tested = false;
      kernelType = type;
    }
  };

#ifndef CPU_ONLY
#ifdef USE_GREENTEA
  virtual void setup_convolution(const vector<Blob<Dtype>*>& bottom,
                                 const vector<Blob<Dtype>*>& top,
                                 const Blob<Dtype> &verify_blob);
  virtual void create_convolution_kernel(const vector<Blob<Dtype>*>& bottom,
                                         const vector<Blob<Dtype>*>& top,
                                         ConvType kernelType,
                                         int_tp blockWidth,
                                         int_tp blockHeight,
                                         int_tp blockDepth);
  virtual bool setup_IDLF(const vector<Blob<Dtype>*>& bottom,
                          const vector<Blob<Dtype>*>& top, int_tp blockWidth,
                          int_tp blockHeight,
                          int_tp blockDepth);
  virtual bool create_basic_kernel(const vector<Blob<Dtype>*>& bottom,
                                   const vector<Blob<Dtype>*>& top,
                                   int_tp blockWidth,
                                   int_tp blockHeight,
                                   int_tp blockDepth);
  virtual bool create_gemm_like_conv_kernel(const vector<Blob<Dtype>*>& bottom,
                                   const vector<Blob<Dtype>*>& top,
                                   int_tp blockWidth,
                                   int_tp blockHeight,
                                   int_tp blockDepth);
  virtual bool create_winograd_conv_kernel(const vector<Blob<Dtype>*>& bottom,
                                   const vector<Blob<Dtype>*>& top,
                                   int_tp blockWidth,
                                   int_tp blockHeight,
                                   int_tp blockDepth);
  virtual bool create_dw_conv_kernel(const vector<Blob<Dtype>*>& bottom,
                                   const vector<Blob<Dtype>*>& top,
                                   int_tp blockWidth,
                                   int_tp blockHeight,
                                   int_tp blockDepth);

  virtual cl_int convolve(const vector<Blob<Dtype>*>& bottom,
                          const vector<Blob<Dtype>*>& top, int_tp index,
                          int_tp numImages,
                          kernelConfig* config);
  virtual float timed_convolve(const vector<Blob<Dtype>*>& bottom,
                               const vector<Blob<Dtype>*>& top, int_tp index,
                               int_tp numImages,
                               kernelConfig* config);
  virtual bool verify_result(const vector<Blob<Dtype>*>& bottom,
                             const vector<Blob<Dtype>*>& top, int_tp index,
                             int_tp numImages, const Blob<Dtype> &verify_blob,
                             kernelConfig* config);
  virtual bool tune_local_size(const vector<Blob<Dtype>*>& bottom,
                               const vector<Blob<Dtype>*>& top, kernelConfig*);
  virtual void swizzleWeights(const vector<Blob<Dtype>*>& bottom,
                              const vector<Blob<Dtype>*>& top,
                              int_tp swizzle_factor,
                              bool interleave = false);
  virtual void winogradWeights();
  virtual void generate_key();
  virtual std::string generate_specific_key(ConvType type, int_tp blockWidth,
  int_tp blockHeight,
                                            int_tp blockDepth);
  virtual void calculate_global_size(int_tp batch, int_tp* workItemOutput,
                                     size_t* localSizes, size_t* globalSizes);
  void load_cached_kernels(const vector<Blob<Dtype>*>& bottom,
                           const vector<Blob<Dtype>*>& top);
  bool need_swizzle(const kernelConfig &prev,
                    const kernelConfig &cur);
  void SetUp(const vector<Blob<Dtype>*>& bottom,
             const vector<Blob<Dtype>*>& top, caffe::Backend backend);
  void setBufferKernelArg(const vector<Blob<Dtype>*>& bottom,
                          const vector<Blob<Dtype>*>& top,
                          viennacl::ocl::kernel *cl_kernel,
                          const cl_uint &argIdx,
                          viennacl::ocl::context *ctx,
                          cl_mem buffer, size_t offset,
                          size_t size, bool readOnly,
                          bool preserved);
  void cleanTmpSubBuffers(const vector<Blob<Dtype>*>& bottom,
                          const vector<Blob<Dtype>*>& top);
  std::map<std::tuple<cl_mem, size_t, size_t>, cl_mem> subBufferMap;
  std::vector<cl_mem> tmpSubBuffers;

  bool IsFused() const {
    return (this->layer_param_.convolution_param().fuse_type()
            != ConvolutionParameter_FuseType_UNFUSED);
  }

  bool IsFusedWithMaxPoolAndReLU() const {
    return (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_MAX_POOLING_RELU);
  }

  bool IsFusedWithEltwiseReLU() const {
    return (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_ELTWISE_RELU);
  }

  bool IsFusedWithEltwisePReLU() const {
    return (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_ELTWISE_PRELU);
  }

  bool IsFusedWithReLU() const {
    return IsFusedWithEltwiseReLU() ||
           (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_RELU);
  }

  bool IsFusedWithPReLU() const {
    return IsFusedWithEltwisePReLU() ||
           (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_PRELU);
  }

  bool IsFusedWithEltwise() const {
    return IsFusedWithEltwiseReLU() || IsFusedWithEltwisePReLU() ||
           (this->layer_param_.convolution_param().fuse_type()
            == ConvolutionParameter_FuseType_FUSED_CONV_ELTWISE);
  }
#endif
#endif

  const Dtype* bottom_data;
  Dtype* top_data;
  const Dtype* weight;
  const Dtype* weight_cpu;
  Dtype* swizzled_weights_;
  int_tp weight_offset;
  int_tp col_offset;
  int_tp top_offset;
  int_tp output_h_, output_w_;
  const Dtype* bias_;
  int_tp bias_offset_;
  int_tp bottom_index_;

  int_tp kernel_h_;
  int_tp kernel_w_;
  int_tp height_;
  int_tp width_;
  int_tp pad_h_;
  int_tp pad_w_;
  int_tp stride_h_;
  int_tp stride_w_;
  int_tp dilation_h_;
  int_tp dilation_w_;

  /// M_ is the channel dimension of the output for a single group, which is the
  /// leading dimension of the filter matrix.
  int_tp M_;
  /// K_ is the dimension of an unrolled input for a single group, which is the
  /// leading dimension of the data matrix.
  int_tp K_;
  /// N_ is the spatial dimension of the output, the H x W, which are the last
  /// dimensions of the data and filter matrices.
  int_tp N_;

  bool tuned_;
  bool dwconv_;

  std::string old_key_;
  std::string key_;
  std::string key_text_;
  std::string kernel_name_;
  std::stringstream cache_path_;
  PretunedKey pretuned_key_;

  Blob<Dtype> input_transform_blob_;
  cl_mem winograd_weights_image_;

  Blob<Dtype> swizzled_weights_blob_;
  Blob<Dtype> bias_multiplier_;

  int_tp kernel_index_;
  int_tp kernel_uid_;

  vector<kernelConfig*> kernelQueue;
  kernelConfig* bestKernelConfig;

  // parameters for fused eltwise layer.
  EltwiseParameter_EltwiseOp op_;
  vector<Dtype> coeffs_;
  Blob<int_tp> max_idx_;
  // parameter for relu
  Dtype negative_slope_;

  bool stable_prod_grad_;
};
}  // namespace caffe

#endif  // CAFFE_CONV_SPATIAL_LAYER_HPP_
#endif  // USE_INTEL_SPATIAL
