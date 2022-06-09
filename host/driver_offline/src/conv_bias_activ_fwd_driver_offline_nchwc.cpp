#include <iostream>
#include <numeric>
#include <initializer_list>
#include <cstdlib>
#include <stdlib.h>
#include <half.hpp>
#include "config.hpp"
#include "debug.hpp"
#include "print.hpp"
#include "device.hpp"
#include "host_tensor.hpp"
#include "host_tensor_generator.hpp"
#include "conv_common.hpp"
#include "device_tensor.hpp"
#include "device_convolution_bias_activ_forward_implicit_gemm_v5r1_dlops_nc0hwc1_kc0yxc1_nk0hwk1.hpp"
#include "ck_conv_fig.h"

#define USE_DYNAMIC_MODE 0
#define USE_CONV_FWD_V5R1_NCHWC 1

enum ConvForwardAlgo
{
    V5R1NCHWC // 0
};

template <typename TIn,
          typename TWei,
          typename TBias,
          typename TScale,
          typename TOut,
          typename TOutPacked,
          typename ConvStrides,
          typename ConvDilations,
          typename InLeftPads,
          typename InRightPads,
          typename CElementwiseOp>
void host_direct_convolution_nchwc(const Tensor<TIn>& in,
                                   const Tensor<TWei>& wei,
                                   const Tensor<TBias>& bias,
                                   const Tensor<TScale>& scale,
                                   Tensor<TOut>& out,
                                   Tensor<TOutPacked>& out_pack,
                                   const ConvStrides& conv_strides,
                                   const ConvDilations& conv_dilations,
                                   const InLeftPads& in_left_pads,
                                   const InRightPads&,
                                   const CElementwiseOp c_elementwise_op)
{
    using namespace ck;

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};

    auto f_nchw = [&](auto n, auto k0, auto ho, auto wo, auto k1) {
        float v     = 0;
        const int k = k0 * out.mDesc.GetLengths()[4] + k1;

        for(int c0 = 0; c0 < wei.mDesc.GetLengths()[1]; ++c0)
        {
            for(int y = 0; y < wei.mDesc.GetLengths()[2]; ++y)
            {
                int hi = ho * conv_strides[I0] + y * conv_dilations[I0] - in_left_pads[I0];
                for(int x = 0; x < wei.mDesc.GetLengths()[3]; ++x)
                {
                    int wi = wo * conv_strides[I1] + x * conv_dilations[I1] - in_left_pads[I1];
                    if(hi >= 0 && hi < in.mDesc.GetLengths()[2] && wi >= 0 &&
                       wi < in.mDesc.GetLengths()[3])
                    {
                        for(int c1 = 0; c1 < wei.mDesc.GetLengths()[4]; ++c1)
                        {
                            if(is_same<TIn, int4x2_t>::value)
                            {
                                int8x2_t ax2 = type_convert<int8x2_t>(in(n, c0, hi, wi, c1));
                                int8x2_t bx2 = type_convert<int8x2_t>(wei(k, c0, y, x, c1));

                                int8_t a0 = vector_type<int8_t, 2>{ax2}.AsType<int8_t>()[I0];
                                int8_t a1 = vector_type<int8_t, 2>{ax2}.AsType<int8_t>()[I1];

                                int8_t b0 = vector_type<int8_t, 2>{bx2}.AsType<int8_t>()[I0];
                                int8_t b1 = vector_type<int8_t, 2>{bx2}.AsType<int8_t>()[I1];

                                // std::cout << "a{" << a0 << "," << a1 << "} b{" << b0 << "," << b1
                                //<< "}" << std::endl;

                                v += a0 * b0;
                                v += a1 * b1;
                            }
                            else
                            {
                                v += type_convert<float>(in(n, c0, hi, wi, c1)) *
                                     type_convert<float>(wei(k, c0, y, x, c1));
                            }
                        }
                    }
                }
            }
        }
        v += bias(k0, k1);
        const auto c_elementwise_op_ = CElementwiseOp{scale(k0, k1)};
        out(n, k0, ho, wo, k1)       = c_elementwise_op_(v);
    };

    make_ParallelTensorFunctor(f_nchw,
                               out.mDesc.GetLengths()[0],
                               out.mDesc.GetLengths()[1],
                               out.mDesc.GetLengths()[2],
                               out.mDesc.GetLengths()[3],
                               out.mDesc.GetLengths()[4])(std::thread::hardware_concurrency());

    auto f_pack = [&](auto n, auto k0, auto ho, auto wo) {
        for(int k1 = 0; k1 < out_pack.mDesc.GetLengths()[4]; ++k1)
        {
            int8_t low  = out(n, k0, ho, wo, k1 * 2);
            int8_t high = out(n, k0, ho, wo, k1 * 2 + 1);

            ck::int8x2_t tmp{low, high};

            ck::int4x2_t v = ck::type_convert<ck::int4x2_t>(tmp);

            out_pack(n, k0, ho, wo, k1) = v;
        }
    };

    make_ParallelTensorFunctor(f_pack,
                               out_pack.mDesc.GetLengths()[0],
                               out_pack.mDesc.GetLengths()[1],
                               out_pack.mDesc.GetLengths()[2],
                               out_pack.mDesc.GetLengths()[3])(std::thread::hardware_concurrency());
}

