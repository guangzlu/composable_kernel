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
#include "device_conv3x3_conv1x1_bias_activ_forward_implicit_gemm_v5r1_dlops_nc0hwc1_kc0yxc1_nk0hwk1.hpp"
#include "ck_conv_fig.h"

#define USE_DYNAMIC_MODE 0
#define USE_CONV_FWD_V5R1_NCHWC 1

enum ConvForwardAlgo
{
    V5R1NCHWC // 0
};

template <typename TIn,
          typename TWei,
          typename TOut,
          typename ConvStrides,
          typename ConvDilations,
          typename InLeftPads,
          typename InRightPads>
void host_direct_convolution_nchwc(const Tensor<TIn>& in,
                                   const Tensor<TWei>& wei,
                                   const Tensor<TOut>& bias,
                                   Tensor<TOut>& out,
                                   const ConvStrides& conv_strides,
                                   const ConvDilations& conv_dilations,
                                   const InLeftPads& in_left_pads,
                                   const InRightPads&)
{
    using namespace ck;

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};

    auto f_nchw = [&](auto n, auto k0, auto ho, auto wo, auto k1) {
        double v    = 0;
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
                            v += static_cast<const double>(in(n, c0, hi, wi, c1)) *
                                 static_cast<const double>(wei(k, c0, y, x, c1));
                        }
                    }
                }
            }
        }
        // v += bias(k0, k1);
        out(n, k0, ho, wo, k1) = v;
    };

    make_ParallelTensorFunctor(f_nchw,
                               out.mDesc.GetLengths()[0],
                               out.mDesc.GetLengths()[1],
                               out.mDesc.GetLengths()[2],
                               out.mDesc.GetLengths()[3],
                               out.mDesc.GetLengths()[4])(std::thread::hardware_concurrency());
}

