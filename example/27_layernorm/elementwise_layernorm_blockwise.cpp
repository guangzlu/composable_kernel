// SPDX-License-Identifier: MIT
// Copyright (c) 2018-2022, Advanced Micro Devices, Inc. All rights reserved.

#include <iostream>
#include <numeric>
#include <initializer_list>
#include <cstdlib>
#include <getopt.h>

#include "ck/ck.hpp"
#include "ck/utility/reduction_enums.hpp"
#include "ck/tensor_operation/gpu/device/device_elementwise.hpp"
#include "ck/tensor_operation/gpu/device/device_elementwise_layernorm_impl.hpp"
#include "ck/tensor_operation/gpu/device/reduction_operator_mapping.hpp"

#include "ck/library/utility/check_err.hpp"
#include "ck/library/utility/device_memory.hpp"
#include "ck/library/utility/host_common_util.hpp"
#include "ck/library/utility/host_tensor.hpp"
#include "ck/library/utility/host_tensor_generator.hpp"
#include "ck/library/reference_tensor_operation/cpu/reference_layernorm.hpp"

using ADataType     = ck::half_t;
using BDataType     = ck::half_t;
using CDataType     = ck::half_t;
using GammaDataType = ck::half_t;
using BetaDataType  = ck::half_t;
using YDataType     = ck::half_t;
using AccDataType   = float;
using Add           = ck::tensor_operation::element_wise::Add;
using PassThrough   = ck::tensor_operation::element_wise::PassThrough;

constexpr int Rank         = 2;
constexpr int NumReduceDim = 1;

using DeviceInstance =
    ck::tensor_operation::device::DeviceElementwiseLayernormImpl<ADataType,
                                                                 BDataType,
                                                                 CDataType,
                                                                 GammaDataType,
                                                                 BetaDataType,
                                                                 AccDataType,
                                                                 YDataType,
                                                                 Add,
                                                                 PassThrough,
                                                                 Rank,
                                                                 NumReduceDim,
                                                                 256, // BlockSize
                                                                 8,   // ClusterM
                                                                 32,  // ClusterK
                                                                 1,   // SliceM
                                                                 8,   // SliceK
                                                                 1,   // SrcVecDim (0=M, 1=K)
                                                                 8,   // SrcScalarPerVector
                                                                 8,   // GammaScalarPerVector
                                                                 8,   // BetaScalarPerVector
                                                                 8>;  // OutScalarPerVector

template <typename HostTensorA, typename HostTensorB, typename HostTensorC, typename Functor>
void host_elementwise2D(HostTensorC& C,
                        const HostTensorA& A,
                        const HostTensorB& B,
                        const std::vector<std::size_t>& shape,
                        Functor functor)
{
    using ctype = ck::remove_reference_t<decltype(C(0, 0))>;

    for(std::size_t m = 0; m < shape[0]; ++m)
        for(std::size_t n = 0; n < shape[1]; ++n)
        {
            auto a_val  = A(m, n);
            auto b_val  = B(m, n);
            ctype c_val = 0;
            functor(c_val, a_val, b_val);
            C(m, n) = c_val;
        }
}