int main(int argc, char* argv[])
{
    using namespace ck;

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};
    // constexpr auto I2 = Number<2>{};
    // constexpr auto I3 = Number<3>{};
    // constexpr auto I4 = Number<4>{};
    // constexpr auto I5 = Number<5>{};
    // constexpr auto I6 = Number<6>{};

#if USE_DYNAMIC_MODE
    // dynamic mode
    if(argc != 23)
        v, activ_type
        {
            printf("arg1 to 5: algo, do_verification, init_method, do_log, nrepeat\n");
            printf(
                "rest: N, K0, K1, C0, C1, Y, X, Hi, Wi, Sy, Sx, Dy, Dx, LeftPy, LeftPx, RightPy, "
                "RightPx\n");
            exit(1);
        }

    constexpr ck::ActivTypeEnum_t activ_type = ActivTypeEnum_t::LeakyRelu;

    const ConvForwardAlgo algo = static_cast<ConvForwardAlgo>(std::stoi(argv[1]));
    const bool do_verification = std::stoi(argv[2]);
    const int init_method      = std::stoi(argv[3]);
    const bool do_log          = std::stoi(argv[4]);
    const int nrepeat          = std::stoi(argv[5]);

    const index_t N  = std::stoi(argv[6]);
    const index_t K0 = std::stoi(argv[7]);
    const index_t K1 = std::stoi(argv[8]);
    const index_t C0 = std::stoi(argv[9]);
    const index_t C1 = std::stoi(argv[10]);
    const index_t Y  = std::stoi(argv[11]);
    const index_t X  = std::stoi(argv[12]);
    const index_t Hi = std::stoi(argv[13]);
    const index_t Wi = std::stoi(argv[14]);

    const index_t conv_stride_h   = std::stoi(argv[15]);
    const index_t conv_stride_w   = std::stoi(argv[16]);
    const index_t conv_dilation_h = std::stoi(argv[17]);
    const index_t conv_dilation_w = std::stoi(argv[18]);
    const index_t in_left_pad_h   = std::stoi(argv[19]);
    const index_t in_left_pad_w   = std::stoi(argv[20]);
    const index_t in_right_pad_h  = std::stoi(argv[21]);
    const index_t in_right_pad_w  = std::stoi(argv[22]);

    const index_t YEff = (Y - 1) * conv_dilation_h + 1;
    const index_t XEff = (X - 1) * conv_dilation_w + 1;

    const index_t Ho = (Hi + in_left_pad_h + in_right_pad_h - YEff) / conv_stride_h + 1;
    const index_t Wo = (Wi + in_left_pad_w + in_right_pad_w - XEff) / conv_stride_w + 1;
#else
    // static mode
    if(argc < 6)
    {
        printf("arg1 to 5: algo, do_verification, init_method, do_log, nrepeat\n");
        exit(1);
    }

    const ConvForwardAlgo algo = static_cast<ConvForwardAlgo>(std::stoi(argv[1]));

    const bool do_verification = std::stoi(argv[2]);
    const int init_method      = std::stoi(argv[3]);
    const bool do_log          = std::stoi(argv[4]);
    const int nrepeat          = std::stoi(argv[5]);

#if USE_CONV_FIG
    constexpr auto N           = Number<CONV_N>{};
    constexpr auto Hi          = Number<CONV_HI>{};
    constexpr auto Wi          = Number<CONV_WI>{};
    constexpr auto Y           = Number<CONV_Y>{};
    constexpr auto X           = Number<CONV_X>{};
    constexpr auto C0          = Number<CONV_C0>{};
    constexpr auto C1          = Number<CONV_C1>{};
    constexpr auto K0          = Number<CONV_K0>{};
    constexpr auto K1          = Number<CONV_K1>{};

    constexpr auto conv_stride_h   = Number<CONV_STRIDE_H>{};
    constexpr auto conv_stride_w   = Number<CONV_STRIDE_W>{};
    constexpr auto conv_dilation_h = Number<CONV_DILATION_H>{};
    constexpr auto conv_dilation_w = Number<CONV_DILATION_W>{};

    constexpr auto in_left_pad_h  = Number<CONV_IN_LEFT_PAD_H>{};
    constexpr auto in_left_pad_w  = Number<CONV_IN_LEFT_PAD_W>{};
    constexpr auto in_right_pad_h = Number<CONV_IN_RIGHT_PAD_H>{};
    constexpr auto in_right_pad_w = Number<CONV_IN_RIGHT_PAD_W>{};

    constexpr ck::ActivTypeEnum_t activ_type = ActivTypeEnum_t::CONV_ACTIV;