int main(int argc, char* argv[])
{
    using namespace ck;

    constexpr auto I0 = Number<0>{};
    constexpr auto I1 = Number<1>{};
    constexpr auto I2 = Number<2>{};
    constexpr auto I3 = Number<3>{};
    constexpr auto I4 = Number<4>{};
    constexpr auto I5 = Number<5>{};
    constexpr auto I6 = Number<6>{};
    constexpr auto I7 = Number<6>{};

#if USE_DYNAMIC_MODE
    // dynamic mode
    if(argc != 23)
    {
        printf("arg1 to 5: algo, do_verification, init_method, do_log, nrepeat\n");
        printf("rest: N, K0, K1, C0, C1, Y, X, Hi, Wi, Sy, Sx, Dy, Dx, LeftPy, LeftPx, RightPy, "
               "RightPx\n");
        exit(1);
    }

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
#elif 1
    using in_data_t   = half_t;
    using acc_data_t  = float;
    using out_data_t  = half_t;
#elif 1
    using in_data_t  = int8_t;
    using acc_data_t = int32_t;
    using out_data_t = int8_t;
#endif

    std::vector<std::size_t> in1_lengths_host(5), wei1_lengths_host(5), out1_lengths_host(5),
        bias1_lengths_host(2);

    in1_lengths_host[0] = static_cast<std::size_t>(N);
    in1_lengths_host[1] = static_cast<std::size_t>(C0);
    in1_lengths_host[2] = static_cast<std::size_t>(Hi);
    in1_lengths_host[3] = static_cast<std::size_t>(Wi);
    in1_lengths_host[4] = static_cast<std::size_t>(C1);

    wei1_lengths_host[0] = static_cast<std::size_t>(K0 * K1);
    wei1_lengths_host[1] = static_cast<std::size_t>(C0);
    wei1_lengths_host[2] = static_cast<std::size_t>(Y);
    wei1_lengths_host[3] = static_cast<std::size_t>(X);
    wei1_lengths_host[4] = static_cast<std::size_t>(C1);

    out1_lengths_host[0] = static_cast<std::size_t>(N);
    out1_lengths_host[1] = static_cast<std::size_t>(K0);
    out1_lengths_host[2] = static_cast<std::size_t>(Ho);
    out1_lengths_host[3] = static_cast<std::size_t>(Wo);
    out1_lengths_host[4] = static_cast<std::size_t>(K1);

    bias1_lengths_host[0] = static_cast<std::size_t>(K0);
    bias1_lengths_host[1] = static_cast<std::size_t>(K1);

    std::vector<std::size_t> in2_lengths_host(5), wei2_lengths_host(5), out2_lengths_host(5),
        bias2_lengths_host(2);

    in2_lengths_host[0] = static_cast<std::size_t>(N);
    in2_lengths_host[1] = static_cast<std::size_t>(C0);
    in2_lengths_host[2] = static_cast<std::size_t>(Ho);
    in2_lengths_host[3] = static_cast<std::size_t>(Wo);
    in2_lengths_host[4] = static_cast<std::size_t>(C1);

    wei2_lengths_host[0] = static_cast<std::size_t>(K0 * K1);
    wei2_lengths_host[1] = static_cast<std::size_t>(K0);
    wei2_lengths_host[2] = static_cast<std::size_t>(I1);
    wei2_lengths_host[3] = static_cast<std::size_t>(I1);
    wei2_lengths_host[4] = static_cast<std::size_t>(K1);

    out2_lengths_host[0] = static_cast<std::size_t>(N);
    out2_lengths_host[1] = static_cast<std::size_t>(K0);
    out2_lengths_host[2] = static_cast<std::size_t>(Ho);
    out2_lengths_host[3] = static_cast<std::size_t>(Wo);
    out2_lengths_host[4] = static_cast<std::size_t>(K1);

    bias2_lengths_host[0] = static_cast<std::size_t>(K0);
    bias2_lengths_host[1] = static_cast<std::size_t>(K1);

    Tensor<in_data_t> in1(in1_lengths_host);
    Tensor<in_data_t> wei1(wei1_lengths_host);
    Tensor<out_data_t> bias1(bias1_lengths_host);
    Tensor<out_data_t> out1_host(out1_lengths_host);
    Tensor<out_data_t> out1_device(out1_lengths_host);

    Tensor<in_data_t> wei2(wei2_lengths_host);
    Tensor<out_data_t> bias2(bias2_lengths_host);
    Tensor<out_data_t> out2_host(out2_lengths_host);
    Tensor<out_data_t> out2_device(out2_lengths_host);

    ostream_HostTensorDescriptor(in1.mDesc, std::cout << "in1: ");
    ostream_HostTensorDescriptor(wei1.mDesc, std::cout << "wei1: ");
    ostream_HostTensorDescriptor(bias1.mDesc, std::cout << "bias1: ");
    ostream_HostTensorDescriptor(out1_host.mDesc, std::cout << "out1: ");

    ostream_HostTensorDescriptor(wei2.mDesc, std::cout << "wei2: ");
    ostream_HostTensorDescriptor(bias2.mDesc, std::cout << "bias2: ");
    ostream_HostTensorDescriptor(out2_host.mDesc, std::cout << "out2: ");

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
        in1.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei1.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei2.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        break;
    case 2:
        in1.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei1.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        wei2.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        break;
    case 3:
        in1.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        wei1.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        wei2.GenerateTensorValue(GeneratorTensor_1<in_data_t>{}, num_thread);
        break;
    case 4:
        in1.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        wei1.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        wei2.GenerateTensorValue(GeneratorTensor_2<in_data_t>{-5, 5}, num_thread);
        break;
    case 5:
        in1.GenerateTensorValue(GeneratorTensor_3<in_data_t>{0.0, 1.0}, num_thread);
        wei1.GenerateTensorValue(GeneratorTensor_3<in_data_t>{-0.5, 0.5}, num_thread);
        wei2.GenerateTensorValue(GeneratorTensor_3<in_data_t>{-0.5, 0.5}, num_thread);
        break;
    default:
        in1.GenerateTensorValue(GeneratorTensor_2<in_data_t>{1, 5}, num_thread);

        auto gen_wei = [](auto... is) {
            return GeneratorTensor_2<in_data_t>{1, 5}(is...) * GeneratorTensor_Checkboard{}(is...);
        };
        wei1.GenerateTensorValue(gen_wei, num_thread);
        wei2.GenerateTensorValue(gen_wei, num_thread);
    }

    bias1.GenerateTensorValue(GeneratorTensor_3<in_data_t>{-0.5, 0.5}, num_thread);
    bias2.GenerateTensorValue(GeneratorTensor_3<in_data_t>{-0.5, 0.5}, num_thread);

    const auto in1_lengths_dev  = make_tuple(N, C0, Hi, Wi, C1);
    const auto wei1_lengths_dev = make_tuple(K0 * K1, C0, Y, X, C1);
    const auto out1_lengths_dev = make_tuple(N, K0, Ho, Wo, K1);

    const auto in2_lengths_dev  = make_tuple(N, K0, Ho, Wo, K1);
    const auto wei2_lengths_dev = make_tuple(K0 * K1, K0, I1, I1, K1);
    const auto out2_lengths_dev = make_tuple(N, K0, Ho, Wo, K1);

    const auto conv_strides_dev   = make_tuple(conv_stride_h, conv_stride_w);
    const auto conv_dilations_dev = make_tuple(conv_dilation_h, conv_dilation_w);
    const auto in_left_pads_dev   = make_tuple(in_left_pad_h, in_left_pad_w);
    const auto in_right_pads_dev  = make_tuple(in_right_pad_h, in_right_pad_w);