int main()
{
    bool time_kernel = true;

    ck::index_t M      = 512;
    ck::index_t N      = 1024;
    ck::index_t Stride = N;

    auto f_host_tensor_descriptor1d = [](std::size_t len, std::size_t stride) {
        return HostTensorDescriptor(std::vector<std::size_t>({len}),
                                    std::vector<std::size_t>({stride}));
    };

    auto f_host_tensor_descriptor2d = [](std::size_t row, std::size_t col, std::size_t stride) {
        return HostTensorDescriptor(std::vector<std::size_t>({row, col}),
                                    std::vector<std::size_t>({stride, 1}));
    };

    Tensor<ADataType> a(f_host_tensor_descriptor2d(M, N, Stride));
    Tensor<BDataType> b(f_host_tensor_descriptor2d(M, N, Stride));
    Tensor<CDataType> c(f_host_tensor_descriptor2d(M, N, Stride));
    Tensor<GammaDataType> gamma(f_host_tensor_descriptor1d(N, 1));
    Tensor<BetaDataType> beta(f_host_tensor_descriptor1d(N, 1));
    Tensor<YDataType> y(f_host_tensor_descriptor2d(M, N, Stride));

    a.GenerateTensorValue(GeneratorTensor_3<ADataType>{0.0, 1.0});
    b.GenerateTensorValue(GeneratorTensor_3<BDataType>{0.0, 1.0});
    gamma.GenerateTensorValue(GeneratorTensor_3<GammaDataType>{0.0, 1.0});
    beta.GenerateTensorValue(GeneratorTensor_3<BetaDataType>{0.0, 1.0});

    DeviceMem a_dev(sizeof(ADataType) * a.mDesc.GetElementSpaceSize());
    DeviceMem b_dev(sizeof(BDataType) * b.mDesc.GetElementSpaceSize());
    DeviceMem c_dev(sizeof(CDataType) * c.mDesc.GetElementSpaceSize());
    DeviceMem gamma_dev(sizeof(GammaDataType) * gamma.mDesc.GetElementSpaceSize());
    DeviceMem beta_dev(sizeof(BetaDataType) * beta.mDesc.GetElementSpaceSize());
    DeviceMem y_dev(sizeof(YDataType) * y.mDesc.GetElementSpaceSize());

    a_dev.ToDevice(a.mData.data());
    b_dev.ToDevice(b.mData.data());
    gamma_dev.ToDevice(gamma.mData.data());
    beta_dev.ToDevice(beta.mData.data());

    auto device_instance = DeviceInstance{};
    auto argument_ptr    = device_instance.MakeArgumentPointer(
        {M, N},
        std::vector<ck::index_t>{a.mDesc.GetStrides().begin(), a.mDesc.GetStrides().end()},
        std::vector<ck::index_t>{b.mDesc.GetStrides().begin(), b.mDesc.GetStrides().end()},
        std::vector<ck::index_t>{c.mDesc.GetStrides().begin(), c.mDesc.GetStrides().end()},
        std::vector<ck::index_t>{gamma.mDesc.GetStrides().begin(), gamma.mDesc.GetStrides().end()},
        std::vector<ck::index_t>{beta.mDesc.GetStrides().begin(), beta.mDesc.GetStrides().end()},
        std::vector<ck::index_t>{y.mDesc.GetStrides().begin(), y.mDesc.GetStrides().end()},
        {1},
        1e-4,
        a_dev.GetDeviceBuffer(),
        b_dev.GetDeviceBuffer(),
        c_dev.GetDeviceBuffer(),
        gamma_dev.GetDeviceBuffer(),
        beta_dev.GetDeviceBuffer(),
        y_dev.GetDeviceBuffer(),
        Add{},
        PassThrough{});

    if(!device_instance.IsSupportedArgument(argument_ptr.get()))
    {
        std::cout << "The runtime parameters are not supported" << std::endl;
        return 1;
    };

    auto invoker_ptr = device_instance.MakeInvokerPointer();
    float ela_time   = 0;
    ela_time         = invoker_ptr->Run(argument_ptr.get(), StreamConfig{nullptr, time_kernel});
    std::cout << "Time elapase is : " << ela_time << " s . " << std::endl;

    c_dev.FromDevice(c.mData.data());

    bool pass = true;
    {
        std::vector<std::size_t> mn = {static_cast<unsigned long>(M),
                                       static_cast<unsigned long>(N)};
        Tensor<CDataType> x(f_host_tensor_descriptor2d(M, N, Stride));
        host_elementwise2D<Tensor<ADataType>, Tensor<BDataType>, Tensor<CDataType>, Add>(
            x, a, b, mn, Add{});
        pass &= ck::utils::check_err(c.mData, x.mData, "Error: Incorrect results d1", 1e-3, 1e-3);

        if(!(pass))
        {
            std::cout << "add wrong" << std::endl;
        }

        Tensor<YDataType> host_y(f_host_tensor_descriptor2d(M, N, Stride));
        using ReferenceInstance = ck::tensor_operation::host::ReferenceLayernorm<CDataType,
                                                                                 GammaDataType,
                                                                                 BetaDataType,
                                                                                 YDataType,
                                                                                 AccDataType,
                                                                                 PassThrough,
                                                                                 Rank,
                                                                                 NumReduceDim>;

        ReferenceInstance ref;
        auto ref_argument =
            ref.MakeArgument(x, gamma, beta, host_y, PassThrough{}, {M, N}, {1}, 1e-4);
        auto ref_invoker = ref.MakeInvoker();
        ref_invoker.Run(ref_argument);

        y_dev.FromDevice(y.mData.data());
        pass &=
            ck::utils::check_err(y.mData, host_y.mData, "Error: Incorrect results d1", 1e-3, 1e-3);
        if(!(pass))
        {
            std::cout << "layernorm wrong" << std::endl;
        }
    }
    return (pass ? 0 : 1);
}