#else
    constexpr auto N  = Number<1>{};
    constexpr auto Hi = Number<270>{};
    constexpr auto Wi = Number<480>{};
    constexpr auto Y  = Number<3>{};
    constexpr auto X  = Number<3>{};
    constexpr auto C0 = Number<2>{};
    constexpr auto C1 = Number<8>{};
    constexpr auto K0 = Number<2>{};
    constexpr auto K1 = Number<8>{};

    constexpr auto conv_stride_h   = I1;
    constexpr auto conv_stride_w   = I1;
    constexpr auto conv_dilation_h = I1;
    constexpr auto conv_dilation_w = I1;

    constexpr auto in_left_pad_h  = I1;
    constexpr auto in_left_pad_w  = I1;
    constexpr auto in_right_pad_h = I1;
    constexpr auto in_right_pad_w = I1;

    constexpr ck::ActivTypeEnum_t activ_type = ActivTypeEnum_t::LeakyRelu;
#endif

    constexpr auto YEff = (Y - I1) * conv_dilation_h + I1;
    constexpr auto XEff = (X - I1) * conv_dilation_w + I1;

    constexpr auto Ho = (Hi + in_left_pad_h + in_right_pad_h - YEff) / conv_stride_h + I1;
    constexpr auto Wo = (Wi + in_left_pad_w + in_right_pad_w - XEff) / conv_stride_w + I1;
#endif

#if 0
    using in_data_t  = float;
    using acc_data_t = float;
    using out_data_t = float;
#elif 0
    using in_data_t   = half_t;
    using acc_data_t  = float;
    using out_data_t  = half_t;
#elif 1
    using in_data_t = int4x2_t;
    // using in_data_t         = int8_t;
    using bias_data_t       = int32_t;
    using scale_data_t      = float;
    using acc_data_t        = int32_t;
    using out_data_t        = int8_t;
    using out_packed_data_t = int4x2_t;