#if USE_CONV_FWD_V5R1_NCHWC
    if(algo == ConvForwardAlgo::V5R1NCHWC)
    {
        device_conv3x3_conv1x1_bias_activ_forward_implicit_gemm_v5r1_dlops_nc0hwc1_kc0yxc1_nk0hwk1<
            in_data_t,
            acc_data_t,
            out_data_t>(in1_lengths_dev,
                        wei1_lengths_dev,
                        out1_lengths_dev,
                        in2_lengths_dev,
                        wei2_lengths_dev,
                        out2_lengths_dev,
                        conv_strides_dev,
                        conv_dilations_dev,
                        in_left_pads_dev,
                        in_right_pads_dev,
                        in1,
                        wei1,
                        bias1,
                        out1_device,
                        wei2,
                        bias2,
                        out2_device,
                        nrepeat);
    }
#endif

    if(do_verification)
    {
        host_direct_convolution_nchwc(in1,
                                      wei1,
                                      bias1,
                                      out1_host,
                                      make_tuple(conv_stride_h, conv_stride_w),
                                      make_tuple(conv_dilation_h, conv_dilation_w),
                                      make_tuple(in_left_pad_h, in_left_pad_w),
                                      make_tuple(in_right_pad_h, in_right_pad_w));

        host_direct_convolution_nchwc(out1_host,
                                      wei2,
                                      bias2,
                                      out2_host,
                                      make_tuple(I1, I1),
                                      make_tuple(I1, I1),
                                      make_tuple(I0, I0),
                                      make_tuple(I0, I0));

        check_error(out1_host, out1_device);
        check_error(out2_host, out2_device);

        if(do_log)
        {
            // LogRangeAsType<float>(std::cout << "in1 : ", in1.mData, ",") << std::endl;
            // LogRangeAsType<float>(std::cout << "wei1: ", wei1.mData, ",") << std::endl;
            // LogRangeAsType<float>(std::cout << "bias1: ", bias1.mData, ",") << std::endl;
            LogRangeAsType<float>(std::cout << "out1_host  : ", out1_host.mData, ",") << std::endl;
            LogRangeAsType<float>(std::cout << "out1_device: ", out1_device.mData, ",")
                << std::endl;

            // LogRangeAsType<float>(std::cout << "wei2: ", wei2.mData, ",") << std::endl;
            // LogRangeAsType<float>(std::cout << "bias2: ", bias2.mData, ",") << std::endl;
            LogRangeAsType<float>(std::cout << "out2_host  : ", out2_host.mData, ",") << std::endl;
            LogRangeAsType<float>(std::cout << "out2_device: ", out2_device.mData, ",")
                << std::endl;
        }
    }
}
