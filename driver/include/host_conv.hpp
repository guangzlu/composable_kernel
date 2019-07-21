#pragma once
#include "tensor.hpp"
#include "common_header.hpp"
#include "constant_tensor_descriptor.hpp"

// this is ugly, only for 4d
template <class TConstTensorDesc>
void ostream_ConstantTensorDescriptor(TConstTensorDesc, std::ostream& os = std::cout)
{
    using namespace ck;

    static_assert(TConstTensorDesc::nDim == 4, "nDim is not 4");

    constexpr auto I0   = Number<0>{};
    constexpr auto I1   = Number<1>{};
    constexpr auto I2   = Number<2>{};
    constexpr auto I3   = Number<3>{};
    constexpr auto desc = TConstTensorDesc{};

    os << "Lengths: {" << desc.GetLength(I0) << ", " << desc.GetLength(I1) << ", "
       << desc.GetLength(I2) << ", " << desc.GetLength(I3) << "}, "
       << "Strides: {" << desc.GetStride(I0) << ", " << desc.GetStride(I1) << ", "
       << desc.GetStride(I2) << ", " << desc.GetStride(I3) << "}" << std::endl;
}

// this is ugly, only for 4d
template <class TConstTensorDesc>
auto make_TensorDescriptor(TConstTensorDesc)
{
    using namespace ck;

    static_assert(TConstTensorDesc::nDim == 4, "nDim is not 4");

    constexpr auto I0   = Number<0>{};
    constexpr auto I1   = Number<1>{};
    constexpr auto I2   = Number<2>{};
    constexpr auto I3   = Number<3>{};
    constexpr auto desc = TConstTensorDesc{};

    std::initializer_list<index_t> lengths = {
        desc.GetLength(I0), desc.GetLength(I1), desc.GetLength(I2), desc.GetLength(I3)};
    std::initializer_list<index_t> strides = {
        desc.GetStride(I0), desc.GetStride(I1), desc.GetStride(I2), desc.GetStride(I3)};

    return HostTensorDescriptor(lengths, strides);
}

template <class TIn,
          class TWei,
          class TOut,
          class ConvStrides,
          class ConvDilations,
          class LowerPads,
          class UpperPads>
void host_direct_convolution(const HostTensor<TIn>& in_nchw,
                             const HostTensor<TWei>& wei_kcyx,
                             HostTensor<TOut>& out_nkhw,
                             ConvStrides,
                             ConvDilations,
                             LowerPads,
                             UpperPads)
{
    using namespace ck;

    index_t h_pad_low = LowerPads{}.Get(Number<0>{});
    index_t w_pad_low = LowerPads{}.Get(Number<1>{});

    index_t h_pad_up = UpperPads{}.Get(Number<0>{});
    index_t w_pad_up = UpperPads{}.Get(Number<1>{});

    auto f = [&](auto n, auto k, auto ho, auto wo) {
        double v = 0;
        for(int c = 0; c < wei_kcyx.mDesc.GetLengths()[1]; ++c)
        {
            for(int y = 0; y < wei_kcyx.mDesc.GetLengths()[2]; ++y)
            {
                int hi = ho * ConvStrides{}[0] + y * ConvDilations{}[0] - h_pad_low;
                for(int x = 0; x < wei_kcyx.mDesc.GetLengths()[3]; ++x)
                {
                    int wi = wo * ConvStrides{}[1] + x * ConvDilations{}[1] - w_pad_low;
                    if(hi >= 0 && hi < in_nchw.mDesc.GetLengths()[2] && wi >= 0 &&
                       wi < in_nchw.mDesc.GetLengths()[3])
                    {
                        v += double(in_nchw(n, c, hi, wi)) * double(wei_kcyx(k, c, y, x));
                    }
                }
            }
        }
        out_nkhw(n, k, ho, wo) = v;
    };

    auto f_par = make_ParallelTensorFunctor(f,
                                            out_nkhw.mDesc.GetLengths()[0],
                                            out_nkhw.mDesc.GetLengths()[1],
                                            out_nkhw.mDesc.GetLengths()[2],
                                            out_nkhw.mDesc.GetLengths()[3]);

    f_par(std::thread::hardware_concurrency());
}