#endif

    std::vector<std::size_t> in_lengths_host(5), wei_lengths_host(5), out_lengths_host(5),
        bias_lengths_host(2);

    in_lengths_host[0] = static_cast<std::size_t>(N);
    in_lengths_host[1] = static_cast<std::size_t>(C0);
    in_lengths_host[2] = static_cast<std::size_t>(Hi);
    in_lengths_host[3] = static_cast<std::size_t>(Wi);
    in_lengths_host[4] = static_cast<std::size_t>(C1);

    wei_lengths_host[0] = static_cast<std::size_t>(K0 * K1);
    wei_lengths_host[1] = static_cast<std::size_t>(C0);
    wei_lengths_host[2] = static_cast<std::size_t>(Y);
    wei_lengths_host[3] = static_cast<std::size_t>(X);
    wei_lengths_host[4] = static_cast<std::size_t>(C1);

    out_lengths_host[0] = static_cast<std::size_t>(N);
    out_lengths_host[1] = static_cast<std::size_t>(K0);
    out_lengths_host[2] = static_cast<std::size_t>(Ho);
    out_lengths_host[3] = static_cast<std::size_t>(Wo);
    out_lengths_host[4] = static_cast<std::size_t>(K1);

    bias_lengths_host[0] = static_cast<std::size_t>(K0);
    bias_lengths_host[1] = static_cast<std::size_t>(K1);

    std::vector<std::size_t> out_packed_lengths_host(5);

    out_packed_lengths_host[0] = static_cast<std::size_t>(N);
    out_packed_lengths_host[1] = static_cast<std::size_t>(K0);
    out_packed_lengths_host[2] = static_cast<std::size_t>(Ho);
    out_packed_lengths_host[3] = static_cast<std::size_t>(Wo);
    out_packed_lengths_host[4] = static_cast<std::size_t>(K1 / 2);

    Tensor<in_data_t> in(in_lengths_host);
    Tensor<in_data_t> wei(wei_lengths_host);
    Tensor<bias_data_t> bias(bias_lengths_host);
    Tensor<scale_data_t> scale(bias_lengths_host);
    Tensor<out_data_t> out_host(out_lengths_host);
    Tensor<out_packed_data_t> out_packed_host(out_packed_lengths_host);
    Tensor<out_data_t> out_device(out_lengths_host);
    Tensor<out_packed_data_t> out_packed_device(out_packed_lengths_host);

    ostream_HostTensorDescriptor(in.mDesc, std::cout << "in: ");
    ostream_HostTensorDescriptor(wei.mDesc, std::cout << "wei: ");
    ostream_HostTensorDescriptor(bias.mDesc, std::cout << "bias: ");
    ostream_HostTensorDescriptor(scale.mDesc, std::cout << "scale: ");
    ostream_HostTensorDescriptor(out_host.mDesc, std::cout << "out: ");

    print_array("InLeftPads", make_tuple(in_left_pad_h, in_left_pad_w));
    print_array("InRightPads", make_tuple(in_right_pad_h, in_right_pad_w));
    print_array("ConvStrides", make_tuple(conv_stride_h, conv_stride_w));
    print_array("ConvDilations", make_tuple(conv_dilation_h, conv_dilation_w));

    std::size_t num_thread = std::thread::hardware_concurrency();

    switch(init_method)
    {
    case 0:
        // no initialization
        break;
    case 1:
        in.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        break;
    case 2:
        in.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        break;
    case 3:
        in.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        wei.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        break;
    case 4:
        in.GenerateTensorValue(GeneratorTensor_2<in_data_t>{0, 5}, num_thread);
        wei.GenerateTensorValue(GeneratorTensor_2<in_data_t>{0, 5}, num_thread);
        break;
    // case 5:
    // in.GenerateTensorValue(GeneratorTensor_3<in_data_t>{0.0, 1.0}, num_thread);
    // wei.GenerateTensorValue(GeneratorTensor_3<in_data_t>{-0.5, 0.5}, num_thread);
    // break;
    default:
        in.GenerateTensorValue(GeneratorTensor_2<in_data_t>{1, 5}, num_thread);

        auto gen_wei = [](auto... is) {
            return GeneratorTensor_2<in_data_t>{1, 5}(is...) * GeneratorTensor_Checkboard{}(is...);
        };
        wei.GenerateTensorValue(gen_wei, num_thread);
    }

    bias.GenerateTensorValue(GeneratorTensor_2<bias_data_t>{-5, 5}, num_thread);
    scale.GenerateTensorValue(GeneratorTensor_3<scale_data_t>{0.1, 0.8}, num_thread);

    const auto in_lengths_dev     = make_tuple(N, C0, Hi, Wi, C1);
    const auto wei_lengths_dev    = make_tuple(K0 * K1, C0, Y, X, C1);
    const auto out_lengths_dev    = make_tuple(N, K0, Ho, Wo, K1);
    const auto conv_strides_dev   = make_tuple(conv_stride_h, conv_stride_w);
    const auto conv_dilations_dev = make_tuple(conv_dilation_h, conv_dilation_w);
    const auto in_left_pads_dev   = make_tuple(in_left_pad_h, in_left_pad_w);
    const auto in_right_pads_dev  = make_tuple(in_right_pad_h, in_right_pad_w);

#if USE_CONV_FWD_V5R1_NCHWC
    if(algo == ConvForwardAlgo::V5R1NCHWC)
    {
        device_convolution_bias_activ_forward_implicit_gemm_v5r1_dlops_nc0hwc1_kc0yxc1_nk0hwk1<
            in_data_t,
            acc_data_t,
            bias_data_t,
            scale_data_t,
            out_data_t,
            out_packed_data_t,
            activ_type>(in_lengths_dev,
                        wei_lengths_dev,
                        out_lengths_dev,
                        conv_strides_dev,
                        conv_dilations_dev,
                        in_left_pads_dev,
                        in_right_pads_dev,
                        in,
                        wei,
                        bias,
                        scale,
                        out_device,
                        out_packed_device,
                        nrepeat);
    }
#endif

    if(do_verification)
    {
        host_direct_convolution_nchwc(in,
                                      wei,
                                      bias,
                                      scale,
                                      out_host,
                                      out_packed_host,
                                      make_tuple(conv_stride_h, conv_stride_w),
                                      make_tuple(conv_dilation_h, conv_dilation_w),
                                      make_tuple(in_left_pad_h, in_left_pad_w),
                                      make_tuple(in_right_pad_h, in_right_pad_w),
                                      ck::tensor_operation::element_wise::HardTanhQuant{0.3});

        // check_error(out_host, out_device);
        check_error(out_packed_host, out_packed_device);

        if(do_log)
        {
            // LogRangeAsType<float>(std::cout << "in : ", in.mData, ",") << std::endl;
            // LogRangeAsType<float>(std::cout << "wei: ", wei.mData, ",") << std::endl;
            // LogRangeAsType<float>(std::cout << "bias: ", bias.mData, ",") << std::endl;
            LogRangeAsType<float>(std::cout << "out_host  : ", out_packed_host.mData, ",")
                << std::endl;
            LogRangeAsType<float>(std::cout << "out_device: ", out_packed_device.mData, ",")
                << std::endl;
        }
    }
}