template <class TIn, class TWei, class TOut, class LowerPads, class UpperPads>
void host_winograd_3x3_convolution(const HostTensor<TIn>& in_nchw,
                                   const HostTensor<TWei>& wei_kcyx,
                                   HostTensor<TOut>& out_nkhw,
                                   LowerPads,
                                   UpperPads)
{
    using namespace ck;

    constexpr std::size_t HoPerTile = 2;
    constexpr std::size_t WoPerTile = 2;

    std::size_t N  = in_nchw.mDesc.GetLengths()[0];
    std::size_t C  = in_nchw.mDesc.GetLengths()[1];
    std::size_t HI = in_nchw.mDesc.GetLengths()[2];
    std::size_t WI = in_nchw.mDesc.GetLengths()[3];

    std::size_t K = wei_kcyx.mDesc.GetLengths()[0];
    std::size_t Y = wei_kcyx.mDesc.GetLengths()[2];
    std::size_t X = wei_kcyx.mDesc.GetLengths()[3];

    std::size_t HO = out_nkhw.mDesc.GetLengths()[2];
    std::size_t WO = out_nkhw.mDesc.GetLengths()[3];

    index_t h_pad_low = LowerPads{}.Get(Number<0>{});
    index_t w_pad_low = LowerPads{}.Get(Number<1>{});

    index_t h_pad_up = UpperPads{}.Get(Number<0>{});
    index_t w_pad_up = UpperPads{}.Get(Number<1>{});

    std::size_t HiPerTile = HoPerTile + Y - 1;
    std::size_t WiPerTile = WoPerTile + X - 1;

    std::size_t HTile = (HO + HoPerTile - 1) / HoPerTile;
    std::size_t WTile = (WO + WoPerTile - 1) / WoPerTile;

    HostTensor<double> in_hold({N, C, HTile, WTile, HiPerTile, WiPerTile});
    HostTensor<double> in_transform({N, C, HTile, WTile, HiPerTile, WiPerTile});
    HostTensor<double> wei_transform({K, C, HiPerTile, WiPerTile});
    HostTensor<double> out_transform({N, K, HTile, WTile, HiPerTile, HiPerTile});
    HostTensor<double> out_hold({N, K, HTile, WTile, HoPerTile, WoPerTile});

    auto f_in_hold = [&](auto n, auto c, auto htile, auto wtile) {
        for(int j = 0; j < HiPerTile; ++j)
        {
            int hi = HoPerTile * htile + j - h_pad_low;
            for(int i = 0; i < WiPerTile; ++i)
            {
                int wi = WoPerTile * wtile + i - w_pad_low;

                if(hi >= 0 && hi < in_nchw.mDesc.GetLengths()[2] && wi >= 0 &&
                   wi < in_nchw.mDesc.GetLengths()[3])
                {
                    in_hold(n, c, htile, wtile, j, i) = in_nchw(n, c, hi, wi);
                }
                else
                {
                    in_hold(n, c, htile, wtile, j, i) = TIn(0);
                }
            }
        }
    };

    auto f_in_transform = [&](auto n, auto c, auto htile, auto wtile) {
        in_transform(n, c, htile, wtile, 0, 0) =
            in_hold(n, c, htile, wtile, 0, 0) - in_hold(n, c, htile, wtile, 0, 2) -
            in_hold(n, c, htile, wtile, 2, 0) + in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 0, 1) =
            in_hold(n, c, htile, wtile, 0, 1) + in_hold(n, c, htile, wtile, 0, 2) -
            in_hold(n, c, htile, wtile, 2, 1) - in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 0, 2) =
            -in_hold(n, c, htile, wtile, 0, 1) + in_hold(n, c, htile, wtile, 0, 2) +
            in_hold(n, c, htile, wtile, 2, 1) - in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 0, 3) =
            in_hold(n, c, htile, wtile, 0, 1) - in_hold(n, c, htile, wtile, 0, 3) -
            in_hold(n, c, htile, wtile, 2, 1) + in_hold(n, c, htile, wtile, 2, 3);

        in_transform(n, c, htile, wtile, 1, 0) =
            in_hold(n, c, htile, wtile, 1, 0) - in_hold(n, c, htile, wtile, 1, 2) +
            in_hold(n, c, htile, wtile, 2, 0) - in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 1, 1) =
            in_hold(n, c, htile, wtile, 1, 1) + in_hold(n, c, htile, wtile, 1, 2) +
            in_hold(n, c, htile, wtile, 2, 1) + in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 1, 2) =
            -in_hold(n, c, htile, wtile, 1, 1) + in_hold(n, c, htile, wtile, 1, 2) -
            in_hold(n, c, htile, wtile, 2, 1) + in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 1, 3) =
            in_hold(n, c, htile, wtile, 1, 1) - in_hold(n, c, htile, wtile, 1, 3) +
            in_hold(n, c, htile, wtile, 2, 1) - in_hold(n, c, htile, wtile, 2, 3);

        in_transform(n, c, htile, wtile, 2, 0) =
            -in_hold(n, c, htile, wtile, 1, 0) + in_hold(n, c, htile, wtile, 1, 2) +
            in_hold(n, c, htile, wtile, 2, 0) - in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 2, 1) =
            -in_hold(n, c, htile, wtile, 1, 1) - in_hold(n, c, htile, wtile, 1, 2) +
            in_hold(n, c, htile, wtile, 2, 1) + in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 2, 2) =
            in_hold(n, c, htile, wtile, 1, 1) - in_hold(n, c, htile, wtile, 1, 2) -
            in_hold(n, c, htile, wtile, 2, 1) + in_hold(n, c, htile, wtile, 2, 2);
        in_transform(n, c, htile, wtile, 2, 3) =
            -in_hold(n, c, htile, wtile, 1, 1) + in_hold(n, c, htile, wtile, 1, 3) +
            in_hold(n, c, htile, wtile, 2, 1) - in_hold(n, c, htile, wtile, 2, 3);

        in_transform(n, c, htile, wtile, 3, 0) =
            in_hold(n, c, htile, wtile, 1, 0) - in_hold(n, c, htile, wtile, 1, 2) -
            in_hold(n, c, htile, wtile, 3, 0) + in_hold(n, c, htile, wtile, 3, 2);
        in_transform(n, c, htile, wtile, 3, 1) =
            in_hold(n, c, htile, wtile, 1, 1) + in_hold(n, c, htile, wtile, 1, 2) -
            in_hold(n, c, htile, wtile, 3, 1) - in_hold(n, c, htile, wtile, 3, 2);
        in_transform(n, c, htile, wtile, 3, 2) =
            -in_hold(n, c, htile, wtile, 1, 1) + in_hold(n, c, htile, wtile, 1, 2) +
            in_hold(n, c, htile, wtile, 3, 1) - in_hold(n, c, htile, wtile, 3, 2);
        in_transform(n, c, htile, wtile, 3, 3) =
            in_hold(n, c, htile, wtile, 1, 1) - in_hold(n, c, htile, wtile, 1, 3) -
            in_hold(n, c, htile, wtile, 3, 1) + in_hold(n, c, htile, wtile, 3, 3);
    };

    auto f_wei_transform = [&](auto k, auto c) {
        wei_transform(k, c, 0, 0) = double(wei_kcyx(k, c, 0, 0));
        wei_transform(k, c, 0, 1) = 0.5 * double(wei_kcyx(k, c, 0, 0)) +
                                    0.5 * double(wei_kcyx(k, c, 0, 1)) +
                                    0.5 * double(wei_kcyx(k, c, 0, 2));
        wei_transform(k, c, 0, 2) = 0.5 * double(wei_kcyx(k, c, 0, 0)) -
                                    0.5 * double(wei_kcyx(k, c, 0, 1)) +
                                    0.5 * double(wei_kcyx(k, c, 0, 2));
        wei_transform(k, c, 0, 3) = double(wei_kcyx(k, c, 0, 2));

        wei_transform(k, c, 1, 0) = 0.5 * double(wei_kcyx(k, c, 0, 0)) +
                                    0.5 * double(wei_kcyx(k, c, 1, 0)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 0));
        wei_transform(k, c, 1, 1) =
            0.25 * double(wei_kcyx(k, c, 0, 0)) + 0.25 * double(wei_kcyx(k, c, 0, 1)) +
            0.25 * double(wei_kcyx(k, c, 0, 2)) + 0.25 * double(wei_kcyx(k, c, 1, 0)) +
            0.25 * double(wei_kcyx(k, c, 1, 1)) + 0.25 * double(wei_kcyx(k, c, 1, 2)) +
            0.25 * double(wei_kcyx(k, c, 2, 0)) + 0.25 * double(wei_kcyx(k, c, 2, 1)) +
            0.25 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 1, 2) =
            0.25 * double(wei_kcyx(k, c, 0, 0)) - 0.25 * double(wei_kcyx(k, c, 0, 1)) +
            0.25 * double(wei_kcyx(k, c, 0, 2)) + 0.25 * double(wei_kcyx(k, c, 1, 0)) -
            0.25 * double(wei_kcyx(k, c, 1, 1)) + 0.25 * double(wei_kcyx(k, c, 1, 2)) +
            0.25 * double(wei_kcyx(k, c, 2, 0)) - 0.25 * double(wei_kcyx(k, c, 2, 1)) +
            0.25 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 1, 3) = 0.5 * double(wei_kcyx(k, c, 0, 2)) +
                                    0.5 * double(wei_kcyx(k, c, 1, 2)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 2));

        wei_transform(k, c, 2, 0) = 0.5 * double(wei_kcyx(k, c, 0, 0)) -
                                    0.5 * double(wei_kcyx(k, c, 1, 0)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 0));
        wei_transform(k, c, 2, 1) =
            0.25 * double(wei_kcyx(k, c, 0, 0)) + 0.25 * double(wei_kcyx(k, c, 0, 1)) +
            0.25 * double(wei_kcyx(k, c, 0, 2)) - 0.25 * double(wei_kcyx(k, c, 1, 0)) -
            0.25 * double(wei_kcyx(k, c, 1, 1)) - 0.25 * double(wei_kcyx(k, c, 1, 2)) +
            0.25 * double(wei_kcyx(k, c, 2, 0)) + 0.25 * double(wei_kcyx(k, c, 2, 1)) +
            0.25 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 2, 2) =
            0.25 * double(wei_kcyx(k, c, 0, 0)) - 0.25 * double(wei_kcyx(k, c, 0, 1)) +
            0.25 * double(wei_kcyx(k, c, 0, 2)) - 0.25 * double(wei_kcyx(k, c, 1, 0)) +
            0.25 * double(wei_kcyx(k, c, 1, 1)) - 0.25 * double(wei_kcyx(k, c, 1, 2)) +
            0.25 * double(wei_kcyx(k, c, 2, 0)) - 0.25 * double(wei_kcyx(k, c, 2, 1)) +
            0.25 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 2, 3) = 0.5 * double(wei_kcyx(k, c, 0, 2)) -
                                    0.5 * double(wei_kcyx(k, c, 1, 2)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 2));

        wei_transform(k, c, 3, 0) = double(wei_kcyx(k, c, 2, 0));
        wei_transform(k, c, 3, 1) = 0.5 * double(wei_kcyx(k, c, 2, 0)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 1)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 3, 2) = 0.5 * double(wei_kcyx(k, c, 2, 0)) -
                                    0.5 * double(wei_kcyx(k, c, 2, 1)) +
                                    0.5 * double(wei_kcyx(k, c, 2, 2));
        wei_transform(k, c, 3, 3) = double(wei_kcyx(k, c, 2, 2));
    };

    auto f_out_transform = [&](auto n, auto k, auto htile, auto wtile) {
        for(int j = 0; j < HiPerTile; ++j)
        {
            for(int i = 0; i < WiPerTile; ++i)
            {
                double v = 0;
                for(int c = 0; c < C; ++c)
                {
                    v += in_transform(n, c, htile, wtile, j, i) * wei_transform(k, c, j, i);
                }

                out_transform(n, k, htile, wtile, j, i) = v;
            }
        }
    };

    auto f_out_hold = [&](auto n, auto k, auto htile, auto wtile) {
        out_hold(n, k, htile, wtile, 0, 0) =
            out_transform(n, k, htile, wtile, 0, 0) + out_transform(n, k, htile, wtile, 0, 1) +
            out_transform(n, k, htile, wtile, 0, 2) + out_transform(n, k, htile, wtile, 1, 0) +
            out_transform(n, k, htile, wtile, 1, 1) + out_transform(n, k, htile, wtile, 1, 2) +
            out_transform(n, k, htile, wtile, 2, 0) + out_transform(n, k, htile, wtile, 2, 1) +
            out_transform(n, k, htile, wtile, 2, 2);
        out_hold(n, k, htile, wtile, 0, 1) =
            out_transform(n, k, htile, wtile, 0, 1) - out_transform(n, k, htile, wtile, 0, 2) -
            out_transform(n, k, htile, wtile, 0, 3) + out_transform(n, k, htile, wtile, 1, 1) -
            out_transform(n, k, htile, wtile, 1, 2) - out_transform(n, k, htile, wtile, 1, 3) +
            out_transform(n, k, htile, wtile, 2, 1) - out_transform(n, k, htile, wtile, 2, 2) -
            out_transform(n, k, htile, wtile, 2, 3);
        out_hold(n, k, htile, wtile, 1, 0) =
            out_transform(n, k, htile, wtile, 1, 0) + out_transform(n, k, htile, wtile, 1, 1) +
            out_transform(n, k, htile, wtile, 1, 2) - out_transform(n, k, htile, wtile, 2, 0) -
            out_transform(n, k, htile, wtile, 2, 1) - out_transform(n, k, htile, wtile, 2, 2) -
            out_transform(n, k, htile, wtile, 3, 0) - out_transform(n, k, htile, wtile, 3, 1) -
            out_transform(n, k, htile, wtile, 3, 2);
        out_hold(n, k, htile, wtile, 1, 1) =
            out_transform(n, k, htile, wtile, 1, 1) - out_transform(n, k, htile, wtile, 1, 2) -
            out_transform(n, k, htile, wtile, 1, 3) - out_transform(n, k, htile, wtile, 2, 1) +
            out_transform(n, k, htile, wtile, 2, 2) + out_transform(n, k, htile, wtile, 2, 3) -
            out_transform(n, k, htile, wtile, 3, 1) + out_transform(n, k, htile, wtile, 3, 2) +
            out_transform(n, k, htile, wtile, 3, 3);
    };

    auto f_out = [&](auto n, auto k, auto htile, auto wtile) {
        for(int j = 0; j < HoPerTile; ++j)
        {
            std::size_t ho = HoPerTile * htile + j;
            for(int i = 0; i < WoPerTile; ++i)
            {
                std::size_t wo = WoPerTile * wtile + i;
                out_nkhw(n, k, ho, wo) = out_hold(n, k, htile, wtile, j, i);
            }
        }
    };

    std::size_t num_thread = std::thread::hardware_concurrency();

    make_ParallelTensorFunctor(f_in_hold, N, C, HTile, WTile)(num_thread);
    make_ParallelTensorFunctor(f_in_transform, N, C, HTile, WTile)(num_thread);
    make_ParallelTensorFunctor(f_wei_transform, K, C)(num_thread);
    make_ParallelTensorFunctor(f_out_transform, N, K, HTile, WTile)(num_thread);
    make_ParallelTensorFunctor(f_out_hold, N, K, HTile, WTile)(num_thread);
    make_ParallelTensorFunctor(f_out, N, K, HTile, WTile)(num_thread);
}

template <class T>
void check_error(const HostTensor<T>& ref, const HostTensor<T>& result)
{
    float error     = 0;
    float max_diff  = -1;
    float ref_value = 0, result_value = 0;
    for(int i = 0; i < ref.mData.size(); ++i)
    {
        error += std::abs(double(ref.mData[i]) - double(result.mData[i]));
        float diff = std::abs(double(ref.mData[i]) - double(result.mData[i]));
        if(max_diff < diff)
        {
            max_diff     = diff;
            ref_value    = ref.mData[i];
            result_value = result.mData[i];
        }
    }

    std::cout << "error: " << error << std::endl;
    std::cout << "max_diff: " << max_diff << ", " << ref_value << ", " << result_value << std::endl;
}
